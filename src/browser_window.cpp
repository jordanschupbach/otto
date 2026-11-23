#include "browser_window.hpp"

#include "shortcuts.hpp"

#include <algorithm>

namespace otto {
namespace {

constexpr const char* kTabDataKey = "otto-tab";

// The active pane gets an accent border, but only while a tab is split:
// with a single pane there is nothing to distinguish.
constexpr const char* kCss = R"css(
.otto-tab.otto-split .otto-pane {
    border: 2px solid transparent;
}
.otto-tab.otto-split .otto-pane.otto-active {
    border-color: #3584e4;
}
.otto-toolbar {
    padding: 4px;
}
)css";

void install_css_once() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, kCss);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

// Put `replacement` (unparented) where `old` sits in `parent`, which is either
// a GtkPaned inside the split tree or the tab's root GtkBox. The caller must
// hold a reference on `old` if it should survive being unparented.
void replace_child(GtkWidget* parent, GtkWidget* old, GtkWidget* replacement) {
    if (GTK_IS_PANED(parent)) {
        GtkPaned* paned = GTK_PANED(parent);
        if (gtk_paned_get_start_child(paned) == old)
            gtk_paned_set_start_child(paned, replacement);
        else
            gtk_paned_set_end_child(paned, replacement);
    } else {
        gtk_box_remove(GTK_BOX(parent), old);
        gtk_box_append(GTK_BOX(parent), replacement);
    }
}


}  // namespace

struct BrowserWindow::Tab {
    GtkWidget* root = nullptr;  // notebook page; holds the split tree
    std::vector<std::unique_ptr<Pane>> panes;
    Pane* active = nullptr;

    GtkWidget* label_box = nullptr;
    GtkLabel* title = nullptr;
    GtkWidget* spinner = nullptr;
};

// ---------------------------------------------------------------------------
// Construction

BrowserWindow::BrowserWindow(GtkApplication* app, const std::vector<std::string>& inputs) {
    install_css_once();
    settings_ = webkit_settings_new();
    webkit_settings_set_enable_developer_extras(settings_, TRUE);  // right-click → Inspect

    build_ui(app);
    install_actions();

    for (size_t i = 0; i < inputs.size(); ++i) new_tab(inputs[i], i == 0);
    if (tabs_.empty()) new_tab("about:blank", true);
}

BrowserWindow::~BrowserWindow() { g_object_unref(settings_); }

void BrowserWindow::present() { gtk_window_present(window_); }

void BrowserWindow::build_ui(GtkApplication* app) {
    window_ = GTK_WINDOW(gtk_application_window_new(app));
    gtk_window_set_default_size(window_, 1200, 800);
    gtk_window_set_title(window_, "Otto");
    gtk_window_set_titlebar(window_, gtk_header_bar_new());
    install_shortcuts(GTK_WIDGET(window_));

    // We hold our own reference so the notebook (and the web views in it) outlive
    // the window's teardown until on_destroy has disconnected our handlers.
    notebook_ = GTK_NOTEBOOK(g_object_ref_sink(gtk_notebook_new()));
    gtk_notebook_set_scrollable(notebook_, TRUE);
    gtk_notebook_set_show_border(notebook_, FALSE);
    gtk_widget_set_focusable(GTK_WIDGET(notebook_), FALSE);  // keep Ctrl+PgUp/Dn for us

    GtkWidget* new_tab_button = gtk_button_new_from_icon_name("tab-new-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(new_tab_button), FALSE);
    gtk_widget_set_tooltip_text(new_tab_button, "New tab (Ctrl+T)");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(new_tab_button), "win.new-tab");
    gtk_notebook_set_action_widget(notebook_, new_tab_button, GTK_PACK_END);

    // Run after the default handler so the notebook's current page is updated.
    g_signal_connect_after(notebook_, "switch-page", G_CALLBACK(on_switch_page), this);
    gtk_window_set_child(window_, GTK_WIDGET(notebook_));

    g_signal_connect(window_, "destroy", G_CALLBACK(on_destroy), this);
}

