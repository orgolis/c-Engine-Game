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
#include "vfx/vfx_io.h"
#include "assets/asset_watcher.h"

#include <cstdint>
#include <string>
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

        // Apply any .vfx edits that settled since last frame. Done HERE, not in
        // the watcher callback: that fires from AssetWatcher::poll() mid-frame,
        // and swapping a graph while the emitter loop below is iterating it is
        // a use-after-free waiting to happen.
        if (!dirty_paths_.empty()) {
            for (const std::string& p : dirty_paths_) {
                schizo::vfx::VfxGraph g;
                if (!schizo::vfx::load_vfx(p, g)) continue;
                for (const auto& ent : scene->GetEntities()) {
                    if (!ent) continue;
                    auto* c = ent->GetParticleEmitterComponent();
                    if (!c || c->vfx_path != p) continue;
                    auto it = systems_.find(ent->GetId());
                    // set_graph deliberately keeps the live particles, so an
                    // edit changes the effect instead of restarting it.
                    if (it != systems_.end()) it->second->set_graph(g);
                }
            }
            dirty_paths_.clear();
        }

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
                // A .vfx asset wins. Falling back to the legacy fields rather
                // than to library defaults is what keeps an already-authored
                // emitter looking the way its scene says it should: those four
                // values are the only record of it that exists.
                schizo::vfx::VfxGraph graph;
                if (pe->vfx_path.empty() || !schizo::vfx::load_vfx(pe->vfx_path, graph)) {
                    schizo::vfx::EmitterConfig cfg;
                    cfg.spawn_rate    = pe->spawn_rate;
                    cfg.max_particles = pe->max_particles;
                    cfg.color_start   = pe->color_start;
                    cfg.color_end     = pe->color_end;
                    graph = schizo::vfx::VfxGraph::from_emitter_config(cfg);
                }
                it = systems_.emplace(id, std::make_unique<schizo::vfx::ParticleSystem>()).first;
                it->second->set_graph(graph);
                // Deterministic per entity: two emitters with the same settings
                // should not produce identical particle streams, and the same
                // emitter should look the same across a reload.
                it->second->set_seed(id * 2654435761u + 1u);

                // Watch the asset once, however many entities play it.
                if (watcher_ && !pe->vfx_path.empty() && !watched_.count(pe->vfx_path)) {
                    const std::string p = pe->vfx_path;
                    watched_[p] = watcher_->watch(p, [this, p](const std::string&) {
                        dirty_paths_.push_back(p);
                    });
                }
            }

            auto& sys = *it->second;
            sys.set_position(tf->GetWorldPosition());
            sys.set_emitting(pe->emitting);
            sys.set_rate_scale(pe->rate_scale);
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

        // Drop watches for paths no live emitter references any more, so a
        // scene that stops using an effect stops paying to stat its file.
        if (watcher_) {
            for (auto it = watched_.begin(); it != watched_.end(); ) {
                bool still_used = false;
                for (const auto& ent : scene->GetEntities()) {
                    auto* c = ent ? ent->GetParticleEmitterComponent() : nullptr;
                    if (c && c->enabled && c->vfx_path == it->first) { still_used = true; break; }
                }
                if (still_used) { ++it; continue; }
                watcher_->unwatch(it->second);
                it = watched_.erase(it);
            }
        }
    }

    /// Hot reload. Without a watcher the cache still works — it simply never
    /// re-reads a .vfx, which is the pre-4.3 behaviour.
    void set_watcher(gws::assets::AssetWatcher* w) { watcher_ = w; }

    /// Camera-facing quads for the frame: 4 verts per particle, ready for a
    /// GPU backend. Empty until an emitter entity exists.
    const std::vector<schizo::vfx::BillboardVertex>& billboards() const { return vertices_; }
    size_t vertex_count() const { return vertices_.size(); }
    size_t live_particles() const { return live_; }
    size_t emitter_count() const { return systems_.size(); }

    void clear() { systems_.clear(); vertices_.clear(); live_ = 0; }

private:
    gws::assets::AssetWatcher* watcher_ = nullptr;
    /// path -> watch token, so a path is watched once no matter how many
    /// entities play it, and unwatched when the last one goes.
    std::unordered_map<std::string, uint64_t> watched_;
    /// Paths whose file changed since the last update(); drained at the TOP of
    /// update() rather than inside the callback. See the note there.
    std::vector<std::string> dirty_paths_;

    std::unordered_map<uint32_t, std::unique_ptr<schizo::vfx::ParticleSystem>> systems_;
    std::vector<schizo::vfx::BillboardVertex> vertices_;
    std::vector<schizo::vfx::BillboardVertex> scratch_;   // reused, not reallocated per emitter
    size_t live_ = 0;
};

}  // namespace schizo::editor
