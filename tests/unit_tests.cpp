// Minimal dependency-free unit tests for the pure logic (no GTK main loop).
#include "layout.hpp"
#include "url.hpp"

#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace {

int failures = 0;

template <typename A, typename B>
void check_eq(const A& actual, const B& expected, const char* expr, int line) {
    if (!(actual == expected)) {
        ++failures;
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expr);
    }
}
#define CHECK_EQ(a, b) check_eq((a), (b), #a " == " #b, __LINE__)

using otto::Direction;
using otto::find_neighbor;
using otto::Rect;
constexpr std::optional<size_t> none = std::nullopt;

void test_normalize_input() {
    using otto::normalize_input;
    CHECK_EQ(normalize_input("https://example.com"), std::string("https://example.com"));
    CHECK_EQ(normalize_input("  example.com  "), std::string("https://example.com"));
    CHECK_EQ(normalize_input("localhost:8080/x"), std::string("https://localhost:8080/x"));
    CHECK_EQ(normalize_input("about:blank"), std::string("about:blank"));
    CHECK_EQ(normalize_input("file:///tmp/a.html"), std::string("file:///tmp/a.html"));
    CHECK_EQ(normalize_input(""), std::string("about:blank"));
    CHECK_EQ(normalize_input("nix flakes"), std::string("https://duckduckgo.com/?q=nix%20flakes"));
}

void test_find_neighbor_side_by_side() {
    // [0][1]
    std::vector<Rect> r = {{0, 0, 100, 100}, {101, 0, 100, 100}};
    CHECK_EQ(find_neighbor(r, 0, Direction::Right), std::optional<size_t>(1));
    CHECK_EQ(find_neighbor(r, 1, Direction::Left), std::optional<size_t>(0));
    CHECK_EQ(find_neighbor(r, 0, Direction::Left), none);
    CHECK_EQ(find_neighbor(r, 0, Direction::Up), none);
    CHECK_EQ(find_neighbor(r, 0, Direction::Down), none);
}

void test_find_neighbor_nested() {
    // +-----+-----+
    // |  0  |  1  |
    // |     +-----+
    // |     |  2  |
    // +-----+--+--+
    // |   3    | 4|
    // +--------+--+
    std::vector<Rect> r = {
        {0, 0, 100, 200},  {101, 0, 100, 99}, {101, 100, 100, 100},
        {0, 201, 150, 50}, {151, 201, 50, 50},
    };
    // 1 and 2 share (about) equal edge with 0; prefer the one aligned with 0's top.
    CHECK_EQ(find_neighbor(r, 0, Direction::Right), std::optional<size_t>(1));
    CHECK_EQ(find_neighbor(r, 2, Direction::Up), std::optional<size_t>(1));
    CHECK_EQ(find_neighbor(r, 1, Direction::Down), std::optional<size_t>(2));
    CHECK_EQ(find_neighbor(r, 2, Direction::Left), std::optional<size_t>(0));
    CHECK_EQ(find_neighbor(r, 0, Direction::Down), std::optional<size_t>(3));
    // 2 sits above both 3 (x 101..150) and 4 (x 151..201); 4 shares more edge.
    CHECK_EQ(find_neighbor(r, 2, Direction::Down), std::optional<size_t>(4));
    CHECK_EQ(find_neighbor(r, 4, Direction::Left), std::optional<size_t>(3));
    CHECK_EQ(find_neighbor(r, 3, Direction::Up), std::optional<size_t>(0));
    // Diagonal neighbours are not reachable in one step.
    CHECK_EQ(find_neighbor(r, 1, Direction::Left), std::optional<size_t>(0));
    CHECK_EQ(find_neighbor(r, 4, Direction::Up), std::optional<size_t>(2));
}

void test_divider_step() {
    using otto::divider_step;
    CHECK_EQ(divider_step(Direction::Right, 1000), 50);
    CHECK_EQ(divider_step(Direction::Down, 1000), 50);
    CHECK_EQ(divider_step(Direction::Left, 1000), -50);
    CHECK_EQ(divider_step(Direction::Up, 1000), -50);
    CHECK_EQ(divider_step(Direction::Right, 100), 10);  // minimum step
    static_assert(otto::is_horizontal(Direction::Left) && !otto::is_horizontal(Direction::Up));
}

}  // namespace

int main() {
    test_normalize_input();
    test_find_neighbor_side_by_side();
    test_find_neighbor_nested();
    test_divider_step();
    if (failures == 0) std::puts("all tests passed");
    return failures == 0 ? 0 : 1;
}
