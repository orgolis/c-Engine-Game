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
    ParticleSystem() = default;
    explicit ParticleSystem(const EmitterConfig& cfg) : cfg_(cfg) {}

    EmitterConfig& config() { return cfg_; }
    const EmitterConfig& config() const { return cfg_; }

    void set_position(const glm::vec3& p) { position_ = p; }
    void set_emitting(bool e) { emitting_ = e; }
    bool emitting() const { return emitting_; }
    void set_seed(uint32_t s) { rng_ = s ? s : 1u; }   // deterministic

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
    std::vector<Particle> particles_;
    glm::vec3 position_{0.0f};
    bool      emitting_ = true;
    float     spawn_accum_ = 0.0f;
    uint32_t  rng_ = 0x1234567u;
};

} // namespace schizo::vfx
