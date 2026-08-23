// ============================================================================
// particle_system.cpp — CPU particle sim + camera-facing billboards. See header.
// ============================================================================

#include "vfx/particle_system.h"
#include "vfx/vfx_exec.h"
#include <algorithm>
#include <cmath>

namespace schizo::vfx {

ParticleSystem::ParticleSystem() : graph_(VfxGraph::default_stack()) {}

ParticleSystem::ParticleSystem(const EmitterConfig& cfg)
    : cfg_(cfg), graph_(VfxGraph::from_emitter_config(cfg)) {}

float ParticleSystem::rand01() {
    // xorshift32 — deterministic given the seed (reproducible tests).
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return static_cast<float>(rng_ & 0xFFFFFF) / static_cast<float>(0x1000000);
}

namespace {

/// One module, applied to one particle. Every case is local arithmetic on `p` —
/// see vfx_exec.h for why that restriction is load-bearing.
void apply_module(const VfxModule& m, Particle& p, const Context& ctx) {
    switch (m.kind) {
        case ModuleKind::InitLifetime: {
            const float lo = m.get_float("MIN", 1.0f), hi = m.get_float("MAX", 2.0f);
            p.life = lo + (hi - lo) * ctx_rand01(ctx);
            break;
        }
        case ModuleKind::InitVelocityCone: {
            const glm::vec3 base = m.get_vec3("BASE", glm::vec3(0.0f, 3.0f, 0.0f));
            const float spread   = m.get_float("SPREAD", 1.0f);
            const glm::vec3 r(ctx_rand01(ctx) * 2.0f - 1.0f,
                              ctx_rand01(ctx) * 2.0f - 1.0f,
                              ctx_rand01(ctx) * 2.0f - 1.0f);
            p.vel = base + r * spread;
            break;
        }
        case ModuleKind::InitSize:
            p.size = m.get_float("SIZE", 0.25f);
            break;
        case ModuleKind::InitColor:
            p.color = m.get_vec4("COLOR", glm::vec4(1.0f));
            break;
        case ModuleKind::InitPositionShape: {
            // 0 = point, 1 = sphere, 2 = box. Point is the pre-4.3 behaviour.
            const float shape  = m.get_float("SHAPE", 0.0f);
            const float radius = m.get_float("RADIUS", 1.0f);
            if (shape >= 0.5f) {
                const glm::vec3 r(ctx_rand01(ctx) * 2.0f - 1.0f,
                                  ctx_rand01(ctx) * 2.0f - 1.0f,
                                  ctx_rand01(ctx) * 2.0f - 1.0f);
                p.pos += (shape >= 1.5f)
                    ? r * radius
                    : (glm::length(r) > 1e-5f
                           ? glm::normalize(r) * radius * ctx_rand01(ctx)
                           : glm::vec3(0.0f));
            }
            break;
        }
        case ModuleKind::Gravity:
            p.vel += m.get_vec3("GRAVITY", glm::vec3(0.0f, -9.8f, 0.0f)) * ctx.dt;
            break;
        case ModuleKind::Drag:
            p.vel *= std::max(0.0f, 1.0f - m.get_float("DRAG", 0.1f) * ctx.dt);
            break;
        case ModuleKind::VelocityOverLife:
            p.vel += m.get_vec3("ACCEL", glm::vec3(0.0f)) * ctx.dt;
            break;
        case ModuleKind::SizeOverLife: {
            const gws::anim::Curve fallback(std::vector<gws::anim::CurveKey>{
                {0.0f, 0.25f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}});
            p.size = m.get_curve("CURVE", fallback).evaluate(ctx.t);
            break;
        }
        case ModuleKind::ColorOverLife: {
            const gws::anim::Gradient fallback =
                gws::anim::Gradient::two_stop(glm::vec4(1.0f), glm::vec4(0.0f));
            p.color = m.get_gradient("GRADIENT", fallback).sample(ctx.t);
            break;
        }
        case ModuleKind::SpawnRate:
        case ModuleKind::SpawnBurst:
            break;   // Spawn modules produce counts, not particle edits.
    }
}

}  // namespace

void ParticleSystem::spawn_one() {
    if (particles_.size() >= graph_.max_particles) return;
    Particle p;
    p.pos = position_;
    p.age = 0.0f;
    Context ctx;
    ctx.dt = 0.0f;
    ctx.t  = 0.0f;
    ctx.emitter_pos = position_;
    ctx.rng = &rng_;
    for (const VfxModule& m : graph_.stage(VfxStage::Init)) apply_module(m, p, ctx);
    p.seed = ctx_rand01(ctx);
    particles_.push_back(p);
}

void ParticleSystem::emit_burst(int n) {
    for (int i = 0; i < n; ++i) spawn_one();
}

void ParticleSystem::update(float dt) {
    if (dt <= 0.0f) return;

    // ---- Spawn stage: how many particles this frame ----
    if (emitting_) {
        for (const VfxModule& m : graph_.stage(VfxStage::Spawn)) {
            if (m.kind == ModuleKind::SpawnRate) {
                spawn_accum_ += m.get_float("RATE", 50.0f) * rate_scale_ * dt;
                while (spawn_accum_ >= 1.0f) { spawn_one(); spawn_accum_ -= 1.0f; }
            } else if (m.kind == ModuleKind::SpawnBurst) {
                // Fires once, on the frame the emitter's clock crosses the
                // burst time. A >= test would re-fire it every frame after.
                const float at = m.get_float("TIME", 0.0f);
                if (elapsed_ <= at && at < elapsed_ + dt) {
                    const int n = static_cast<int>(m.get_float("COUNT", 10.0f) * rate_scale_);
                    for (int i = 0; i < n; ++i) spawn_one();
                }
            }
        }
    }
    elapsed_ += dt;

    // ---- Update stage ----
    // Age first so ctx.t is correct for the modules that read it, then run the
    // modules, then integrate position from the velocity they produced. That
    // ordering reproduces the pre-4.3 arithmetic exactly.
    const auto& update_modules = graph_.stage(VfxStage::Update);
    for (auto& p : particles_) {
        p.age += dt;
        Context ctx;
        ctx.dt = dt;
        ctx.t  = p.life > 0.0f ? glm::clamp(p.age / p.life, 0.0f, 1.0f) : 1.0f;
        ctx.emitter_pos = position_;
        ctx.rng = &rng_;
        for (const VfxModule& m : update_modules) apply_module(m, p, ctx);
        p.pos += p.vel * dt;
    }

    // Reap dead (swap-remove keeps it O(n)).
    particles_.erase(
        std::remove_if(particles_.begin(), particles_.end(),
                       [](const Particle& p) { return p.age >= p.life; }),
        particles_.end());
}

void ParticleSystem::apply_shift(const glm::vec3& delta) {
    if (!graph_.world_space) return;
    for (auto& p : particles_) p.pos += delta;
}

void ParticleSystem::build_billboards(const glm::vec3& cam_pos, const glm::vec3& cam_up,
                                      std::vector<BillboardVertex>& out,
                                      const glm::mat4& local_to_world) const {
    out.clear();
    out.reserve(particles_.size() * 4);
    for (const Particle& p : particles_) {
        const glm::vec3 world = graph_.world_space
            ? p.pos : glm::vec3(local_to_world * glm::vec4(p.pos, 1.0f));

        // Camera-facing basis: normal points from the particle toward the
        // camera; right/up span the quad plane. Works for ANY camera rig.
        glm::vec3 to_cam = cam_pos - world;
        const float d = glm::length(to_cam);
        const glm::vec3 normal = d > 1e-5f ? to_cam / d : glm::vec3(0, 0, 1);
        glm::vec3 right = glm::cross(cam_up, normal);
        if (glm::length(right) < 1e-5f) right = glm::vec3(1, 0, 0);
        right = glm::normalize(right);
        const glm::vec3 up = glm::normalize(glm::cross(normal, right));

        // Read state rather than recompute it: a GPU simulation writes these in
        // compute and the vertex stage only reads them, so the CPU path must
        // work the same way or the two would diverge.
        const float size = p.size * 0.5f;      // half-extent; p.size is full width
        const glm::vec4 color = p.color;

        const glm::vec3 r = right * size, u = up * size;
        const glm::vec3 c0 = world - r - u, c1 = world + r - u,
                        c2 = world + r + u, c3 = world - r + u;
        out.push_back({c0, {0, 0}, color});
        out.push_back({c1, {1, 0}, color});
        out.push_back({c2, {1, 1}, color});
        out.push_back({c3, {0, 1}, color});
    }
}

} // namespace schizo::vfx
