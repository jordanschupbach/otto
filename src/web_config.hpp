#pragma once

#include <webkit/webkit.h>

// WebKit configuration shared by every Otto view, and by the benchmark
// harness (bench/), so benchmarks measure exactly what the browser runs.
namespace otto::web_config {

// The JavaScript world Otto's page scripts run in; see add_page_scripts().
inline constexpr const char* kScriptWorld = "otto";
// Name of the message handler page scripts post to (in kScriptWorld only).
inline constexpr const char* kMessageHandler = "otto";

// New settings object for Otto's views.
WebKitSettings* create_settings();

// Turn a WebKit feature flag (webkit_feature_get_identifier) on or off. Returns
// false, with a warning, if this WebKit has no such feature.
bool set_feature(WebKitSettings* settings, const char* identifier, bool enabled);

// Inject Otto's page scripts (src/keys.js) into `manager`. They run in their own
// world: they share the page's DOM but not its globals, so only they can reach
// the message handler.
void add_page_scripts(WebKitUserContentManager* manager);

// Register the handler page scripts post to (signal
// "script-message-received::otto"). Once per manager.
void register_message_handler(WebKitUserContentManager* manager);

}  // namespace otto::web_config
