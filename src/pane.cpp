#include "pane.hpp"

#include "theme.hpp"
#include "url.hpp"

#include <algorithm>
#include <cmath>

namespace otto {
namespace {

// Zoom presets, matching what most browsers step through.
constexpr double kZoomLevels[] = {0.3, 0.5, 0.67, 0.8, 0.9, 1.0, 1.1,
                                  1.25, 1.5, 1.75, 2.0, 2.5, 3.0};
constexpr double kZoomEpsilon = 0.001;

// GTK4 entries delegate focus to an internal GtkText child, so check ancestry.
bool has_focus_within(GtkWidget* widget) {
    GtkRoot* root = gtk_widget_get_root(widget);
    GtkWidget* focus = root ? gtk_root_get_focus(root) : nullptr;
    return focus && (focus == widget || gtk_widget_is_ancestor(focus, widget));
}

// URI to show in the address bar; blank pages show an empty bar.
const char* display_uri(WebKitWebView* view) {
    const char* uri = webkit_web_view_get_uri(view);
    if (!uri || g_strcmp0(uri, "about:blank") == 0) return "";
    return uri;
}

}  // namespace

// Connects a captureless lambda (converted to a plain function pointer) with
// `this` as user data, and remembers the instance so ~Pane can disconnect it.
template <typename Fn>
void Pane::connect(gpointer instance, const char* signal, Fn handler) {
    g_signal_connect(instance, signal, G_CALLBACK(+handler), this);
    auto* obj = G_OBJECT(instance);
    if (std::find(connected_.begin(), connected_.end(), obj) == connected_.end())
        connected_.push_back(obj);
}

Pane::Pane(PaneHost* host, WebKitWebView* view) : host_(host), view_(view) {
    root_ = GTK_WIDGET(g_object_ref_sink(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0)));
    gtk_widget_add_css_class(root_, "otto-pane");

    auto make_button = [](const char* icon, const char* tooltip) {
        GtkWidget* b = gtk_button_new_from_icon_name(icon);
        gtk_widget_set_tooltip_text(b, tooltip);
        gtk_widget_set_focusable(b, FALSE);
        return b;
    };
    back_ = make_button("go-previous-symbolic", "Back (Alt+Left)");
    forward_ = make_button("go-next-symbolic", "Forward (Alt+Right)");
    reload_ = make_button("view-refresh-symbolic", "Reload (Ctrl+R)");
    connect(back_, "clicked", [](GtkButton*, Pane* p) { p->go_back(); });
    connect(forward_, "clicked", [](GtkButton*, Pane* p) { p->go_forward(); });
    connect(reload_, "clicked", [](GtkButton*, Pane* p) { p->reload_or_stop(); });

    address_ = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(address_, "Search or enter address");
    gtk_entry_set_input_purpose(address_, GTK_INPUT_PURPOSE_URL);
    gtk_widget_set_hexpand(GTK_WIDGET(address_), TRUE);
    connect(address_, "activate", [](GtkEntry* e, Pane* p) {
        p->load(gtk_editable_get_text(GTK_EDITABLE(e)));
        p->focus_view();
    });

    // Shows the zoom level when it isn't 100%; clicking resets it.
    zoom_ = gtk_button_new_with_label("100%");
    gtk_widget_set_tooltip_text(zoom_, "Reset zoom (Ctrl+0)");
    gtk_widget_set_focusable(zoom_, FALSE);
    gtk_widget_set_visible(zoom_, FALSE);
    connect(zoom_, "clicked", [](GtkButton*, Pane* p) { p->zoom_reset(); });

    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(toolbar, "otto-toolbar");
    gtk_box_append(GTK_BOX(toolbar), back_);
    gtk_box_append(GTK_BOX(toolbar), forward_);
    gtk_box_append(GTK_BOX(toolbar), reload_);
    gtk_box_append(GTK_BOX(toolbar), GTK_WIDGET(address_));
    gtk_box_append(GTK_BOX(toolbar), zoom_);
    gtk_box_append(GTK_BOX(root_), toolbar);

    gtk_widget_set_hexpand(GTK_WIDGET(view_), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(view_), TRUE);
    gtk_box_append(GTK_BOX(root_), GTK_WIDGET(view_));

