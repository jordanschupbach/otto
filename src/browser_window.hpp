#pragma once

#include "layout.hpp"
#include "pane.hpp"

#include <gtk/gtk.h>
#include <webkit/webkit.h>

#include <memory>
#include <string>
#include <vector>

namespace otto {

// A top-level browser window: a notebook of tabs. Each tab holds one or more
// panes arranged in a tree of splits (nested GtkPaned), with exactly one
// active pane that keyboard commands (navigation, zoom, Ctrl+L, ...) act on.
//
// Lifetime is tied to the GtkWindow: the BrowserWindow is deleted when the
// window is destroyed, so callers only need `new BrowserWindow(...)`.
class BrowserWindow final : private PaneHost {
public:
    // Opens one tab per entry in `inputs` (URLs or search terms).
    BrowserWindow(GtkApplication* app, const std::vector<std::string>& inputs);
    BrowserWindow(const BrowserWindow&) = delete;
    BrowserWindow& operator=(const BrowserWindow&) = delete;

    void present();

private:
    struct Tab;

    ~BrowserWindow();

    void build_ui(GtkApplication* app);
    void install_actions();

    // Tabs. `position` -1 appends; otherwise inserts at that index.
    Tab* new_tab(const std::string& input, bool select, int position = -1);
    Tab* add_tab(WebKitWebView* view, bool select, int position);
    void close_tab(Tab* tab);
    void select_relative_tab(int offset);
    Tab* current_tab() const;
    Tab* tab_of(const Pane* pane) const;
    int insert_position_after_current() const;
    WebKitWebView* create_web_view(WebKitWebView* related = nullptr);
    void update_tab_label(Tab* tab);
    void update_window_title();
    void update_theme_button();
    void on_theme_changed();

    // Panes within the current tab.
    Pane* active_pane() const;
    void activate_pane(Tab* tab, Pane* pane);
    void split_active_pane(GtkOrientation orientation);
    void close_pane(Pane* pane);
    Pane* neighbor_of_active(Tab* tab, Direction dir) const;
    void focus_pane_in_direction(Direction dir);
    void resize_active_pane(Direction dir);  // move its divider that way
    void swap_active_pane(Direction dir);    // trade places with the neighbour that way

    // PaneHost
    void pane_activated(Pane* pane) override;
    void pane_title_changed(Pane* pane) override;
    void pane_loading_changed(Pane* pane) override;
    void pane_close_requested(Pane* pane) override;
    WebKitWebView* pane_create_related_view(Pane* pane, WebKitWebView* opener) override;
    void pane_open_in_background(Pane* pane, const std::string& uri) override;

    static void on_switch_page(GtkNotebook*, GtkWidget* page, guint, BrowserWindow* self);
    static void on_destroy(GtkWidget*, BrowserWindow* self);

    // Page scripts (link hints) and the message channel they use to reach us.
    void setup_page_scripts();
    static void on_script_message(WebKitUserContentManager*, JSCValue* message, BrowserWindow* self);

    GtkWindow* window_ = nullptr;
    GtkNotebook* notebook_ = nullptr;     // we hold an extra ref; see on_destroy
    GtkWidget* theme_button_ = nullptr;   // light/dark toggle in the header bar
    WebKitSettings* settings_ = nullptr;  // shared by all views in this window
    WebKitUserContentManager* content_manager_ = nullptr;  // page scripts; shared likewise

    std::vector<std::unique_ptr<Tab>> tabs_;
};

}  // namespace otto
