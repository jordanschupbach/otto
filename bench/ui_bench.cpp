// otto_bench --ui: times Otto's own UI operations (src/browser_window.cpp,
// src/pane.cpp) in a real BrowserWindow, triggered through the same window
// actions the keyboard shortcuts use. For each operation:
//
//   action   the synchronous work in Otto/GTK/WebKit when the action runs
//   painted  until GTK has painted the window afterwards (the next frame,
//            so this includes waiting for the frame clock)
//   loaded   for operations that create a view: until its page has loaded,
//            which includes WebKit starting a web process for it
#include "ui_bench.hpp"

#include "browser_window.hpp"
#include "theme.hpp"

#include <gtk/gtk.h>
#include <webkit/webkit.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace otto::bench {
namespace {

constexpr gint64 kWaitTimeoutUs = 10'000'000;

// Content for the tabs being switched between: enough to make painting count.
std::string content_page(int n) {
    std::string html = "data:text/html,<!doctype html><title>tab " + std::to_string(n) + "</title><body>";
    for (int i = 0; i < 300; ++i)
        html += "<p>Paragraph " + std::to_string(i) + " with <a href='%23" + std::to_string(i) +
                "'>a link</a> and some text to lay out.</p>";
    return html + "</body>";
}

// Spin the main loop until `done` (or the timeout); false on timeout.
bool wait_until(const std::function<bool()>& done, gint64 timeout_us = kWaitTimeoutUs) {
    bool expired = false;
    guint timer = g_timeout_add_once(timeout_us / 1000, [](gpointer flag) { *static_cast<bool*>(flag) = true; },
                                     &expired);
    while (!done() && !expired) g_main_context_iteration(nullptr, TRUE);
    if (!expired) g_source_remove(timer);
    return !expired;
}

void wait_ms(int ms) {
    gint64 end = g_get_monotonic_time() + ms * 1000;
    wait_until([&] { return g_get_monotonic_time() >= end; });
}

void collect_views(GtkWidget* widget, std::vector<WebKitWebView*>& out) {
    if (WEBKIT_IS_WEB_VIEW(widget)) out.push_back(WEBKIT_WEB_VIEW(widget));
    for (GtkWidget* c = gtk_widget_get_first_child(widget); c; c = gtk_widget_get_next_sibling(c))
        collect_views(c, out);
}

// Includes views in background notebook pages: they're in the widget tree too.
std::vector<WebKitWebView*> all_views(GtkWindow* window) {
    std::vector<WebKitWebView*> views;
    collect_views(GTK_WIDGET(window), views);
    return views;
}

class Runner {
public:
    explicit Runner(GtkWindow* window) : window_(window) {
        clock_ = gtk_widget_get_frame_clock(GTK_WIDGET(window));
        paint_handler_ = g_signal_connect_swapped(clock_, "after-paint",
                                                  G_CALLBACK(+[](Runner* r) { ++r->paints_; }), this);
    }
    ~Runner() { g_signal_handler_disconnect(clock_, paint_handler_); }

    void activate(const char* action) { g_action_group_activate_action(G_ACTION_GROUP(window_), action, nullptr); }

    // Run `action`, recording its timings under `name` (unless `record` is false,
    // for setup/cleanup steps). Waits for a new view's page if one is created.
    void run(const std::string& name, const char* action, bool record = true) {
        std::vector<WebKitWebView*> before = all_views(window_);
        std::set<WebKitWebView*> old(before.begin(), before.end());
        unsigned paints = paints_;

        gint64 t0 = g_get_monotonic_time();
        activate(action);
        gint64 t_action = g_get_monotonic_time();

        WebKitWebView* fresh = nullptr;
        for (WebKitWebView* v : all_views(window_))
            if (!old.count(v)) fresh = v;
        bool loaded = false;
        gulong load_handler = 0;
        if (fresh)
            load_handler = g_signal_connect(fresh, "load-changed",
                                            G_CALLBACK(+[](WebKitWebView*, WebKitLoadEvent e, bool* done) {
                                                if (e == WEBKIT_LOAD_FINISHED) *done = true;
                                            }),
                                            &loaded);

        bool ok = wait_until([&] { return paints_ != paints; });
        gint64 t_paint = g_get_monotonic_time();
        if (fresh) ok = wait_until([&] { return loaded; }) && ok;
        gint64 t_loaded = g_get_monotonic_time();
        if (fresh) g_signal_handler_disconnect(fresh, load_handler);

        if (!ok) {
            std::fprintf(stderr, "otto_bench: %s timed out\n", name.c_str());
            failed_ = true;
        }
        if (!record) return;
        samples_[name + ": action"].push_back((t_action - t0) / 1000.0);
        samples_[name + ": painted"].push_back((t_paint - t0) / 1000.0);
        if (fresh) samples_[name + ": loaded"].push_back((t_loaded - t0) / 1000.0);
        order_.push_back(name);
    }

