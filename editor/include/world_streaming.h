#pragma once

// ============================================================================
// EditorWorldStreaming — connect the verified streaming stack to a live scene.
//
// WorldGrid, StreamingManager and FloatingOrigin were built and verified (21
// assertions in world_check) and then referenced by **no editor file at all**.
// A library that nothing runs is indistinguishable from a library that does not
// work, which is the whole premise of this phase.
//
// WHAT A "CELL" CONTAINS HERE. There is no world-partitioned content format
// yet, so the content of a cell is simply the entities whose world position
// falls inside it. Loading a cell activates them; unloading deactivates them.
// That is genuine streaming behaviour — it controls what is live based on
// distance from the focus — without inventing a format that would have to be
// thrown away when real partitioned content arrives.
//
// THE REBASE CONTRACT, which is the part that is easy to get subtly wrong.
// FloatingOrigin keeps a double-precision world origin and, past a threshold,
// returns a shift that must be applied to EVERYTHING with a local position:
// every entity AND the camera. Apply it to entities but not the camera and the
// world appears to teleport; apply it to the camera but not entities and the
// camera flies away. It is applied to ROOT entities only — children inherit
// through their parent's transform, so shifting both would double-shift them.
//
// Off by default. Streaming deactivates entities, and an editor that silently
// hides parts of the scene you are working on would be a bug, not a feature.
// ============================================================================

#include "entity.h"
#include "scene.h"
#include "transform.h"
#include "world/floating_origin.h"
#include "world/streaming_manager.h"
#include "world/world_grid.h"

#include <spdlog/spdlog.h>

#include <unordered_set>

namespace schizo::editor {

class EditorWorldStreaming {
public:
    EditorWorldStreaming()
        : mgr_(schizo::world::WorldGrid{kCellSize}, kLoadRadius, kUnloadRadius) {}

    bool  enabled() const { return enabled_; }
    void  set_enabled(bool e) { enabled_ = e; }

    // Stats, for a UI that can show what streaming is actually doing rather
    // than asking the user to take it on faith.
    size_t active_cells()   const { return mgr_.active_count(); }
    size_t loaded_cells()   const { return mgr_.loaded_count(); }
    size_t streamed_out()   const { return streamed_out_; }
    size_t rebase_count()   const { return rebases_; }
    glm::vec3 total_shift() const { return total_shift_; }

    /// Drive one frame. `focus` is the camera position. Returns the shift the
    /// caller must apply to its own camera when a rebase happened — the caller
    /// owns the camera, so it cannot be moved from in here.
    glm::vec3 update(const std::shared_ptr<schizo::scene::Scene>& scene,
                     const glm::vec3& focus) {
        glm::vec3 camera_shift(0.0f);
        if (!enabled_ || !scene) return camera_shift;

        // 1) Cell residency from the focus position.
        mgr_.update(focus);
        // Nothing here loads asynchronously yet — a cell's content is entities
        // that already exist — so a cell is Loaded as soon as it is requested.
        // Saying so beats faking a load delay.
        for (const auto& c : mgr_.last_loads()) mgr_.mark_loaded(c);

        // 2) Activate entities inside loaded cells, deactivate the rest.
        streamed_out_ = 0;
        const auto& grid = mgr_.grid();
        for (const auto& ent : scene->GetEntities()) {
            if (!ent) continue;
            auto* tf = ent->GetTransform();
            if (!tf) continue;
            // Children follow their parent's fate; deciding per-child would let
            // a child outlive the parent it is positioned relative to.
            if (ent->GetParent()) continue;

            const glm::vec3 p = tf->GetWorldPosition();
            const bool live = mgr_.state(grid.to_cell(p)) == schizo::world::CellState::Loaded;
            if (ent->IsActive() != live) ent->SetActive(live);
            if (!live) ++streamed_out_;
        }

        // 3) Floating-origin rebase.
        glm::vec3 shift(0.0f);
        if (origin_.update(focus, shift)) {
            for (const auto& ent : scene->GetEntities()) {
                if (!ent || ent->GetParent()) continue;   // roots only
                if (auto* tf = ent->GetTransform())
                    tf->SetLocalPosition(tf->GetLocalPosition() + shift);
            }
            camera_shift = shift;      // the caller moves its own camera
            total_shift_ += shift;
            ++rebases_;
            spdlog::info("[streaming] origin rebase by ({:.0f}, {:.0f}) — "
                         "world position preserved, local coordinates reset",
                         shift.x, shift.z);
        }
        return camera_shift;
    }

    /// Put every entity back on: leaving streaming enabled while editing would
    /// otherwise hide parts of the scene the moment it is switched off.
    void restore_all(const std::shared_ptr<schizo::scene::Scene>& scene) {
        if (!scene) return;
        for (const auto& ent : scene->GetEntities())
            if (ent && !ent->IsActive()) ent->SetActive(true);
        streamed_out_ = 0;
    }

private:
    // Editor-scale defaults: a cell you can see across, a load radius that
    // keeps the visible world resident, and hysteresis so cells do not thrash
    // when the camera sits on a boundary.
    static constexpr float  kCellSize     = 64.0f;
    static constexpr float  kLoadRadius   = 160.0f;
    static constexpr float  kUnloadRadius = 240.0f;

    bool                            enabled_ = false;
    schizo::world::StreamingManager mgr_;
    schizo::world::FloatingOrigin   origin_{1024.0, 64.0};
    size_t                          streamed_out_ = 0;
    size_t                          rebases_ = 0;
    glm::vec3                       total_shift_{0.0f};
};

}  // namespace schizo::editor