    // Any click inside the pane makes it active, before the click is handled;
    // so does keyboard focus moving into it.
    GtkGesture* press = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(press), 0);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(press), GTK_PHASE_CAPTURE);
    gtk_widget_add_controller(root_, GTK_EVENT_CONTROLLER(press));
    connect(press, "pressed", [](GtkGestureClick*, int, double, double, Pane* p) {
        p->host_->pane_activated(p);
    });
    GtkEventController* focus = gtk_event_controller_focus_new();
    gtk_widget_add_controller(root_, focus);
    connect(focus, "enter", [](GtkEventControllerFocus*, Pane* p) { p->host_->pane_activated(p); });

    connect(view_, "notify::uri", [](WebKitWebView*, GParamSpec*, Pane* p) { p->update_address(); });
    connect(view_, "notify::title", [](WebKitWebView*, GParamSpec*, Pane* p) {
        p->host_->pane_title_changed(p);
    });
    connect(view_, "notify::estimated-load-progress", [](WebKitWebView*, GParamSpec*, Pane* p) {
        p->update_progress();
    });
    connect(view_, "notify::is-loading", [](WebKitWebView*, GParamSpec*, Pane* p) {
        p->update_navigation();
        p->update_progress();
        p->host_->pane_loading_changed(p);
    });
    connect(view_, "notify::zoom-level", [](WebKitWebView*, GParamSpec*, Pane* p) { p->update_zoom(); });
    connect(view_, "load-changed", [](WebKitWebView*, WebKitLoadEvent event, Pane* p) {
        if (event == WEBKIT_LOAD_STARTED || event == WEBKIT_LOAD_COMMITTED) {
            p->page_committed_ = event == WEBKIT_LOAD_COMMITTED;
            p->update_background();
        }
        p->update_address();
        p->update_navigation();
        // Pages without a <title> never notify::title; refresh the URI fallback.
        if (event == WEBKIT_LOAD_COMMITTED || event == WEBKIT_LOAD_FINISHED)
            p->host_->pane_title_changed(p);
    });
    connect(view_, "load-failed",
            [](WebKitWebView*, WebKitLoadEvent, const char* uri, GError* error, Pane*) -> gboolean {
                if (!g_error_matches(error, WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_CANCELLED))
                    g_warning("Failed to load %s: %s", uri, error->message);
                return FALSE;  // let WebKit show its default error page
            });
    connect(view_, "create",
            [](WebKitWebView* view, WebKitNavigationAction*, Pane* p) -> GtkWidget* {
                return GTK_WIDGET(p->host_->pane_create_related_view(p, view));
            });
    connect(view_, "close", [](WebKitWebView*, Pane* p) { p->host_->pane_close_requested(p); });
    // Middle-click or Ctrl+click a link: hand it to the host for a background tab.
    connect(view_, "decide-policy",
            [](WebKitWebView*, WebKitPolicyDecision* decision, WebKitPolicyDecisionType type,
               Pane* p) -> gboolean {
                if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) return FALSE;
                WebKitNavigationAction* action = webkit_navigation_policy_decision_get_navigation_action(
                    WEBKIT_NAVIGATION_POLICY_DECISION(decision));
                if (webkit_navigation_action_get_navigation_type(action) !=
                    WEBKIT_NAVIGATION_TYPE_LINK_CLICKED)
                    return FALSE;
                bool middle = webkit_navigation_action_get_mouse_button(action) == GDK_BUTTON_MIDDLE;
                bool ctrl = (webkit_navigation_action_get_modifiers(action) & GDK_CONTROL_MASK) != 0;
                if (!middle && !ctrl) return FALSE;
                p->host_->pane_open_in_background(
                    p, webkit_uri_request_get_uri(webkit_navigation_action_get_request(action)));
                webkit_policy_decision_ignore(decision);
                return TRUE;
            });

    update_address();
    update_navigation();
    update_zoom();
    update_background();
}

Pane::~Pane() {
    cancel_pending_load();
    for (GObject* obj : connected_) g_signal_handlers_disconnect_by_data(obj, this);
    g_object_unref(root_);
}

void Pane::load(const std::string& input) {
    cancel_pending_load();
    std::string uri = normalize_input(input);
    webkit_web_view_load_uri(view_, uri.c_str());
}