    // Let follow-up work (paints, process teardown) finish between samples.
    void settle() { wait_ms(80); }

    void print() const {
        std::printf("\n== UI operations\n%-28s %10s %10s %10s\n", "scenario", "median ms", "p95 ms", "max ms");
        std::set<std::string> done;
        for (const std::string& name : order_) {
            if (!done.insert(name).second) continue;
            for (const char* part : {": action", ": painted", ": loaded"}) {
                auto it = samples_.find(name + part);
                if (it == samples_.end()) continue;
                std::vector<double> v = it->second;
                std::sort(v.begin(), v.end());
                auto pick = [&](double q) { return v[std::min(v.size() - 1, static_cast<size_t>(q * v.size()))]; };
                std::printf("%-28s %10.2f %10.2f %10.2f\n", it->first.c_str(), pick(0.5), pick(0.95), v.back());
            }
        }
        std::fflush(stdout);
    }

    bool failed() const { return failed_; }

private:
    GtkWindow* window_;
    GdkFrameClock* clock_;
    gulong paint_handler_ = 0;
    unsigned paints_ = 0;
    bool failed_ = false;
    std::map<std::string, std::vector<double>> samples_;
    std::vector<std::string> order_;
};

struct Options {
    int iterations;
    int status = 0;
};

void run_suite(GtkApplication* app, Options* opts) {
    theme::init();
    std::vector<std::string> pages;
    for (int i = 0; i < 4; ++i) pages.push_back(content_page(i));
    (new BrowserWindow(app, pages))->present();
    GtkWindow* window = gtk_application_get_active_window(app);
    if (!window) window = GTK_WINDOW(gtk_application_get_windows(app)->data);

    // Wait for the starting tabs to load.
    if (!wait_until([&] {
            auto views = all_views(window);
            return views.size() == pages.size() &&
                   std::none_of(views.begin(), views.end(), webkit_web_view_is_loading);
        })) {
        std::fputs("otto_bench: starting tabs didn't load\n", stderr);
        opts->status = 1;
        gtk_window_destroy(window);
        return;
    }
    wait_ms(300);

    {
        Runner r(window);
        for (int i = 0; i < opts->iterations; ++i) {
            r.run("new tab", "new-tab");
            r.settle();
            r.run("close tab", "close-tab");
            r.settle();
            r.run("split pane", "split-vertical");
            r.settle();
            r.run("close pane", "close-pane");
            r.settle();
            r.run("switch tab", "next-tab");
            r.settle();
        }
        r.print();
        if (r.failed()) opts->status = 1;
    }
    gtk_window_destroy(window);
}

}  // namespace

int run_ui_bench(int iterations) {
    // Non-unique: never hand off to (or receive from) a running Otto.
    GtkApplication* app = gtk_application_new(nullptr, G_APPLICATION_NON_UNIQUE);
    Options opts{iterations};
    g_signal_connect(app, "activate", G_CALLBACK(+[](GtkApplication* a, Options* o) { run_suite(a, o); }), &opts);
    g_application_run(G_APPLICATION(app), 0, nullptr);
    g_object_unref(app);
    return opts.status;
}

}  // namespace otto::bench
