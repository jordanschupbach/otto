#include "layout.hpp"

#include <algorithm>
#include <cmath>

namespace otto {

std::optional<size_t> find_neighbor(const std::vector<Rect>& rects, size_t from, Direction dir) {
    // Neighbouring panes are separated by a split handle and borders, so
    // "touching" edges can be a few pixels apart (or overlap slightly).
    constexpr double kTolerance = 8.0;

    const Rect& a = rects.at(from);
    const bool horizontal = is_horizontal(dir);

    std::optional<size_t> best;
    double best_gap = 0, best_overlap = 0, best_offset = 0;

    for (size_t i = 0; i < rects.size(); ++i) {
        if (i == from) continue;
        const Rect& b = rects[i];

        double gap = 0;
        switch (dir) {
            case Direction::Left:  gap = a.x - (b.x + b.width); break;
            case Direction::Right: gap = b.x - (a.x + a.width); break;
            case Direction::Up:    gap = a.y - (b.y + b.height); break;
            case Direction::Down:  gap = b.y - (a.y + a.height); break;
        }
        if (gap < -kTolerance) continue;  // not on that side of us

        double overlap = horizontal
            ? std::min(a.y + a.height, b.y + b.height) - std::max(a.y, b.y)
            : std::min(a.x + a.width, b.x + b.width) - std::max(a.x, b.x);
        if (overlap <= kTolerance) continue;  // diagonal, or merely touching a corner

        // How far b's start is from ours along the shared edge; used to prefer
        // the neighbour aligned with us (e.g. the top one when moving right
        // from a tall pane next to two stacked ones).
        double offset = horizontal ? std::abs(b.y - a.y) : std::abs(b.x - a.x);

        bool better = !best || gap < best_gap - kTolerance;
        if (!better && std::abs(gap - best_gap) <= kTolerance) {
            if (overlap > best_overlap + kTolerance)
                better = true;
            else if (std::abs(overlap - best_overlap) <= kTolerance)
                better = offset < best_offset;
        }
        if (better) {
            best = i;
            best_gap = gap;
            best_overlap = overlap;
            best_offset = offset;
        }
    }
    return best;
}

int divider_step(Direction dir, int extent) {
    int step = std::max(10, extent / 20);
    return (dir == Direction::Right || dir == Direction::Down) ? step : -step;
}

}  // namespace otto
