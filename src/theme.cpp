#include "theme.hpp"

#include <gtk/gtk.h>

#include <cerrno>

namespace otto::theme {
namespace {

constexpr const char* kGroup = "appearance";
constexpr const char* kKey = "dark";

char* settings_path() {
    return g_build_filename(g_get_user_config_dir(), "otto", "settings.ini", nullptr);
}

GtkSettings* gtk_settings() { return gtk_settings_get_default(); }

void apply(bool dark) {
    g_object_set(gtk_settings(), "gtk-interface-color-scheme",
                 dark ? GTK_INTERFACE_COLOR_SCHEME_DARK : GTK_INTERFACE_COLOR_SCHEME_LIGHT, nullptr);
}

void save(bool dark) {
    g_autofree char* path = settings_path();
    g_autoptr(GKeyFile) file = g_key_file_new();
    // Keep any other settings that may be in the file.
    g_key_file_load_from_file(file, path, G_KEY_FILE_KEEP_COMMENTS, nullptr);
    g_key_file_set_boolean(file, kGroup, kKey, dark);

    g_autofree char* dir = g_path_get_dirname(path);
    g_autoptr(GError) error = nullptr;
    if (g_mkdir_with_parents(dir, 0700) != 0 || !g_key_file_save_to_file(file, path, &error))
        g_warning("Could not save appearance to %s: %s", path, error ? error->message : g_strerror(errno));
}

}  // namespace

void init() {
    constexpr bool kDefaultDark = true;  // until the user picks with the header-bar button

    g_autofree char* path = settings_path();
    g_autoptr(GKeyFile) file = g_key_file_new();
    g_autoptr(GError) error = nullptr;
    bool dark = kDefaultDark;
    if (g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, nullptr)) {
        gboolean saved = g_key_file_get_boolean(file, kGroup, kKey, &error);
        if (!error) dark = saved;
    }
    apply(dark);
}

bool is_dark() {
    GtkInterfaceColorScheme scheme = GTK_INTERFACE_COLOR_SCHEME_DEFAULT;
    g_object_get(gtk_settings(), "gtk-interface-color-scheme", &scheme, nullptr);
    return scheme == GTK_INTERFACE_COLOR_SCHEME_DARK;
}

void set_dark(bool dark) {
    apply(dark);
    save(dark);
}

}  // namespace otto::theme
