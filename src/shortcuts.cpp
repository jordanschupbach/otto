#include "shortcuts.hpp"

namespace otto {
namespace {

struct Binding {
    const char* action;  // "win.*" from BrowserWindow, "window.close" from GtkWindow, "app.quit"
    const char* keys;    // GtkShortcutTrigger syntax; alternatives separated by '|'
};

constexpr Binding kBindings[] = {
    // Navigation (active pane)
    {"win.back", "<Alt>Left"},
    {"win.forward", "<Alt>Right"},
    {"win.reload", "<Control>r|F5"},
    {"win.focus-address", "<Control>l|F6"},

    // Panes, mep-style: Alt+v side by side, Alt+s stacked, Alt+h/j/k/l to move.
    {"win.split-vertical", "<Alt>v"},
    {"win.split-horizontal", "<Alt>s"},
    {"win.close-pane", "<Alt>d"},
    {"win.focus-left", "<Alt>h"},
    {"win.focus-down", "<Alt>j"},
    {"win.focus-up", "<Alt>k"},
    {"win.focus-right", "<Alt>l"},
    // Alt+Shift+h/j/k/l moves the active pane's divider that way (holding repeats).
    // Shifted letters arrive as uppercase keyvals on some setups, so accept both.
    {"win.resize-left", "<Alt><Shift>h|<Alt><Shift>H"},
    {"win.resize-down", "<Alt><Shift>j|<Alt><Shift>J"},
    {"win.resize-up", "<Alt><Shift>k|<Alt><Shift>K"},
    {"win.resize-right", "<Alt><Shift>l|<Alt><Shift>L"},
    // Alt+Ctrl+h/j/k/l swaps the active pane with its neighbour that way.
    {"win.swap-left", "<Control><Alt>h"},
    {"win.swap-down", "<Control><Alt>j"},
    {"win.swap-up", "<Control><Alt>k"},
    {"win.swap-right", "<Control><Alt>l"},

    // Tabs
    {"win.new-tab", "<Control>t"},
    {"win.close-tab", "<Control>w"},
    {"win.next-tab", "<Control>Tab|<Control>Page_Down"},
    {"win.prev-tab", "<Control><Shift>Tab|<Control><Shift>ISO_Left_Tab|<Control>Page_Up"},

    // Zoom (active pane). Ctrl+= is the unshifted "+" key on most layouts.
    {"win.zoom-in", "<Control>equal|<Control>plus|<Control>KP_Add"},
    {"win.zoom-out", "<Control>minus|<Control>KP_Subtract"},
    {"win.zoom-reset", "<Control>0|<Control>KP_0"},

    // Window / app
    {"window.close", "<Control><Shift>w"},
    {"app.quit", "<Control>q"},
};

}  // namespace

void install_shortcuts(GtkWidget* window) {
    GtkEventController* controller = gtk_shortcut_controller_new();
    gtk_event_controller_set_propagation_phase(controller, GTK_PHASE_CAPTURE);
    gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(controller), GTK_SHORTCUT_SCOPE_LOCAL);

    for (const Binding& b : kBindings) {
        GtkShortcutTrigger* trigger = gtk_shortcut_trigger_parse_string(b.keys);
        if (!trigger) {
            g_warning("Invalid shortcut \"%s\" for %s", b.keys, b.action);
            continue;
        }
        gtk_shortcut_controller_add_shortcut(
            GTK_SHORTCUT_CONTROLLER(controller),
            gtk_shortcut_new(trigger, gtk_named_action_new(b.action)));
    }
    gtk_widget_add_controller(window, controller);
}

}  // namespace otto
