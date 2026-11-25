#pragma once

namespace otto::bench {

// otto_bench --ui: time tab and pane operations in a real Otto window.
// Returns the process exit status.
int run_ui_bench(int iterations);

}  // namespace otto::bench
