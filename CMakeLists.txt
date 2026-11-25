cmake_minimum_required(VERSION 3.20)
project(otto VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WEBKIT REQUIRED IMPORTED_TARGET gtk4 webkitgtk-6.0)

# Pure logic, no GTK widgets: shared by the app and the unit tests.
# (url.cpp uses GLib for URI escaping, which comes in via the same pkg-config target.)
add_library(otto_core STATIC
  src/url.cpp
  src/layout.cpp
)
target_include_directories(otto_core PUBLIC src)
target_link_libraries(otto_core PUBLIC PkgConfig::WEBKIT)
target_compile_options(otto_core PRIVATE -Wall -Wextra -Wpedantic)

# Embed page scripts as C++ string constants; re-configure when they change.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS src/keys.js src/scripts.cpp.in)
file(READ src/keys.js OTTO_KEYS_JS)
configure_file(src/scripts.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/scripts.cpp @ONLY)

# WebKit configuration and page scripts: shared by the app and the benchmarks.
add_library(otto_web STATIC
  src/web_config.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/scripts.cpp
)
target_include_directories(otto_web PUBLIC src)
target_link_libraries(otto_web PUBLIC PkgConfig::WEBKIT)
target_compile_options(otto_web PRIVATE -Wall -Wextra -Wpedantic)

# The browser UI (windows, tabs, panes, shortcuts, theme): the app and the UI benchmarks.
add_library(otto_app STATIC
  src/browser_window.cpp
  src/pane.cpp
  src/shortcuts.cpp
  src/theme.cpp
)
target_link_libraries(otto_app PUBLIC otto_core otto_web)
target_compile_options(otto_app PRIVATE -Wall -Wextra -Wpedantic)

add_executable(otto src/main.cpp)
target_link_libraries(otto PRIVATE otto_app)
target_compile_options(otto PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS otto RUNTIME DESTINATION bin)

# Benchmarks for the page keyboard commands; see bench/otto_bench.cpp.
# bench.js is read at run time, so editing it needs no rebuild.
add_executable(otto_bench bench/otto_bench.cpp bench/ui_bench.cpp)
target_link_libraries(otto_bench PRIVATE otto_app)
# Optional: XTest lets the latency benchmark send real key presses (X11 only).
pkg_check_modules(XTST IMPORTED_TARGET xtst x11)
if(XTST_FOUND)
  target_link_libraries(otto_bench PRIVATE PkgConfig::XTST)
  target_compile_definitions(otto_bench PRIVATE OTTO_BENCH_XTEST=1)
endif()
target_compile_definitions(otto_bench PRIVATE OTTO_BENCH_JS="${CMAKE_CURRENT_SOURCE_DIR}/bench/bench.js")
target_compile_options(otto_bench PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
if(BUILD_TESTING)
  add_executable(unit_tests tests/unit_tests.cpp)
  target_link_libraries(unit_tests PRIVATE otto_core)
  target_compile_options(unit_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME unit_tests COMMAND unit_tests)
endif()