void BrowserWindow::install_actions() {
    struct Entry {
        const char* name;
        void (*fn)(BrowserWindow*);
    };
    // Helper for actions that act on the active pane.
    #define ON_PANE(expr) [](BrowserWindow* s) { if (Pane* p = s->active_pane()) p->expr; }
    static const Entry entries[] = {
        {"back", ON_PANE(go_back())},
        {"forward", ON_PANE(go_forward())},
        {"reload", ON_PANE(reload_or_stop())},
        {"focus-address", ON_PANE(focus_address())},
        {"zoom-in", ON_PANE(zoom_step(+1))},
        {"zoom-out", ON_PANE(zoom_step(-1))},
        {"zoom-reset", ON_PANE(zoom_reset())},
        {"new-tab", [](BrowserWindow* s) {
             s->new_tab("about:blank", true);
             if (Pane* p = s->active_pane()) p->focus_address();
         }},
        {"close-tab", [](BrowserWindow* s) {
             if (Tab* t = s->current_tab()) s->close_tab(t);
         }},
        {"next-tab", [](BrowserWindow* s) { s->select_relative_tab(+1); }},
        {"prev-tab", [](BrowserWindow* s) { s->select_relative_tab(-1); }},
        // Vim naming: a "vertical" split puts panes side by side.
        {"split-vertical", [](BrowserWindow* s) { s->split_active_pane(GTK_ORIENTATION_HORIZONTAL); }},
        {"split-horizontal", [](BrowserWindow* s) { s->split_active_pane(GTK_ORIENTATION_VERTICAL); }},
        {"close-pane", [](BrowserWindow* s) {
             if (Pane* p = s->active_pane()) s->close_pane(p);
         }},
        {"focus-left", [](BrowserWindow* s) { s->focus_pane_in_direction(Direction::Left); }},
        {"focus-down", [](BrowserWindow* s) { s->focus_pane_in_direction(Direction::Down); }},
        {"focus-up", [](BrowserWindow* s) { s->focus_pane_in_direction(Direction::Up); }},
        {"focus-right", [](BrowserWindow* s) { s->focus_pane_in_direction(Direction::Right); }},
        {"resize-left", [](BrowserWindow* s) { s->resize_active_pane(Direction::Left); }},
        {"resize-down", [](BrowserWindow* s) { s->resize_active_pane(Direction::Down); }},
        {"resize-up", [](BrowserWindow* s) { s->resize_active_pane(Direction::Up); }},
        {"resize-right", [](BrowserWindow* s) { s->resize_active_pane(Direction::Right); }},
        {"swap-left", [](BrowserWindow* s) { s->swap_active_pane(Direction::Left); }},
        {"swap-down", [](BrowserWindow* s) { s->swap_active_pane(Direction::Down); }},
        {"swap-up", [](BrowserWindow* s) { s->swap_active_pane(Direction::Up); }},
        {"swap-right", [](BrowserWindow* s) { s->swap_active_pane(Direction::Right); }},
    };
    #undef ON_PANE

    for (const Entry& e : entries) {
        GSimpleAction* action = g_simple_action_new(e.name, nullptr);
        g_object_set_data(G_OBJECT(action), "otto-fn", reinterpret_cast<gpointer>(e.fn));
        g_signal_connect(action, "activate",
                         G_CALLBACK(+[](GSimpleAction* a, GVariant*, gpointer self) {
                             auto fn = reinterpret_cast<void (*)(BrowserWindow*)>(
                                 g_object_get_data(G_OBJECT(a), "otto-fn"));
                             fn(static_cast<BrowserWindow*>(self));
                         }),
                         this);
        g_action_map_add_action(G_ACTION_MAP(window_), G_ACTION(action));
        g_object_unref(action);
    }
}

// ---------------------------------------------------------------------------
// Tabs

WebKitWebView* BrowserWindow::create_web_view(WebKitWebView* related) {
    // A related view shares its opener's web process (needed for window.open)
    // and inherits its settings.
    GObject* view = related
        ? static_cast<GObject*>(g_object_new(WEBKIT_TYPE_WEB_VIEW, "related-view", related, nullptr))
        : static_cast<GObject*>(g_object_new(WEBKIT_TYPE_WEB_VIEW, "settings", settings_, nullptr));
    return WEBKIT_WEB_VIEW(view);
}

BrowserWindow::Tab* BrowserWindow::new_tab(const std::string& input, bool select, int position) {
    Tab* tab = add_tab(create_web_view(), select, position);
    tab->active->load(input);
    return tab;
}