void Pane::load_when_idle(const std::string& input) {
    cancel_pending_load();
    pending_uri_ = input;
    pending_load_ = g_idle_add_once([](gpointer data) {
        auto* p = static_cast<Pane*>(data);
        p->pending_load_ = 0;
        p->load(p->pending_uri_);
    }, this);
}

void Pane::cancel_pending_load() {
    if (pending_load_) g_source_remove(pending_load_);
    pending_load_ = 0;
}

void Pane::go_back() { webkit_web_view_go_back(view_); }
void Pane::go_forward() { webkit_web_view_go_forward(view_); }

void Pane::reload_or_stop() {
    if (webkit_web_view_is_loading(view_))
        webkit_web_view_stop_loading(view_);
    else
        webkit_web_view_reload(view_);
}

void Pane::zoom_step(int direction) {
    double current = webkit_web_view_get_zoom_level(view_);
    double next = current;
    if (direction > 0) {
        auto it = std::find_if(std::begin(kZoomLevels), std::end(kZoomLevels),
                               [&](double z) { return z > current + kZoomEpsilon; });
        if (it != std::end(kZoomLevels)) next = *it;
    } else {
        auto it = std::find_if(std::rbegin(kZoomLevels), std::rend(kZoomLevels),
                               [&](double z) { return z < current - kZoomEpsilon; });
        if (it != std::rend(kZoomLevels)) next = *it;
    }
    webkit_web_view_set_zoom_level(view_, next);
}

void Pane::zoom_reset() { webkit_web_view_set_zoom_level(view_, 1.0); }

void Pane::focus_address() { gtk_widget_grab_focus(GTK_WIDGET(address_)); }
void Pane::focus_view() { gtk_widget_grab_focus(GTK_WIDGET(view_)); }

void Pane::set_active(bool active) {
    if (active)
        gtk_widget_add_css_class(root_, "otto-active");
    else
        gtk_widget_remove_css_class(root_, "otto-active");
}

std::string Pane::title() const {
    const char* title = webkit_web_view_get_title(view_);
    if (title && *title) return title;
    const char* uri = display_uri(view_);
    return *uri ? uri : "New Tab";
}

bool Pane::is_loading() const { return webkit_web_view_is_loading(view_); }

// WebKit paints the view's background colour under any page that doesn't set its
// own (most don't), so black can only stand in until a page commits; after that
// it's the usual white. about:blank stays black, as it has nothing to show.
void Pane::update_background() {
    const char* uri = webkit_web_view_get_uri(view_);
    bool blank = !uri || !*uri || g_str_equal(uri, "about:blank");
    bool black = theme::is_dark() && (!page_committed_ || blank);
    GdkRGBA color = black ? GdkRGBA{0, 0, 0, 1} : GdkRGBA{1, 1, 1, 1};
    webkit_web_view_set_background_color(view_, &color);
}

void Pane::update_address() {
    if (has_focus_within(GTK_WIDGET(address_))) return;  // don't clobber typing
    gtk_editable_set_text(GTK_EDITABLE(address_), display_uri(view_));
}

void Pane::update_navigation() {
    gtk_widget_set_sensitive(back_, webkit_web_view_can_go_back(view_));
    gtk_widget_set_sensitive(forward_, webkit_web_view_can_go_forward(view_));
    bool loading = webkit_web_view_is_loading(view_);
    gtk_button_set_icon_name(GTK_BUTTON(reload_),
                             loading ? "process-stop-symbolic" : "view-refresh-symbolic");
    gtk_widget_set_tooltip_text(reload_, loading ? "Stop" : "Reload (Ctrl+R)");
}

void Pane::update_progress() {
    double progress = webkit_web_view_is_loading(view_)
        ? webkit_web_view_get_estimated_load_progress(view_) : 0.0;
    gtk_entry_set_progress_fraction(address_, progress >= 1.0 ? 0.0 : progress);
}

void Pane::update_zoom() {
    double zoom = webkit_web_view_get_zoom_level(view_);
    gtk_widget_set_visible(zoom_, std::abs(zoom - 1.0) >= kZoomEpsilon);
    g_autofree char* label = g_strdup_printf("%d%%", static_cast<int>(std::lround(zoom * 100)));
    gtk_button_set_label(GTK_BUTTON(zoom_), label);
}

}  // namespace otto
