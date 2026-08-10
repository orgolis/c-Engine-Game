// ============================================================================
// assetwatch_check — hot reload has to be trustworthy, not just present.
//
// The behaviours worth defending are mostly NEGATIVE, which is why they were
// easy to get wrong in the three ad-hoc poll loops this replaces:
//   - starting to watch a file must not look like an edit
//   - a file still being written must NOT fire (the flakiness everyone blames
//     on "hot reload is unreliable")
//   - a long write must fire ONCE at the end, not once per poll
//   - a handler that throws must not stop the other reloads
//
// Time is injected rather than slept. A settle test built on sleeps would take
// seconds and still be flaky on a loaded CI runner; passing the clock in makes
// the same test instant and exact.
// ============================================================================
#include "assets/asset_watcher.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using gws::assets::AssetWatcher;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

fs::path g_dir;

std::string write_file(const std::string& name, const std::string& body) {
    const auto p = g_dir / name;
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << body;
    f.close();
    return p.string();
}

// Force a distinct mtime. Filesystem timestamp granularity is coarse enough on
// some Windows configurations that two writes in the same millisecond compare
// equal — the watcher also compares SIZE, but a test that relied on that would
// be testing the wrong thing.
void touch_distinct(const std::string& path, const std::string& body) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << body;
    f.close();
    auto t = fs::last_write_time(path);
    fs::last_write_time(path, t + std::chrono::seconds(1));
}

}  // namespace

