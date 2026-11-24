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
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS src/hints.js src/scripts.cpp.in)
file(READ src/hints.js OTTO_HINTS_JS)
configure_file(src/scripts.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/scripts.cpp @ONLY)

add_executable(otto
  src/main.cpp
  src/browser_window.cpp
  src/pane.cpp
  src/shortcuts.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/scripts.cpp
)
target_link_libraries(otto PRIVATE otto_core)
target_compile_options(otto PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS otto RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
  add_executable(unit_tests tests/unit_tests.cpp)
  target_link_libraries(unit_tests PRIVATE otto_core)
  target_compile_options(unit_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME unit_tests COMMAND unit_tests)
endif()
