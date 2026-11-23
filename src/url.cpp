#include "url.hpp"

#include <glib.h>

#include <algorithm>
#include <cctype>

namespace otto {
namespace {

constexpr std::string_view kSearchPrefix = "https://duckduckgo.com/?q=";

std::string_view trim(std::string_view s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
    return s;
}

bool has_scheme(std::string_view s) {
    // RFC 3986: scheme = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ) ":"
    auto colon = s.find(':');
    if (colon == std::string_view::npos || colon == 0) return false;
    if (!std::isalpha(static_cast<unsigned char>(s[0]))) return false;
    for (size_t i = 1; i < colon; ++i) {
        unsigned char c = s[i];
        if (!std::isalnum(c) && c != '+' && c != '-' && c != '.') return false;
    }
    std::string_view rest = s.substr(colon + 1);
    // "localhost:8080/x" parses as scheme "localhost"; if what follows the colon
    // is a port number (digits, then end or a path/query/fragment), it's a host.
    std::string_view port = rest.substr(0, rest.find_first_of("/?#"));
    bool rest_is_port = !port.empty() &&
        std::all_of(port.begin(), port.end(), [](unsigned char c) { return std::isdigit(c); });
    return !rest_is_port;
}

bool looks_like_host(std::string_view s) {
    if (s.find(' ') != std::string_view::npos) return false;
    std::string_view host = s.substr(0, s.find_first_of("/?#"));
    host = host.substr(0, host.find(':'));
    return host == "localhost" || host.find('.') != std::string_view::npos;
}

}  // namespace

std::string normalize_input(std::string_view input) {
    std::string_view s = trim(input);
    if (s.empty()) return "about:blank";
    if (has_scheme(s)) return std::string(s);
    if (looks_like_host(s)) return "https://" + std::string(s);

    g_autofree char* escaped = g_uri_escape_string(std::string(s).c_str(), nullptr, TRUE);
    return std::string(kSearchPrefix) + escaped;
}

}  // namespace otto
