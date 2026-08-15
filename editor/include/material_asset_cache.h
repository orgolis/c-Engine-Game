#pragma once

// ============================================================================
// MaterialAssetCache — .mat files on disk → MaterialDesc, hot-reloaded.
//
// Deliberately Vulkan-free, and split out of material_cache.h for that reason:
// the inspector panel that EDITS materials, and the check tool that verifies
// this cache's behaviour, both need it without needing a GPU. Only the half
// that builds descriptor sets (MaterialGpuCache) has to know about Vulkan.
//
// The behaviour worth defending here is what happens when a material does NOT
// load. A cache that quietly substitutes a default renders a plausible white
// surface for a typo'd path and logs nothing, which is the most expensive
// failure this whole feature can have — so a failed path is remembered AS a
// failure (retrying every frame would spam the log at 60 Hz), the entity falls
// back to its inline material, and the file stays watched so creating it later
// fixes the scene without a restart.
// ============================================================================

#include "assets/material_desc.h"
#include "assets/asset_watcher.h"
#include "asset_path_util.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>

namespace schizo::editor {

class MaterialAssetCache {
public:
    /// The material at `path`, or nullptr if it could not be loaded. The
    /// pointer is stable until a poll_reload() that reloads that path.
    const gws::assets::MaterialDesc* get(const std::string& path) {
        if (path.empty()) return nullptr;
        auto it = entries_.find(path);
        if (it != entries_.end())
            return it->second.ok ? &it->second.desc : nullptr;

        // Materials are referenced by project-relative path but the editor can
        // be launched from anywhere; resolve like every other asset reference.
        const std::string disk_path = resolve_asset_path(path);

        Entry e;
        std::string err;
        e.ok = gws::assets::load_material(disk_path, e.desc, &err);
        if (!e.ok)
            spdlog::error("[MaterialCache] {} — entities using it fall back to "
                          "their inline material", err);

        // Watch either way. A material referenced before it exists is a normal
        // state mid-authoring; it has to start working the moment it is saved.
        watch(path, disk_path);

        auto ins = entries_.emplace(path, std::move(e)).first;
        return ins->second.ok ? &ins->second.desc : nullptr;
    }

    /// Re-read materials whose file changed. Returns how many actually changed
    /// CONTENT — a save that rewrites identical bytes returns 0, so nothing
    /// downstream rebuilds a descriptor set for a no-op write. Safe every frame.
    size_t poll_reload() { return poll_reload_at(now_seconds()); }

    /// Time-injected form, so the check tool can exercise settle behaviour
    /// without sleeping. `now` is seconds on any monotonic scale.
    size_t poll_reload_at(double now) {
        watcher_.poll(now);
        if (dirty_.empty()) return 0;

        std::vector<std::string> dirty;
        dirty.swap(dirty_);
        std::sort(dirty.begin(), dirty.end());
        dirty.erase(std::unique(dirty.begin(), dirty.end()), dirty.end());

        size_t changed = 0;
        for (const auto& key : dirty) {
            auto it = entries_.find(key);
            if (it == entries_.end()) continue;

            gws::assets::MaterialDesc fresh;
            std::string err;
            if (!gws::assets::load_material(resolve_asset_path(key), fresh, &err)) {
                // A material that WAS working and now will not load is worth a
                // loud line — usually deleted or moved out from under a scene
                // that still references it.
                if (it->second.ok) {
                    spdlog::error("[MaterialCache] '{}' stopped loading — {}", key, err);
                    ++changed;
                }
                it->second.ok = false;
                continue;
            }
            const bool same = it->second.ok &&
                              it->second.desc.content_hash() == fresh.content_hash();
            it->second.desc = std::move(fresh);
            it->second.ok   = true;
            if (!same) {
                ++changed;
                spdlog::info("[MaterialCache] '{}' reloaded", key);
            }
        }
        return changed;
    }

    /// Update a material IN MEMORY only, without touching the file.
    ///
    /// This is what makes dragging a roughness slider update the viewport on the
    /// frame you drag it. Writing to disk on every slider frame would issue
    /// hundreds of file writes per gesture and trip the watcher's settle logic
    /// continuously, so the editor edits in memory while the mouse is down and
    /// calls save() once the gesture ends. The consequence is stated rather than
    /// hidden: between those two points the file on disk is stale, which is
    /// exactly the window in which "unsaved changes" is the honest answer.
    void set(const std::string& path, const gws::assets::MaterialDesc& desc) {
        if (path.empty()) return;
        Entry& e = entries_[path];
        e.desc = desc;
        e.ok   = true;
    }

    /// Write a material to disk AND update the cache in one step, so an
    /// in-editor edit and the file never disagree. Returns false (reason
    /// logged) if the write failed, leaving the cache showing what is actually
    /// on disk rather than what the user hoped to save.
    bool save(const std::string& path, const gws::assets::MaterialDesc& desc) {
        if (path.empty()) return false;
        const std::string disk_path = resolve_asset_path(path);
        std::string err;
        if (!gws::assets::save_material(disk_path, desc, &err)) {
            spdlog::error("[MaterialCache] {}", err);
            return false;
        }
        Entry& e = entries_[path];
        e.desc = desc;
        e.ok   = true;
        watch(path, disk_path);
        return true;
    }

    /// True when this path is known to have failed to load. The inspector uses
    /// it to say so in place of showing fields that are not driving anything.
    bool failed(const std::string& path) const {
        auto it = entries_.find(path);
        return it != entries_.end() && !it->second.ok;
    }

    /// Forget a path so the next get() re-reads it from disk.
    void invalidate(const std::string& path) { entries_.erase(path); }

    void clear() {
        entries_.clear();
        watcher_.clear();
        watched_.clear();
        dirty_.clear();
    }

private:
    struct Entry {
        gws::assets::MaterialDesc desc;
        bool ok = false;
    };

    void watch(const std::string& key, const std::string& disk_path) {
        if (watched_.count(key)) return;
        watched_.insert(key);
        const std::string k = key;
        watcher_.watch(disk_path, [this, k](const std::string&) { dirty_.push_back(k); });
    }

    static double now_seconds() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    std::unordered_map<std::string, Entry> entries_;
    gws::assets::AssetWatcher              watcher_;
    std::unordered_set<std::string>        watched_;
    std::vector<std::string>               dirty_;
};

}  // namespace schizo::editor
