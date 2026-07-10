// ============================================================
// ScriptSystem implementation — see script_system.h
// ============================================================

#include "script_system.h"

#include "scene.h"
#include "entity.h"
#include "script_component.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <system_error>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace schizo::editor {

namespace {
std::string ext_of(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string e = path.substr(dot);
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

// Last-write time as a raw 64-bit tick count. std::filesystem::last_write_time
// on this toolchain resolves to ~1 second (it goes through CRT stat), which
// made saves within the same second invisible to hot reload — so on Windows we
// read the 100 ns FILETIME directly.
uint64_t file_mtime64(const std::string& path) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fad)) return 0;
    return (static_cast<uint64_t>(fad.ftLastWriteTime.dwHighDateTime) << 32) |
           fad.ftLastWriteTime.dwLowDateTime;
#else
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(path, ec);
    return ec ? 0 : static_cast<uint64_t>(t.time_since_epoch().count());
#endif
}
}  // namespace

void ScriptSystem::update(const std::shared_ptr<schizo::scene::Scene>& scene,
                          bool playing, float dt, const ScriptApi& api) {
    if (!playing || !scene) {
        if (!slots_.empty()) {
            spdlog::info("[script] play ended — released {} script instance(s)", slots_.size());
            slots_.clear();
        }
        return;
    }

    // Hot reload: poll active script files' mtimes twice a second.
    watch_timer_ += dt;
    const bool check_mtime = watch_timer_ >= 0.5f;
    if (check_mtime) watch_timer_ = 0.0f;

    // Snapshot the entity list: scripts may SPAWN entities during their
    // update, which mutates scene->GetEntities() and would invalidate a live
    // iterator. Newly spawned entities are picked up next frame.
    const std::vector<std::shared_ptr<schizo::scene::Entity>> ents = scene->GetEntities();

    std::unordered_set<uint32_t> seen;
    for (const auto& ent : ents) {
        if (!ent || !ent->IsActiveInHierarchy()) continue;
        auto sc = ent->GetComponent<schizo::scene::ScriptComponent>();
        if (!sc || !sc->IsEnabled() || sc->GetScriptPath().empty()) continue;
        const uint32_t eid = ent->GetId();
        seen.insert(eid);

        Slot& slot = slots_[eid];

        // (Re)instantiate when: first seen, the path changed, or (on the mtime
        // poll) the file was modified — the hot-reload path.
        bool need_create = !slot.inst && !slot.failed;
        if (slot.path != sc->GetScriptPath()) { need_create = true; slot.failed = false; }
        if (check_mtime && !slot.path.empty()) {
            const uint64_t mt = file_mtime64(slot.path);
            if (mt != 0 && mt != slot.mtime) {
                need_create  = true;
                slot.failed  = false;
                spdlog::info("[script] '{}' changed on disk — hot reloading", slot.path);
            }
        }

        if (need_create) {
            slot.inst.reset();
            slot.started = false;
            slot.path    = sc->GetScriptPath();
            slot.mtime   = file_mtime64(slot.path);

            const std::string ext = ext_of(slot.path);
            auto hit = hosts_.find(ext);
            if (hit == hosts_.end()) {
                slot.failed = true;
                sc->SetStatus("no script backend for '" + ext + "'");
                spdlog::warn("[script] entity {}: {}", eid, sc->GetStatus());
                continue;
            }
            std::string err;
            slot.inst = hit->second->create(slot.path, &api, err);
            if (!slot.inst) {
                slot.failed = true;
                sc->SetStatus(err.empty() ? "failed to load script" : err);
                spdlog::warn("[script] entity {} '{}': {}", eid, slot.path, sc->GetStatus());
                continue;
            }
            spdlog::info("[script] entity {} loaded '{}' ({})",
                         eid, slot.path, hit->second->language());
        }
        if (!slot.inst) continue;

        std::string err;
        if (!slot.started) {
            if (!slot.inst->start(eid, err)) {
                slot.failed = true;
                slot.inst.reset();
                sc->SetStatus("on_start error: " + err);
                spdlog::warn("[script] entity {} on_start: {}", eid, err);
                continue;
            }
            slot.started = true;
            sc->SetStatus("running");
        }
        if (!slot.inst->update(eid, dt, err)) {
            slot.failed = true;
            slot.inst.reset();
            sc->SetStatus("on_update error: " + err);
            spdlog::warn("[script] entity {} on_update: {}", eid, err);
        }
    }

    // Drop slots for entities that vanished or lost their component.
    for (auto it = slots_.begin(); it != slots_.end();) {
        if (!seen.count(it->first)) it = slots_.erase(it);
        else ++it;
    }
}

}  // namespace schizo::editor
