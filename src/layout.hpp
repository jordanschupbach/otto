#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace otto {

struct Rect {
    double x = 0, y = 0, width = 0, height = 0;
};

enum class Direction { Left, Down, Up, Right };

// Pick the rect adjacent to rects[from] in `dir`, the way tiling editors and
// window managers do: the nearest rect on that side that shares some of the
// edge; among equally near ones, the one sharing the most of it; among those,
// the one most aligned with rects[from] (top/left edges closest).
// Returns nullopt if there is nothing in that direction.
std::optional<size_t> find_neighbor(const std::vector<Rect>& rects, size_t from, Direction dir);

// True for Left/Right: the direction moves along the x axis.
constexpr bool is_horizontal(Direction dir) {
    return dir == Direction::Left || dir == Direction::Right;
}

// Signed pixel change to a split divider's position when resizing toward
// `dir`, for a split `extent` pixels long on that axis (mep's convention: the
// active pane's divider moves the way the key points). 5% per step, at least
// 10px so small splits still move.
int divider_step(Direction dir, int extent);

}  // namespace otto