BrowserWindow::Tab* BrowserWindow::add_tab(WebKitWebView* view, bool select, int position) {
    auto owned = std::make_unique<Tab>();
    Tab* tab = owned.get();

    tab->root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(tab->root, "otto-tab");
    g_object_set_data(G_OBJECT(tab->root), kTabDataKey, tab);

    auto pane = std::make_unique<Pane>(static_cast<PaneHost*>(this), view);
    gtk_box_append(GTK_BOX(tab->root), pane->widget());
    tab->active = pane.get();
    pane->set_active(true);
    tab->panes.push_back(std::move(pane));

    // Tab label: [spinner] title [×]
    tab->label_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    tab->spinner = gtk_spinner_new();
    gtk_widget_set_visible(tab->spinner, FALSE);
    tab->title = GTK_LABEL(gtk_label_new("New Tab"));
    gtk_label_set_ellipsize(tab->title, PANGO_ELLIPSIZE_END);
    gtk_label_set_width_chars(tab->title, 12);
    gtk_label_set_max_width_chars(tab->title, 24);
    gtk_label_set_single_line_mode(tab->title, TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(tab->title), TRUE);
    gtk_label_set_xalign(tab->title, 0.0f);

    GtkWidget* close = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(close), FALSE);
    gtk_widget_set_focusable(close, FALSE);
    gtk_widget_set_tooltip_text(close, "Close tab (Ctrl+W)");
    g_object_set_data(G_OBJECT(close), "otto-window", this);
    g_signal_connect(close, "clicked", G_CALLBACK(+[](GtkButton* b, Tab* t) {
        static_cast<BrowserWindow*>(g_object_get_data(G_OBJECT(b), "otto-window"))->close_tab(t);
    }), tab);

    gtk_box_append(GTK_BOX(tab->label_box), tab->spinner);
    gtk_box_append(GTK_BOX(tab->label_box), GTK_WIDGET(tab->title));
    gtk_box_append(GTK_BOX(tab->label_box), close);

    // Middle-click a tab to close it.
    GtkGesture* middle = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(middle), GDK_BUTTON_MIDDLE);
    g_object_set_data(G_OBJECT(middle), "otto-window", this);
    g_signal_connect(middle, "released",
                     G_CALLBACK(+[](GtkGestureClick* g, int, double, double, Tab* t) {
                         static_cast<BrowserWindow*>(g_object_get_data(G_OBJECT(g), "otto-window"))
                             ->close_tab(t);
                     }),
                     tab);
    gtk_widget_add_controller(tab->label_box, GTK_EVENT_CONTROLLER(middle));

    tabs_.push_back(std::move(owned));

    int index = gtk_notebook_insert_page(notebook_, tab->root, tab->label_box, position);
    gtk_notebook_set_tab_reorderable(notebook_, tab->root, TRUE);
    if (select) gtk_notebook_set_current_page(notebook_, index);
    return tab;
}

void BrowserWindow::close_tab(Tab* tab) {
    int index = gtk_notebook_page_num(notebook_, tab->root);

    // Panes disconnect their handlers here; their widgets stay alive inside
    // the page until it's removed below.
    tab->active = nullptr;
    tab->panes.clear();

    // Removing the page may switch tabs; that handler only touches the new page.
    gtk_notebook_remove_page(notebook_, index);
    std::erase_if(tabs_, [tab](const auto& t) { return t.get() == tab; });

    if (tabs_.empty()) gtk_window_destroy(window_);
}

void BrowserWindow::select_relative_tab(int offset) {
    int n = gtk_notebook_get_n_pages(notebook_);
    if (n <= 1) return;
    int current = gtk_notebook_get_current_page(notebook_);
    gtk_notebook_set_current_page(notebook_, ((current + offset) % n + n) % n);
}

BrowserWindow::Tab* BrowserWindow::current_tab() const {
    int index = gtk_notebook_get_current_page(notebook_);
    if (index < 0) return nullptr;
    GtkWidget* page = gtk_notebook_get_nth_page(notebook_, index);
    return static_cast<Tab*>(g_object_get_data(G_OBJECT(page), kTabDataKey));
}

BrowserWindow::Tab* BrowserWindow::tab_of(const Pane* pane) const {
    for (const auto& tab : tabs_)
        for (const auto& p : tab->panes)
            if (p.get() == pane) return tab.get();
    return nullptr;
}

int BrowserWindow::insert_position_after_current() const {
    return gtk_notebook_get_current_page(notebook_) + 1;
}

void BrowserWindow::update_tab_label(Tab* tab) {
    if (!tab->active) return;
    std::string title = tab->active->title();
    gtk_label_set_text(tab->title, title.c_str());
    gtk_widget_set_tooltip_text(tab->label_box, title.c_str());

    bool loading = tab->active->is_loading();
    gtk_widget_set_visible(tab->spinner, loading);
    gtk_spinner_set_spinning(GTK_SPINNER(tab->spinner), loading);
}

void BrowserWindow::update_window_title() {
    Pane* pane = active_pane();
    const char* title = pane ? webkit_web_view_get_title(pane->view()) : nullptr;
    if (title && *title) {
        g_autofree char* full = g_strdup_printf("%s — Otto", title);
        gtk_window_set_title(window_, full);
    } else {
        gtk_window_set_title(window_, "Otto");
    }
}