int main() {
    std::printf("=== assetwatch_check ===\n\n");

    g_dir = fs::temp_directory_path() / "gws_assetwatch_check";
    std::error_code ec;
    fs::remove_all(g_dir, ec);
    fs::create_directories(g_dir, ec);

    // ---- 1. watching is not an edit ---------------------------------------
    {
        AssetWatcher w;
        w.set_poll_interval(0.0);
        w.set_settle_time(0.1);
        int hits = 0;
        const auto p = write_file("a.txt", "one");
        w.watch(p, [&](const std::string&) { ++hits; });

        check(w.watched_count() == 1, "watch() registers the file");
        w.poll(1.0);
        w.poll(2.0);
        check(hits == 0, "starting to watch an existing file fires nothing");
    }

    // ---- 2. a settled edit fires exactly once ------------------------------
    {
        AssetWatcher w;
        w.set_poll_interval(0.0);
        w.set_settle_time(0.2);
        int hits = 0;
        std::string seen;
        const auto p = write_file("b.txt", "one");
        w.watch(p, [&](const std::string& path) { ++hits; seen = path; });

        touch_distinct(p, "two");
        w.poll(10.0);
        check(hits == 0, "change is not reported before it settles");
        check(w.pending_count() == 1, "a settling file is visible as pending");

        w.poll(10.1);
        check(hits == 0, "still quiet inside the settle window");

        w.poll(10.3);
        check(hits == 1, "fires once the file has settled");
        check(seen == p, "callback receives the watched path");
        check(w.pending_count() == 0, "pending clears after firing");

        w.poll(11.0);
        w.poll(12.0);
        check(hits == 1, "a settled file does not fire again while unchanged");
    }

    // ---- 3. THE ONE THAT MATTERS: a file still being written stays quiet ----
    // Simulates an exporter appending over several polls. Firing here is how
    // half-written assets get loaded and hot reload earns its reputation.
    {
        AssetWatcher w;
        w.set_poll_interval(0.0);
        w.set_settle_time(0.2);
        int hits = 0;
        const auto p = write_file("c.bin", "");
        w.watch(p, [&](const std::string&) { ++hits; });

        double t = 20.0;
        for (int i = 0; i < 5; ++i) {          // still growing every poll
            touch_distinct(p, std::string(static_cast<size_t>(64 * (i + 1)), 'x'));
            t += 0.15;
            w.poll(t);
        }
        check(hits == 0, "a file that keeps changing never fires mid-write");

        t += 0.25;                              // writer stopped
        w.poll(t);
        check(hits == 1, "fires exactly once after the write finishes");
    }

    // ---- 4. a file that appears later counts as a change -------------------
    // "Export into the project folder" is the common way assets arrive.
    {
        AssetWatcher w;
        w.set_poll_interval(0.0);
        w.set_settle_time(0.1);
        int hits = 0;
        const auto p = (g_dir / "later.txt").string();
        w.watch(p, [&](const std::string&) { ++hits; });

        w.poll(30.0);
        check(hits == 0, "a missing file is quiet");

        write_file("later.txt", "arrived");
        w.poll(31.0);
        w.poll(31.2);
        check(hits == 1, "a file appearing later fires as a change");
    }

    // ---- 5. deletion is not a reload --------------------------------------
    // Deleting is usually the middle of a save, and there is nothing to load.
    {
        AssetWatcher w;
        w.set_poll_interval(0.0);
        w.set_settle_time(0.1);
        int hits = 0;
        const auto p = write_file("gone.txt", "here");
        w.watch(p, [&](const std::string&) { ++hits; });

        fs::remove(p, ec);
        w.poll(40.0);
        w.poll(40.5);
        check(hits == 0, "deleting a watched file does not fire a reload");

        write_file("gone.txt", "back");        // restored -> that IS a change
        w.poll(41.0);
        w.poll(41.5);
        check(hits == 1, "the file coming back fires once");
    }

    // ---- 6. poll interval actually throttles -------------------------------
    {
        AssetWatcher w;
        w.set_poll_interval(1.0);
        w.set_settle_time(0.0);
        int hits = 0;
        const auto p = write_file("d.txt", "one");
        w.watch(p, [&](const std::string&) { ++hits; });

        touch_distinct(p, "two");
        w.poll(50.0);                          // first poll establishes the clock
        const int after_first = hits;
        touch_distinct(p, "three");
        w.poll(50.1);                          // inside the interval -> skipped
        check(hits == after_first, "poll() inside the interval does no work");
        w.poll(51.5);
        check(hits > after_first, "poll() past the interval runs again");
    }

    // ---- 7. one bad handler does not stop the others -----------------------
    {
        AssetWatcher w;
        w.set_poll_interval(0.0);
        w.set_settle_time(0.1);
        int good = 0;
        const auto bad  = write_file("bad.txt", "x");
        const auto good_p = write_file("good.txt", "x");
        w.watch(bad,    [](const std::string&) { throw std::runtime_error("boom"); });
        w.watch(good_p, [&](const std::string&) { ++good; });

        touch_distinct(bad, "y");
        touch_distinct(good_p, "y");
        w.poll(60.0);
        w.poll(60.5);
        check(good == 1, "a throwing reload handler does not block other reloads");
    }

    // ---- 8. unwatch ---------------------------------------------------------
    {
        AssetWatcher w;
        w.set_poll_interval(0.0);
        w.set_settle_time(0.1);
        int hits = 0;
        const auto p = write_file("e.txt", "one");
        const uint64_t tok = w.watch(p, [&](const std::string&) { ++hits; });

        w.unwatch(tok);
        check(w.watched_count() == 0, "unwatch removes the entry");
        touch_distinct(p, "two");
        w.poll(70.0);
        w.poll(70.5);
        check(hits == 0, "an unwatched file fires nothing");

        w.watch(p, [&](const std::string&) { ++hits; });
        w.watch(p, [&](const std::string&) { ++hits; });
        check(w.watched_count() == 2, "the same path can carry two watchers");
        touch_distinct(p, "three");
        w.poll(71.0);
        w.poll(71.5);
        check(hits == 2, "both watchers on one path fire");

        w.unwatch_path(p);
        check(w.watched_count() == 0, "unwatch_path removes every watcher on it");
    }

    // ---- 9. a handler may watch/unwatch during its own callback ------------
    // Loading a mesh can pick up a new material to watch. Doing that while the
    // watcher iterates its own vector is a use-after-free waiting to happen.
    {
        AssetWatcher w;
        w.set_poll_interval(0.0);
        w.set_settle_time(0.1);
        int hits = 0;
        const auto p = write_file("f.txt", "one");
        w.watch(p, [&](const std::string&) {
            ++hits;
            w.watch((g_dir / "spawned.txt").string(), [&](const std::string&) { ++hits; });
        });

        touch_distinct(p, "two");
        w.poll(80.0);
        w.poll(80.5);
        check(hits == 1, "a handler that watches a new file during its callback is safe");
        check(w.watched_count() == 2, "the newly watched file is registered");
    }

    fs::remove_all(g_dir, ec);

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL assetwatch_check\n");
    return g_fail ? 1 : 0;
}
