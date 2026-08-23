#pragma once

// ============================================================================
// Particle system — CPU-simulated particles with camera-facing billboards, for
// ability/impact/ambient VFX. (VFX pillar.) World-space particles integrate
// under gravity + drag, age/fade over their lifetime, and build camera-facing
// billboard quads for a GPU backend to draw.
//
// CAMERA / LARGE-WORLD contract (important — works with ANY player camera):
//   * build_billboards() takes the camera position + up, so billboards face
//     whatever camera rig is active (first-person, third-person, orbit …).
//   * world-space particles hold ABSOLUTE-ish world positions; on a
//     floating-origin rebase the caller calls apply_shift(delta) so particles
//     move with the camera + entities and never jump. (See FloatingOrigin.)
// ============================================================================

#include "vfx/vfx_graph.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace schizo::vfx {

struct Particle {
    glm::vec3 pos{0.0f};
    glm::vec3 vel{0.0f};
    float     age  = 0.0f;
    float     life = 1.0f;
    float     seed = 0.0f;   // per-particle [0,1) for variation

    // Size and colour are particle STATE, not render-time arithmetic. They used
    // to be computed inside build_billboards from the emitter config, which a
    // GPU simulation could not do: compute writes the buffer and the vertex
    // stage only reads it. Modules write these; the billboard builder reads them.
    float     size = 0.25f;
    glm::vec4 color{1.0f};
};

struct EmitterConfig {
    float     spawn_rate = 50.0f;         // particles / second
    float     life_min = 1.0f, life_max = 2.0f;
    glm::vec3 base_velocity{0.0f, 3.0f, 0.0f};
    float     velocity_spread = 1.0f;     // random cone magnitude
    glm::vec3 gravity{0.0f, -9.8f, 0.0f};
    float     drag = 0.1f;                 // per-second velocity damping
    float     size_start = 0.25f, size_end = 0.0f;
    glm::vec4 color_start{1.0f, 0.6f, 0.15f, 1.0f};
    glm::vec4 color_end{0.8f, 0.1f, 0.0f, 0.0f};
    uint32_t  max_particles = 2048;
    bool      world_space = true;          // false = local to the emitter
};

/// One billboard quad corner (a GPU backend draws 2 tris / 4 verts per particle).
struct BillboardVertex {
    glm::vec3 pos;
    glm::vec2 uv;
    glm::vec4 color;
};

class ParticleSystem {
public:
    ParticleSystem();
    /// Compatibility facade: builds the default stack from `cfg`. Kept because
    /// vfx_check and old scene files both arrive holding an EmitterConfig, and a
    /// rewritten regression test proves nothing about the rewrite it polices.
    explicit ParticleSystem(const EmitterConfig& cfg);

    EmitterConfig& config() { return cfg_; }
    const EmitterConfig& config() const { return cfg_; }

    void set_position(const glm::vec3& p) { position_ = p; }
    void set_emitting(bool e) { emitting_ = e; }
    bool emitting() const { return emitting_; }
    void set_seed(uint32_t s) { rng_ = s ? s : 1u; }   // deterministic

    /// Drive the simulation from a VFX document. Replaces the config-driven
    /// path; ParticleSystem(EmitterConfig) simply builds one of these.
    ///
    /// Deliberately does NOT clear live particles: hot reload calls this on
    /// every edit, and clearing would make each save vanish the effect and
    /// restart it, which reads as a broken editor rather than as a reload.
    void set_graph(const VfxGraph& g) { graph_ = g; }
    const VfxGraph& graph() const { return graph_; }

    /// Per-instance multiplier on the Spawn stage's output. See
    /// ParticleEmitterComponent::rate_scale.
    void set_rate_scale(float s) { rate_scale_ = s < 0.0f ? 0.0f : s; }

    /// Emit `n` particles immediately at the emitter position.
    void emit_burst(int n);

    /// Advance: spawn by rate (if emitting), integrate, age, and reap dead.
    void update(float dt);

    /// Apply a floating-origin rebase shift to every world-space particle.
    void apply_shift(const glm::vec3& delta);

    /// Build camera-facing billboard quads (4 verts each) for the alive
    /// particles. Quad normal points at `cam_pos`; size/colour interpolate over
    /// each particle's life. `local_to_world` positions local-space emitters.
    void build_billboards(const glm::vec3& cam_pos, const glm::vec3& cam_up,
                          std::vector<BillboardVertex>& out,
                          const glm::mat4& local_to_world = glm::mat4(1.0f)) const;

    size_t alive() const { return particles_.size(); }
    const std::vector<Particle>& particles() const { return particles_; }

private:
    float rand01();
    void  spawn_one();

    EmitterConfig cfg_;
    VfxGraph  graph_;
    std::vector<Particle> particles_;
    glm::vec3 position_{0.0f};
    bool      emitting_ = true;
    float     spawn_accum_ = 0.0f;
    float     elapsed_     = 0.0f;   // seconds since start, for burst timing
    float     rate_scale_  = 1.0f;
    uint32_t  rng_ = 0x1234567u;
};

} // namespace schizo::vfx