// ---------------------------------------------------------------------------
// Panes

Pane* BrowserWindow::active_pane() const {
    Tab* tab = current_tab();
    return tab ? tab->active : nullptr;
}

void BrowserWindow::activate_pane(Tab* tab, Pane* pane) {
    if (tab->active != pane) {
        if (tab->active) tab->active->set_active(false);
        tab->active = pane;
        pane->set_active(true);
    }
    update_tab_label(tab);
    if (tab == current_tab()) update_window_title();
}

void BrowserWindow::split_active_pane(GtkOrientation orientation) {
    Tab* tab = current_tab();
    if (!tab || !tab->active) return;

    GtkWidget* old = tab->active->widget();  // the Pane holds a ref, so it survives reparenting
    GtkWidget* parent = gtk_widget_get_parent(old);
    // The new paned takes exactly the old pane's slot, so split its current size 50/50.
    int extent = orientation == GTK_ORIENTATION_HORIZONTAL ? gtk_widget_get_width(old)
                                                           : gtk_widget_get_height(old);

    GtkWidget* paned = gtk_paned_new(orientation);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);
    gtk_widget_set_hexpand(paned, TRUE);
    gtk_widget_set_vexpand(paned, TRUE);
    replace_child(parent, old, paned);

    auto fresh = std::make_unique<Pane>(static_cast<PaneHost*>(this), create_web_view());
    Pane* pane = fresh.get();
    tab->panes.push_back(std::move(fresh));

    gtk_paned_set_start_child(GTK_PANED(paned), old);
    gtk_paned_set_end_child(GTK_PANED(paned), pane->widget());
    if (extent > 0) gtk_paned_set_position(GTK_PANED(paned), extent / 2);

    gtk_widget_add_css_class(tab->root, "otto-split");
    activate_pane(tab, pane);
    pane->load("about:blank");
    pane->focus_address();
}

void BrowserWindow::close_pane(Pane* pane) {
    Tab* tab = tab_of(pane);
    if (!tab) return;
    if (tab->panes.size() == 1) {
        close_tab(tab);
        return;
    }

    // The pane's parent is a GtkPaned; replace that paned with the other child.
    GtkWidget* widget = pane->widget();
    GtkPaned* paned = GTK_PANED(gtk_widget_get_parent(widget));
    GtkWidget* sibling = gtk_paned_get_start_child(paned) == widget
        ? gtk_paned_get_end_child(paned) : gtk_paned_get_start_child(paned);
    GtkWidget* grandparent = gtk_widget_get_parent(GTK_WIDGET(paned));

    // Drop focus first: GtkPaned warns if the focused child is detached under it.
    // The next active pane gets focus below.
    gtk_root_set_focus(GTK_ROOT(window_), nullptr);

    g_object_ref(sibling);
    gtk_paned_set_start_child(paned, nullptr);
    gtk_paned_set_end_child(paned, nullptr);
    replace_child(grandparent, GTK_WIDGET(paned), sibling);  // drops the paned
    g_object_unref(sibling);

    // Next active pane: the first one in the subtree that took this one's place.
    Pane* next = nullptr;
    for (const auto& p : tab->panes) {
        if (p.get() == pane) continue;
        if (p->widget() == sibling || gtk_widget_is_ancestor(p->widget(), sibling)) {
            next = p.get();
            break;
        }
    }

    if (tab->active == pane) tab->active = nullptr;
    std::erase_if(tab->panes, [pane](const auto& p) { return p.get() == pane; });
    if (tab->panes.size() == 1) gtk_widget_remove_css_class(tab->root, "otto-split");

    if (next) {
        activate_pane(tab, next);
        if (tab == current_tab()) next->focus_view();
    }
}

Pane* BrowserWindow::neighbor_of_active(Tab* tab, Direction dir) const {
    if (!tab || !tab->active || tab->panes.size() < 2) return nullptr;

    std::vector<Rect> rects;
    size_t from = 0;
    for (size_t i = 0; i < tab->panes.size(); ++i) {
        graphene_rect_t r;
        if (!gtk_widget_compute_bounds(tab->panes[i]->widget(), tab->root, &r)) return nullptr;
        rects.push_back({r.origin.x, r.origin.y, r.size.width, r.size.height});
        if (tab->panes[i].get() == tab->active) from = i;
    }

    auto to = find_neighbor(rects, from, dir);
    return to ? tab->panes[*to].get() : nullptr;
}

void BrowserWindow::focus_pane_in_direction(Direction dir) {
    Tab* tab = current_tab();
    if (Pane* pane = neighbor_of_active(tab, dir)) {
        activate_pane(tab, pane);
        pane->focus_view();
    }
}

