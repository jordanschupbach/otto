// otto_bench: benchmarks for Otto's page keyboard commands (src/keys.js), run in
// a real WebKit view configured exactly like the browser's (src/web_config.cpp).
//
//   otto_bench                   synthetic pages (see bench/bench.js)
//   otto_bench URL...            real sites, measured after they load
//   otto_bench --suite URL...    both
//   otto_bench --latency         key press → rendered scroll, with real key presses
//   otto_bench --ui              tab and pane operations in a real Otto window
//
//   --iterations N               samples per scenario (default 30)
//   --feature Name=0|1           override a WebKit feature flag (to try settings out)
//
// Needs a display: WebKit only lays out pages that are on screen. --latency also
// needs X11 with XTest, and keyboard focus on the bench window: it stops if the
// window loses focus, so key presses can't land in another app.
#include "web_config.hpp"
#include "ui_bench.hpp"

#include <gtk/gtk.h>
#include <webkit/webkit.h>

#ifdef OTTO_BENCH_XTEST
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <gdk/x11/gdkx.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifndef OTTO_BENCH_JS
#error "OTTO_BENCH_JS must name bench/bench.js"
#endif

namespace {

constexpr const char* kBenchWorld = "otto-bench";
constexpr guint kJobTimeoutSeconds = 120;
constexpr gint64 kLatencyMissUs = 500'000;  // no scroll after this long counts as a miss

// Synthetic pages for --latency (names from bench.js; "+busy" adds main-thread load).
const char* const kLatencyPages[] = {
    "article (3k elements, page scrolls)",
    "feed (40k elements, page scrolls)",
    "feed (40k elements, page scrolls)+busy",
};

struct Job {
    std::string mode;  // "suite", "page" or "latency"
    std::string arg;   // URL for "page", page name for "latency"
};

struct Latency {
    std::vector<double> samples;  // ms
    int misses = 0;
    int presses = 0;
    gint64 pressed_at = 0;  // monotonic µs; 0 when not waiting for a scroll
    guint timer = 0;
};

struct State {
    GtkWindow* window = nullptr;
    WebKitWebView* view = nullptr;
    WebKitUserContentManager* manager = nullptr;
    std::string bench_js;
    int iterations = 30;
    std::vector<std::string> features;  // "Name=0" or "Name=1", applied after Otto's own
    std::vector<Job> jobs;
    size_t next = 0;
    guint job_timeout = 0;
    bool failed = false;
    bool done = false;
    std::string last_page;
    Latency latency;
};

struct Stats {
    double median = 0, p95 = 0, max = 0;
};

Stats summarize(std::vector<double> v) {
    if (v.empty()) return {};
    std::sort(v.begin(), v.end());
    auto pick = [&](double q) { return v[std::min(v.size() - 1, static_cast<size_t>(q * v.size()))]; };
    return {pick(0.5), pick(0.95), v.back()};
}

void print_row(State* s, const std::string& page, const std::string& scenario, Stats st) {
    if (page != s->last_page) {
        std::printf("\n== %s\n%-28s %10s %10s %10s\n", page.c_str(), "scenario", "median ms", "p95 ms",
                    "max ms");
        s->last_page = page;
    }
    std::printf("%-28s %10.2f %10.2f %10.2f\n", scenario.c_str(), st.median, st.p95, st.max);
    std::fflush(stdout);
}

void start_next(State* s);

void finish_job(State* s) {
    if (s->job_timeout) g_source_remove(s->job_timeout);
    s->job_timeout = 0;
    if (s->latency.timer) g_source_remove(s->latency.timer);
    s->latency = {};
    ++s->next;
    if (s->next < s->jobs.size())
        start_next(s);
    else
        s->done = true;
}

void fail_job(State* s, const char* why) {
    std::fprintf(stderr, "otto_bench: %s\n", why);
    s->failed = true;
    finish_job(s);
}

// --- latency ---------------------------------------------------------------------

#ifdef OTTO_BENCH_XTEST
void schedule_press(State* s);

void finish_latency(State* s) {
    const Job& job = s->jobs[s->next];
    print_row(s, "latency: " + job.arg, "key → scroll rendered", summarize(s->latency.samples));
    if (s->latency.misses)
        std::printf("  (%d of %d presses didn't scroll within %lld ms)\n", s->latency.misses,
                    s->latency.presses, static_cast<long long>(kLatencyMissUs / 1000));
    finish_job(s);
}

// One real key press (j and k alternately, so the page stays mid-scroll).
void press(State* s) {
    s->latency.timer = 0;
    if (!gtk_window_is_active(s->window))
        return fail_job(s, "bench window lost keyboard focus; stopping so keys don't go elsewhere");

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS  // GTK deprecates its X11 API, but XTest needs it
    Display* dpy = gdk_x11_display_get_xdisplay(gtk_widget_get_display(GTK_WIDGET(s->window)));
    G_GNUC_END_IGNORE_DEPRECATIONS
    KeyCode code = XKeysymToKeycode(dpy, s->latency.presses % 2 ? XK_k : XK_j);
    ++s->latency.presses;
    s->latency.pressed_at = g_get_monotonic_time();
    XTestFakeKeyEvent(dpy, code, True, CurrentTime);
    XTestFakeKeyEvent(dpy, code, False, CurrentTime);
    XFlush(dpy);

    s->latency.timer = g_timeout_add_once(kLatencyMissUs / 1000, [](gpointer data) {
        auto* st = static_cast<State*>(data);
        st->latency.timer = 0;
        st->latency.pressed_at = 0;
        ++st->latency.misses;
        schedule_press(st);
    }, s);
}

// Random gaps, so presses don't fall into step with the frame timer.
void schedule_press(State* s) {
    if (static_cast<int>(s->latency.samples.size()) + s->latency.misses >= s->iterations)
        return finish_latency(s);
    s->latency.timer = g_timeout_add_once(g_random_int_range(60, 140),
                                          [](gpointer data) { press(static_cast<State*>(data)); }, s);
}

void on_scrolled(State* s) {
    if (!s->latency.pressed_at) return;  // a second scroll event for the same press
    s->latency.samples.push_back((g_get_monotonic_time() - s->latency.pressed_at) / 1000.0);
    s->latency.pressed_at = 0;
    if (s->latency.timer) g_source_remove(s->latency.timer);
    s->latency.timer = 0;
    schedule_press(s);
}

void start_latency(State* s) {
    gtk_window_present(s->window);
    gtk_widget_grab_focus(GTK_WIDGET(s->view));
    s->latency.timer = g_timeout_add_once(500, [](gpointer data) {  // let focus settle
        auto* st = static_cast<State*>(data);
        st->latency.timer = 0;
        schedule_press(st);
    }, s);
}
#endif

// --- messages from bench.js --------------------------------------------------------

void on_message(WebKitUserContentManager*, JSCValue* msg, State* s) {
    auto str = [&](const char* name) {
        g_autoptr(JSCValue) v = jsc_value_object_get_property(msg, name);
        g_autofree char* text = jsc_value_to_string(v);
        return std::string(text ? text : "");
    };
    auto num = [&](const char* name) {
        g_autoptr(JSCValue) v = jsc_value_object_get_property(msg, name);
        return jsc_value_to_double(v);
    };

    std::string type = str("type");
    if (type == "result") {
        print_row(s, str("page"), str("scenario"), {num("median"), num("p95"), num("max")});
    } else if (type == "done") {
        finish_job(s);
    } else if (type == "error") {
        std::fprintf(stderr, "bench error: %s\n", str("message").c_str());
        s->failed = true;
        finish_job(s);
#ifdef OTTO_BENCH_XTEST
    } else if (type == "ready") {
        start_latency(s);
    } else if (type == "scrolled") {
        on_scrolled(s);
#endif
    }
}

// Each job gets a fresh copy of the driver script carrying its configuration.
void start_next(State* s) {
    const Job& job = s->jobs[s->next];
    webkit_user_content_manager_remove_all_scripts(s->manager);
    otto::web_config::add_page_scripts(s->manager);  // keys.js, as in Otto

    std::string source = "const OTTO_BENCH = { mode: \"" + job.mode + "\", iterations: " +
                         std::to_string(s->iterations) + ", page: \"" + job.arg + "\" };\n" + s->bench_js;
    WebKitUserScript* driver = webkit_user_script_new_for_world(
        source.c_str(), WEBKIT_USER_CONTENT_INJECT_TOP_FRAME, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END,
        kBenchWorld, nullptr, nullptr);
    webkit_user_content_manager_add_script(s->manager, driver);
    webkit_user_script_unref(driver);

    s->job_timeout = g_timeout_add_seconds_once(kJobTimeoutSeconds, [](gpointer data) {
        auto* st = static_cast<State*>(data);
        st->job_timeout = 0;
        fail_job(st, "timed out");
    }, s);

    if (job.mode == "page")
        webkit_web_view_load_uri(s->view, job.arg.c_str());
    else
        webkit_web_view_load_html(s->view, "<!doctype html><title>otto bench</title><body></body>",
                                  "file:///otto-bench/");
}

}  // namespace

