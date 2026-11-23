#pragma once

#include <gtk/gtk.h>

namespace otto {

// Attach Otto's keyboard shortcuts to `window`.
//
// They run in the capture phase, i.e. before the focused widget sees the key.
// That matters for web views: WebKit hands keys to the page asynchronously and
// only re-emits unhandled ones afterwards, so ordinary accelerators would fire
// late and out of order with keys typed right after them (e.g. Alt+v followed
// by typing an address would type into the old page). The trade-off is that
// pages cannot intercept these particular keys, as in other browsers.
void install_shortcuts(GtkWidget* window);

}  // namespace otto
