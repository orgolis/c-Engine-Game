#pragma once

// ============================================================
// ECS Scene Bridge  (Master Plan Stage 1.4, step 1 — additive)
// ============================================================
//
// The first migration step from the OOP GameObject/Component scene toward the
// data-oriented ecs::World. It is intentionally NON-AUTHORITATIVE ("shadow"):
// each frame it mirrors the live scene's transforms into an ecs::World and runs
// the transform -> LocalToWorld system on the job system. Rendering still reads
// the OOP scene, so this cannot change what is drawn — but it proves the ECS
// data path + parallel systems run live on real scene data, and it is the
// scaffold the later authoritative switch (rendering reads ECS) builds on.
//
// Pimpl so this header pulls in neither EnTT nor the ECS headers — main.cpp
// stays free of the C++20 EnTT include cost.

#include <cstddef>
#include <memory>

#include <glm/fwd.hpp>

namespace schizo::scene { class Scene; class Transform; }

namespace schizo::editor {

class EcsSceneBridge {
public:
    EcsSceneBridge();
    ~EcsSceneBridge();

    EcsSceneBridge(const EcsSceneBridge&) = delete;
    EcsSceneBridge& operator=(const EcsSceneBridge&) = delete;

    // Rebuild the shadow world from the scene's active entities (mirroring each
    // one's LOCAL transform), then run the transform -> LocalToWorld system in
    // parallel. Cheap and side-effect-free w.r.t. the scene; safe every frame.
    void sync_and_run(const std::shared_ptr<schizo::scene::Scene>& scene);

    // Authoritative read path (Stage 1.4 step 2): the ECS-computed world matrix
    // for the OOP entity owning `tf`, or nullptr if it wasn't in the last
    // sync (callers fall back to the OOP matrix). Valid until the next
    // sync_and_run. `tf` is the key used during sync (Entity::GetTransform()).
    const glm::mat4* world_matrix(const schizo::scene::Transform* tf) const;

    // Save the live shadow world to a snapshot blob and reload it into a temp
    // world (Stage 0.5 step 5). Returns true if the reload matches; reports the
    // blob size and reloaded entity count. Proves world snapshot save/load
    // works on real scene data.
    bool snapshot_selfcheck(size_t& out_bytes, size_t& out_reloaded_entities) const;

    // Single-pass ECS draw collection over the live shadow scene (Stage 1.4):
    // every entity with LocalToWorld + MeshRenderer -> one DrawRecord. Returns
    // the draw count; out_ms = wall time of the parallel pass.
    size_t collect_scene_draws(double& out_ms) const;

    // Synthetic benchmark: build `n` throwaway entities and run one parallel
    // draw-collect pass. Demonstrates the Stage 1.4 acceptance (100k entities,
    // single parallel pass, under budget) live in the real build.
    static size_t draw_benchmark(size_t n, double& out_ms);

    // --- Proof / stats from the most recent sync_and_run ---
    size_t entity_count()     const { return entity_count_; }     // shadow entities
    size_t matrices_written() const { return matrices_written_; } // system outputs
    size_t checked()          const { return checked_; }          // entities verified
    // Entities whose ECS-computed world matrix (parallel TRS + hierarchy
    // resolve) equals the OOP Transform::GetWorldMatrix() within epsilon. A
    // full match validates the whole path: reflected components -> world
    // storage -> parallel system math -> parent propagation.
    size_t verified()         const { return verified_; }
    // Persistence reconciliation counts for the most recent sync: entities
    // newly created, reused from the prior frame, and destroyed (no longer
    // active). reused>0 proves shadow entities now persist across frames.
    size_t created()          const { return created_; }
    size_t reused()           const { return reused_; }
    size_t destroyed()        const { return destroyed_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    size_t entity_count_     = 0;
    size_t matrices_written_ = 0;
    size_t checked_          = 0;
    size_t verified_         = 0;
    size_t created_          = 0;
    size_t reused_           = 0;
    size_t destroyed_        = 0;
};

}  // namespace schizo::editor