int main(int argc, char** argv) {
    State s;
    bool suite = false, latency = false, ui = false;
    std::vector<Job> pages;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--suite") {
            suite = true;
        } else if (arg == "--latency") {
            latency = true;
        } else if (arg == "--ui") {
            ui = true;
        } else if (arg == "--iterations" && i + 1 < argc) {
            s.iterations = std::max(5, std::atoi(argv[++i]));
        } else if (arg == "--feature" && i + 1 < argc) {
            s.features.push_back(argv[++i]);
        } else if (arg == "-h" || arg == "--help") {
            std::puts("usage: otto_bench [--suite] [--latency] [--ui] [--iterations N] [--feature Name=0|1]... [URL...]");
            return 0;
        } else {
            pages.push_back({"page", arg});
        }
    }
    if (ui) return otto::bench::run_ui_bench(s.iterations);  // its own app and window
    if (suite || (pages.empty() && !latency)) s.jobs.push_back({"suite", ""});
    if (latency) {
#ifdef OTTO_BENCH_XTEST
        for (const char* page : kLatencyPages) s.jobs.push_back({"latency", page});
#else
        std::fputs("otto_bench: built without XTest (libxtst), so --latency is unavailable\n", stderr);
        return 2;
#endif
    }
    s.jobs.insert(s.jobs.end(), pages.begin(), pages.end());

    g_autofree char* js = nullptr;
    g_autoptr(GError) error = nullptr;
    if (!g_file_get_contents(OTTO_BENCH_JS, &js, nullptr, &error)) {
        std::fprintf(stderr, "otto_bench: %s\n", error->message);
        return 2;
    }
    s.bench_js = js;

    gtk_init();
