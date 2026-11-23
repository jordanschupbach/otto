#pragma once

#include <gtk/gtk.h>
#include <webkit/webkit.h>

#include <string>
#include <vector>

namespace otto {

class Pane;

// What a Pane needs from whoever owns it (the BrowserWindow).
class PaneHost {
public:
    // The user clicked into or focused the pane.
    virtual void pane_activated(Pane* pane) = 0;
    virtual void pane_title_changed(Pane* pane) = 0;
    virtual void pane_loading_changed(Pane* pane) = 0;
    // The page called window.close(). The host may delete the pane.
    virtual void pane_close_requested(Pane* pane) = 0;
    // target="_blank" / window.open(): return a new view related to `opener`.
    virtual WebKitWebView* pane_create_related_view(Pane* pane, WebKitWebView* opener) = 0;
    // Middle-click / Ctrl+click on a link.
    virtual void pane_open_in_background(Pane* pane, const std::string& uri) = 0;

protected:
    ~PaneHost() = default;
};

// One browsing surface: a toolbar (back, forward, reload, address bar, zoom)
// above a WebKitWebView. A tab holds one or more panes in a split layout.
//
// The Pane keeps its own reference on widget(), so the owner may unparent and
// re-parent it freely while rearranging splits. Destroying the Pane
// disconnects every handler pointing at it and drops that reference.
class Pane {
public:
    Pane(PaneHost* host, WebKitWebView* view);
    ~Pane();
    Pane(const Pane&) = delete;
    Pane& operator=(const Pane&) = delete;

    GtkWidget* widget() const { return root_; }
    WebKitWebView* view() const { return view_; }

    void load(const std::string& input);  // URL or search terms
    void go_back();
    void go_forward();
    void reload_or_stop();
    void zoom_step(int direction);  // +1 in, -1 out
    void zoom_reset();

    void focus_address();
    void focus_view();
    void set_active(bool active);  // toggles the highlight style class

    std::string title() const;  // page title, else URI, else "New Tab"
    bool is_loading() const;

private:
    template <typename Fn>
    void connect(gpointer instance, const char* signal, Fn handler);

    void update_address();
    void update_navigation();
    void update_progress();
    void update_zoom();

    PaneHost* host_;
    WebKitWebView* view_;
    GtkWidget* root_ = nullptr;
    GtkEntry* address_ = nullptr;
    GtkWidget* back_ = nullptr;
    GtkWidget* forward_ = nullptr;
    GtkWidget* reload_ = nullptr;
    GtkWidget* zoom_ = nullptr;

    std::vector<GObject*> connected_;  // objects with handlers bound to `this`
};

}  // namespace otto
