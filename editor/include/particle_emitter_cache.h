#pragma once

// ============================================================================
// EditorParticleEmitters — run the verified particle system on real entities.
//
// vfx/particle_system.{h,cpp} was built and verified (12 assertions in
// vfx_check) and then referenced by exactly one editor file: the feature-toggle
// enum. Nothing simulated a single particle. Same shape as world streaming and
// the navmesh bake — a library nothing runs.
//
// SCOPE, STATED PLAINLY. This runs the SIMULATION on entities and produces the
// camera-facing billboard vertices each frame. It does NOT draw them: that
// needs a dedicated Vulkan pipeline, because BillboardVertex carries a per-
// particle colour and SceneVertex has no colour channel — reusing the mesh path
// would mean dropping the fade-over-life that is most of what makes particles
// read correctly. `vertex_count()` is exposed so the remaining GPU work has a
// verifiable input and so the UI can show the simulation is live rather than
// asking anyone to take it on faith.
//
// THE REBASE CONTRACT. Particles hold WORLD-space positions, so a
// floating-origin rebase must shift them like everything else. ParticleSystem
// already has apply_shift for exactly this; wiring it is the difference between
// particles that stay with their emitter and particles that jump 1024 units
// away the first time the camera crosses the threshold.
// ============================================================================

#include "entity.h"
#include "scene.h"
#include "transform.h"
#include "vfx/particle_system.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace schizo::editor {

class EditorParticleEmitters {
public:
    /// Advance every emitter entity and rebuild its billboards.
    /// `origin_shift` is non-zero on the frame a floating-origin rebase
    /// happened and must be applied to live particles.
    void update(const std::shared_ptr<schizo::scene::Scene>& scene,
                float dt,
                const glm::vec3& cam_pos,
                const glm::vec3& cam_up,
                const glm::vec3& origin_shift = glm::vec3(0.0f)) {
        vertices_.clear();
        live_ = 0;
        if (!scene) return;

        std::unordered_map<uint32_t, bool> seen;
        for (const auto& ent : scene->GetEntities()) {
            if (!ent || !ent->IsActive()) continue;
            auto* pe = ent->GetParticleEmitterComponent();
            if (!pe || !pe->enabled) continue;
            auto* tf = ent->GetTransform();
            if (!tf) continue;

            const uint32_t id = ent->GetId();
            seen[id] = true;
            auto it = systems_.find(id);
            if (it == systems_.end()) {
                schizo::vfx::EmitterConfig cfg;
                cfg.spawn_rate    = pe->spawn_rate;
                cfg.max_particles = pe->max_particles;
                cfg.color_start   = pe->color_start;
                cfg.color_end     = pe->color_end;
                it = systems_.emplace(id, std::make_unique<schizo::vfx::ParticleSystem>(cfg)).first;
                // Deterministic per entity: two emitters with the same settings
                // should not produce identical particle streams, and the same
                // emitter should look the same across a reload.
                it->second->set_seed(id * 2654435761u + 1u);
            }

            auto& sys = *it->second;
            sys.set_position(tf->GetWorldPosition());
            sys.set_emitting(pe->emitting);
            // Particles are world-space, so a rebase moves them too. Without
            // this they stay at their absolute coordinates while the world
            // shifts underneath — the emitter walks away from its own smoke.
            if (origin_shift != glm::vec3(0.0f)) sys.apply_shift(origin_shift);
            sys.update(dt);
            // build_billboards CLEARS its output, so it cannot be called
            // straight into the shared buffer: with two emitters the second
            // would silently erase the first, and only the last emitter in the
            // scene would ever draw. Build into scratch, then append.
            scratch_.clear();
            sys.build_billboards(cam_pos, cam_up, scratch_);
            vertices_.insert(vertices_.end(), scratch_.begin(), scratch_.end());
            live_ += sys.alive();
        }

        // Drop simulations for entities that are gone or no longer emitters.
        for (auto it = systems_.begin(); it != systems_.end(); ) {
            if (seen.count(it->first)) ++it;
            else                       it = systems_.erase(it);
        }
    }

    /// Camera-facing quads for the frame: 4 verts per particle, ready for a
    /// GPU backend. Empty until an emitter entity exists.
    const std::vector<schizo::vfx::BillboardVertex>& billboards() const { return vertices_; }
    size_t vertex_count() const { return vertices_.size(); }
    size_t live_particles() const { return live_; }
    size_t emitter_count() const { return systems_.size(); }

    void clear() { systems_.clear(); vertices_.clear(); live_ = 0; }

private:
    std::unordered_map<uint32_t, std::unique_ptr<schizo::vfx::ParticleSystem>> systems_;
    std::vector<schizo::vfx::BillboardVertex> vertices_;
    std::vector<schizo::vfx::BillboardVertex> scratch_;   // reused, not reallocated per emitter
    size_t live_ = 0;
};

}  // namespace schizo::editor