#ifdef OTTO_BENCH_XTEST
    if (latency && !GDK_IS_X11_DISPLAY(gdk_display_get_default())) {
        std::fputs("otto_bench: --latency needs an X11 display\n", stderr);
        return 2;
    }
#endif

    s.manager = webkit_user_content_manager_new();
    otto::web_config::register_message_handler(s.manager);
    webkit_user_content_manager_register_script_message_handler(s.manager, "bench", kBenchWorld);
    g_signal_connect(s.manager, "script-message-received::bench", G_CALLBACK(on_message), &s);

    WebKitSettings* settings = otto::web_config::create_settings();
    for (const std::string& f : s.features) {
        auto eq = f.find('=');
        std::string name = f.substr(0, eq);
        bool on = eq == std::string::npos || f.substr(eq + 1) != "0";
        if (!otto::web_config::set_feature(settings, name.c_str(), on)) return 2;
        std::printf("feature %s=%d\n", name.c_str(), on);
    }
    s.view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW, "settings", settings,
                                          "user-content-manager", s.manager, nullptr));
    g_object_unref(settings);

    s.window = GTK_WINDOW(gtk_window_new());
    gtk_window_set_title(s.window, "otto bench");
    gtk_window_set_default_size(s.window, 1280, 800);
    gtk_window_set_child(s.window, GTK_WIDGET(s.view));
    gtk_window_present(s.window);

    start_next(&s);
    while (!s.done) g_main_context_iteration(nullptr, TRUE);

    gtk_window_destroy(s.window);
    g_object_unref(s.manager);
    return s.failed ? 1 : 0;
}
