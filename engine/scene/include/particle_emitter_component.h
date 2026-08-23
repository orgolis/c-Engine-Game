#pragma once

// ============================================================================
// ParticleEmitterComponent — makes an entity a particle emitter.
//
// Data only, like MeshComponent and SkinnedMeshComponent: the simulation state
// (live particles, RNG, accumulators) lives editor-side keyed by entity, so a
// scene saves and loads without carrying transient particles, and two entities
// with identical settings still get independent streams.
// ============================================================================

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace schizo::scene {

struct ParticleEmitterComponent {
    /// Off means "this entity is not an emitter" — the check the render path
    /// uses. Distinct from `emitting`, which pauses spawning while KEEPING the
    /// particles already alive, so a burst can finish rather than vanish.
    bool     enabled  = false;
    bool     emitting = true;

    /// The .vfx asset this entity plays (4.3). Empty means "no asset yet" —
    /// the legacy fields below describe the effect instead.
    ///
    /// Not reflected, because offset reflection cannot carry a std::string; the
    /// scene serializer writes VFX_PATH by hand, exactly as it already does for
    /// MeshComponent::mesh_path. See reflected_text_io.h.
    std::string vfx_path;

    /// Per-instance spawn multiplier, applied to the count the Spawn stage
    /// produces. It deliberately does NOT reach into module parameters: those
    /// belong to the shared asset, and an instance editing them would make "the
    /// same effect" mean different things in different places.
    /// 0.0 spawns nothing — distinct from `emitting = false`, which is the
    /// per-frame toggle rather than a saved property of this placement.
    float rate_scale = 1.0f;

    // ---- Legacy (pre-4.3) -------------------------------------------------
    // These four described the whole effect before .vfx assets existed. They
    // are KEPT, and kept reflected, because they are the only record of what an
    // existing scene's emitters were configured to do: dropping them would make
    // every already-authored emitter silently snap back to library defaults on
    // load, which is precisely the data loss 3.7 was written to fix.
    //
    // When vfx_path is empty the runtime builds its stack from these, so an old
    // scene keeps its exact look. Saving the effect as a .vfx and setting
    // vfx_path is what retires them, per entity, as an explicit user action.
    float    spawn_rate    = 50.0f;    // particles / second
    uint32_t max_particles = 2048;

    glm::vec4 color_start{1.0f, 0.6f, 0.15f, 1.0f};
    glm::vec4 color_end  {0.8f, 0.1f, 0.0f,  0.0f};

    bool active() const { return enabled; }
};

}  // namespace schizo::scene
