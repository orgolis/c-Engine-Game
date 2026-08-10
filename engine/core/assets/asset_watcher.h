#pragma once

// ============================================================================
// AssetWatcher — one place that notices a file changed on disk.
//
// WHY THIS EXISTS. Hot reload was already working in three places (textures,
// scripts, the sky hot-swap) and each had grown its OWN mtime poll loop. That
// is the shape where behaviour drifts: each copy picks its own poll rate, and
// a fix to one is not a fix to the others. Adding mesh reload would have been
// a fourth copy. This is the shared layer they should have had, so "make X
// hot-reloadable" becomes one watch() call rather than another poll loop.
//
// THE BUG THIS FIXES ON THE WAY. Every existing copy reloads the instant the
// mtime moves, which races the tool that is still writing the file — Blender
// exporting a 40 MB glTF, or a shader compiler rewriting a .spv. The reader
// wins the race often enough to be noticed and rarely enough to be blamed on
// "hot reload is flaky". So a change here must SETTLE: the file's mtime and
// size must both hold steady across a poll before the callback fires. That is
// what makes reload trustworthy rather than merely present.
//
// DESIGN. Polling, not filesystem notifications: ReadDirectoryChangesW and
// inotify are per-platform, do not agree on semantics across network shares,
// and would need their own thread. A stat per watched file at 10 Hz is a
// rounding error next to a frame, and the behaviour is identical everywhere,
// including on the network drives real teams keep projects on.
//
// Not thread-safe by design — poll() runs on the thread that owns the reload
// (the editor thread, where the GPU work has to happen anyway).
// ============================================================================

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace gws::assets {

class AssetWatcher {
public:
    // Fired from poll() once the file has settled. `path` is what was watched.
    using Callback = std::function<void(const std::string& path)>;

    // Start watching `path`. Returns a token for unwatch(), or 0 if the path is
    // empty. Watching the same path twice registers two independent callbacks —
    // the texture manager and a material editor can both care about one file.
    //
    // A file that does not exist yet is still accepted: appearing later counts
    // as a change, which is what makes "export into the project folder" work.
    uint64_t watch(std::string path, Callback on_change);

    void unwatch(uint64_t token);
    void unwatch_path(const std::string& path);
    void clear();

    // Check every watched file. `now_seconds` is monotonic seconds — passed in
    // rather than read here so tests can drive time directly instead of
    // sleeping, which is the difference between a test that is fast and
    // deterministic and one that is slow and flaky.
    //
    // Returns the number of callbacks fired. Cheap to call every frame: it
    // returns immediately unless poll_interval has elapsed.
    size_t poll(double now_seconds);

    // Seconds between filesystem scans. Default 0.25.
    void set_poll_interval(double s) { poll_interval_ = s < 0.0 ? 0.0 : s; }

    // How long a file must hold the same mtime AND size before the change is
    // reported. Default 0.2 — long enough to cover a normal write, short enough
    // to feel immediate. Zero disables settling (do not: see the header note).
    void set_settle_time(double s) { settle_ = s < 0.0 ? 0.0 : s; }

    size_t watched_count() const { return entries_.size(); }

    // Files seen changing but not yet settled. Exposed so a UI can honestly say
    // "importing…" instead of appearing to have missed the edit.
    size_t pending_count() const;

private:
    struct Entry {
        uint64_t    token = 0;
        std::string path;
        Callback    on_change;

        bool        exists = false;
        std::filesystem::file_time_type mtime{};
        uintmax_t   size = 0;

        // Set while a change is settling.
        bool        pending = false;
        double      pending_since = 0.0;
        std::filesystem::file_time_type pending_mtime{};
        uintmax_t   pending_size = 0;
    };

    std::vector<Entry> entries_;
    uint64_t next_token_    = 1;
    double   poll_interval_ = 0.25;
    double   settle_        = 0.2;
    double   last_poll_     = -1e9;
};

}  // namespace gws::assets
