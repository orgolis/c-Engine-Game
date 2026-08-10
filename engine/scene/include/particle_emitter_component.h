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

namespace schizo::scene {

struct ParticleEmitterComponent {
    /// Off means "this entity is not an emitter" — the check the render path
    /// uses. Distinct from `emitting`, which pauses spawning while KEEPING the
    /// particles already alive, so a burst can finish rather than vanish.
    bool     enabled  = false;
    bool     emitting = true;

    float    spawn_rate    = 50.0f;    // particles / second
    uint32_t max_particles = 2048;

    glm::vec4 color_start{1.0f, 0.6f, 0.15f, 1.0f};
    glm::vec4 color_end  {0.8f, 0.1f, 0.0f,  0.0f};

    bool active() const { return enabled; }
};

}  // namespace schizo::scene
