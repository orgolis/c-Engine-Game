#include "asset_watcher.h"

#include <spdlog/spdlog.h>

#include <algorithm>

namespace fs = std::filesystem;

namespace gws::assets {
namespace {

// Read mtime + size without throwing. A file being replaced can vanish for a
// moment between the two calls (a common editor save pattern is write-temp +
// rename), so a failure here means "not there right now", not an error worth
// logging.
bool stat_file(const std::string& path, fs::file_time_type& mtime, uintmax_t& size) {
    std::error_code ec;
    const auto p = fs::u8path(path);
    if (!fs::exists(p, ec) || ec) return false;
    mtime = fs::last_write_time(p, ec);
    if (ec) return false;
    size = fs::file_size(p, ec);
    if (ec) return false;
    return true;
}

}  // namespace

uint64_t AssetWatcher::watch(std::string path, Callback on_change) {
    if (path.empty() || !on_change) return 0;

    Entry e;
    e.token     = next_token_++;
    e.path      = std::move(path);
    e.on_change = std::move(on_change);
    // Seed with the CURRENT state so merely starting to watch never fires a
    // change. Watching a file must not itself look like an edit.
    e.exists = stat_file(e.path, e.mtime, e.size);

    const uint64_t token = e.token;
    entries_.push_back(std::move(e));
    return token;
}

void AssetWatcher::unwatch(uint64_t token) {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [token](const Entry& e) { return e.token == token; }),
                   entries_.end());
}

void AssetWatcher::unwatch_path(const std::string& path) {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&path](const Entry& e) { return e.path == path; }),
                   entries_.end());
}

void AssetWatcher::clear() { entries_.clear(); }

size_t AssetWatcher::pending_count() const {
    size_t n = 0;
    for (const auto& e : entries_) if (e.pending) ++n;
    return n;
}

size_t AssetWatcher::poll(double now_seconds) {
    if (now_seconds - last_poll_ < poll_interval_) return 0;
    last_poll_ = now_seconds;

    // Callbacks are collected and run AFTER the scan. A reload handler is
    // allowed to watch or unwatch files (loading a mesh may pick up a new
    // material), and doing that mid-iteration would invalidate the vector.
    std::vector<std::pair<Callback, std::string>> fire;

    for (auto& e : entries_) {
        fs::file_time_type mtime{};
        uintmax_t size = 0;
        const bool exists = stat_file(e.path, mtime, size);

        // Deletion is not a change to report — a file vanishing is usually the
        // middle of a save, and there is nothing useful to reload from. Drop
        // any pending change and wait for it to come back.
        if (!exists) {
            e.exists  = false;
            e.pending = false;
            continue;
        }

        const bool differs = !e.exists || mtime != e.mtime || size != e.size;

        if (differs) {
            // Restart the settle timer whenever the file is still moving, so a
            // long write is reported once when it finishes rather than N times
            // while it is in progress.
            if (!e.pending || mtime != e.pending_mtime || size != e.pending_size) {
                e.pending       = true;
                e.pending_since = now_seconds;
                e.pending_mtime = mtime;
                e.pending_size  = size;
                // Deliberately falls through to the settle check rather than
                // waiting for the next poll: with settle_ == 0 the documented
                // behaviour is to fire immediately, and costing every change an
                // extra poll interval would quietly contradict that.
            }
        } else if (!e.pending) {
            continue;
        }

        // Steady for at least settle_ seconds -> the writer is done.
        if (e.pending && (now_seconds - e.pending_since) >= settle_) {
            e.exists  = true;
            e.mtime   = e.pending_mtime;
            e.size    = e.pending_size;
            e.pending = false;
            fire.emplace_back(e.on_change, e.path);
        }
    }

    for (auto& [cb, path] : fire) {
        try {
            cb(path);
        } catch (const std::exception& ex) {
            // One asset failing to reload must not take down the editor or stop
            // the other reloads in this batch.
            spdlog::error("[watch] reload handler threw for '{}': {}", path, ex.what());
        } catch (...) {
            spdlog::error("[watch] reload handler threw for '{}'", path);
        }
    }
    return fire.size();
}

}  // namespace gws::assets
