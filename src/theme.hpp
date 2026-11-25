#pragma once

namespace otto::theme {

// Light/dark appearance for the whole app: GTK's interface color scheme for the UI, which
// WebKit also reports to pages as `prefers-color-scheme`, so sites that have a
// dark theme switch with it. The choice is saved in
// $XDG_CONFIG_HOME/otto/settings.ini and restored by init().

// Apply the saved preference (dark if none saved yet). Call once at startup.
void init();

bool is_dark();

// Switch appearance and save the choice. Every window follows via GtkSettings'
// notify::gtk-interface-color-scheme.
void set_dark(bool dark);

}  // namespace otto::theme