void BrowserWindow::resize_active_pane(Direction dir) {
    Pane* pane = active_pane();
    if (!pane) return;

    // The nearest enclosing split along the key's axis owns the divider to move.
    // Whether the active pane is its start or end child, moving the divider
    // the way the key points is the same "position += step".
    GtkOrientation axis = is_horizontal(dir) ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
    for (GtkWidget* w = gtk_widget_get_parent(pane->widget()); w && GTK_IS_PANED(w);
         w = gtk_widget_get_parent(w)) {
        if (gtk_orientable_get_orientation(GTK_ORIENTABLE(w)) != axis) continue;
        GtkPaned* paned = GTK_PANED(w);
        int extent = is_horizontal(dir) ? gtk_widget_get_width(w) : gtk_widget_get_height(w);
        int min = 0, max = 0;
        g_object_get(paned, "min-position", &min, "max-position", &max, nullptr);
        int position = gtk_paned_get_position(paned) + divider_step(dir, extent);
        gtk_paned_set_position(paned, std::clamp(position, min, max));
        return;
    }
}

namespace {

// Where a pane sits in the split tree: a GtkPaned and which side of it.
struct Slot {
    GtkPaned* paned;
    bool start;
};

Slot slot_of(GtkWidget* widget) {
    GtkPaned* paned = GTK_PANED(gtk_widget_get_parent(widget));
    return {paned, gtk_paned_get_start_child(paned) == widget};
}

void fill_slot(Slot slot, GtkWidget* widget) {
    if (slot.start)
        gtk_paned_set_start_child(slot.paned, widget);
    else
        gtk_paned_set_end_child(slot.paned, widget);
}

}  // namespace

void BrowserWindow::swap_active_pane(Direction dir) {
    Tab* tab = current_tab();
    Pane* other = neighbor_of_active(tab, dir);
    if (!other) return;
    Pane* active = tab->active;

    // With 2+ panes every pane's parent is a GtkPaned. The two panes trade
    // slots; the splits keep their shape and sizes. Each Pane holds a ref on
    // its widget, so both survive being detached.
    Slot a = slot_of(active->widget());
    Slot b = slot_of(other->widget());

    gtk_root_set_focus(GTK_ROOT(window_), nullptr);  // see close_pane
    fill_slot(a, nullptr);
    fill_slot(b, nullptr);
    fill_slot(a, other->widget());
    fill_slot(b, active->widget());

    // Focus travels with the moved pane (as in mep), so repeating the key
    // keeps walking it the same way.
    active->focus_view();
}

// ---------------------------------------------------------------------------
// PaneHost

void BrowserWindow::pane_activated(Pane* pane) {
    if (Tab* tab = tab_of(pane)) activate_pane(tab, pane);
}

void BrowserWindow::pane_title_changed(Pane* pane) {
    Tab* tab = tab_of(pane);
    if (!tab || tab->active != pane) return;
    update_tab_label(tab);
    if (tab == current_tab()) update_window_title();
}

void BrowserWindow::pane_loading_changed(Pane* pane) {
    Tab* tab = tab_of(pane);
    if (tab && tab->active == pane) update_tab_label(tab);
}

void BrowserWindow::pane_close_requested(Pane* pane) { close_pane(pane); }

// target="_blank" links and window.open(): open a new tab next to the current one.
WebKitWebView* BrowserWindow::pane_create_related_view(Pane*, WebKitWebView* opener) {
    WebKitWebView* view = create_web_view(opener);
    add_tab(view, true, insert_position_after_current());
    return view;
}

void BrowserWindow::pane_open_in_background(Pane*, const std::string& uri) {
    new_tab(uri, false, insert_position_after_current());
}

// ---------------------------------------------------------------------------
// Window signal handlers

void BrowserWindow::on_switch_page(GtkNotebook*, GtkWidget* page, guint, BrowserWindow* self) {
    auto* tab = static_cast<Tab*>(g_object_get_data(G_OBJECT(page), kTabDataKey));
    self->update_window_title();
    if (tab && tab->active) tab->active->focus_view();
}

void BrowserWindow::on_destroy(GtkWidget*, BrowserWindow* self) {
    // GtkWindow drops its child before emitting "destroy", but our extra notebook
    // reference keeps the tabs alive. Disconnect everything that points at us
    // (destroying the Panes does that), then release the notebook, which tears
    // down the web views.
    for (const auto& tab : self->tabs_) {
        tab->active = nullptr;
        tab->panes.clear();
    }
    g_signal_handlers_disconnect_by_data(self->notebook_, self);
    g_object_unref(self->notebook_);
    delete self;
}

}  // namespace otto
