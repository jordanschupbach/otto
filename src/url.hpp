#pragma once

#include <string>
#include <string_view>

namespace otto {

// Turn whatever the user typed in the address bar into a loadable URI.
// - Anything with a scheme ("https://", "file:", "about:") is used as-is.
// - Something that looks like a host ("example.com", "localhost:8080") gets "https://".
// - Everything else becomes a web search.
std::string normalize_input(std::string_view input);

}  // namespace otto
