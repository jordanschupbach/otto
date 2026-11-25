#include "web_config.hpp"

#include "scripts.hpp"

namespace otto::web_config {

// Feature flags are looked up by name because they come and go between WebKit
// versions; a missing one is skipped (with a warning) rather than failing.
bool set_feature(WebKitSettings* settings, const char* identifier, bool enabled) {
    WebKitFeatureList* all = webkit_settings_get_all_features();
    for (gsize i = 0; i < webkit_feature_list_get_length(all); ++i) {
        WebKitFeature* feature = webkit_feature_list_get(all, i);
        if (g_str_equal(webkit_feature_get_identifier(feature), identifier)) {
            webkit_settings_set_feature_enabled(settings, feature, enabled);
            webkit_feature_list_unref(all);
            return true;
        }
    }
    webkit_feature_list_unref(all);
    g_warning("WebKit feature %s not found; leaving it at its default", identifier);
    return false;
}

WebKitSettings* create_settings() {
    WebKitSettings* settings = webkit_settings_new();
    webkit_settings_set_enable_developer_extras(settings, TRUE);  // right-click → Inspect

    // WebKit otherwise renders at ~60 fps even on faster displays (it picks every
    // 2nd/4th vblank on a 120/240 Hz screen), so scrolling looks choppier and
    // each key press waits longer for its frame than it needs to.
    set_feature(settings, "PreferPageRenderingUpdatesNear60FPS", false);
    return settings;
}

void add_page_scripts(WebKitUserContentManager* manager) {
    WebKitUserScript* keys = webkit_user_script_new_for_world(
        scripts::kKeys, WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, kScriptWorld, nullptr, nullptr);
    webkit_user_content_manager_add_script(manager, keys);
    webkit_user_script_unref(keys);
}

void register_message_handler(WebKitUserContentManager* manager) {
    webkit_user_content_manager_register_script_message_handler(manager, kMessageHandler, kScriptWorld);
}

}  // namespace otto::web_config
