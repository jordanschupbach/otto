#include "browser_window.hpp"
#include "theme.hpp"

#include <gtk/gtk.h>

#include <string>
#include <vector>

namespace {

constexpr const char* kHomePage = "https://duckduckgo.com/";

// Each invocation opens one window with a tab per argument.
int on_command_line(GApplication* app, GApplicationCommandLine* cmdline, gpointer) {
    int argc = 0;
    g_auto(GStrv) argv = g_application_command_line_get_arguments(cmdline, &argc);

    std::vector<std::string> inputs(argv + 1, argv + argc);
    if (inputs.empty()) inputs.emplace_back(kHomePage);
    (new otto::BrowserWindow(GTK_APPLICATION(app), inputs))->present();
    return 0;
}

void on_startup(GApplication* app, gpointer) {
    otto::theme::init();

    // Keyboard shortcuts are per window; see shortcuts.cpp.
    GSimpleAction* quit = g_simple_action_new("quit", nullptr);
    g_signal_connect_swapped(quit, "activate", G_CALLBACK(g_application_quit), app);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(quit));
    g_object_unref(quit);
}

}  // namespace

int main(int argc, char** argv) {
    GtkApplication* app =
        gtk_application_new("io.github.otto.Browser", G_APPLICATION_HANDLES_COMMAND_LINE);
    g_signal_connect(app, "startup", G_CALLBACK(on_startup), nullptr);
    g_signal_connect(app, "command-line", G_CALLBACK(on_command_line), nullptr);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
