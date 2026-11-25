#pragma once

namespace otto::scripts {

// Page scripts, embedded at build time from src/*.js (see CMakeLists.txt).
extern const char* const kKeys;  // src/keys.js: page keyboard commands (f/F hints, j/k scroll)

}  // namespace otto::scripts
