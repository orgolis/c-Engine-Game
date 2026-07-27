// ============================================================================
// particle_system.cpp — CPU particle sim + camera-facing billboards. See header.
// ============================================================================

#include "vfx/particle_system.h"
#include <algorithm>
#include <cmath>

namespace schizo::vfx {

float ParticleSystem::rand01() {
    // xorshift32 — deterministic given the seed (reproducible tests).
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return static_cast<float>(rng_ & 0xFFFFFF) / static_cast<float>(0x1000000);
}

void ParticleSystem::spawn_one() {
    if (particles_.size() >= cfg_.max_particles) return;
    Particle p;
    p.pos  = position_;
    p.life = cfg_.life_min + (cfg_.life_max - cfg_.life_min) * rand01();
    p.age  = 0.0f;
    p.seed = rand01();
    // Random cone velocity: base + spread * random unit-ish vector.
    const glm::vec3 r(rand01() * 2.0f - 1.0f, rand01() * 2.0f - 1.0f,
                      rand01() * 2.0f - 1.0f);
    p.vel = cfg_.base_velocity + r * cfg_.velocity_spread;
    particles_.push_back(p);
}

void ParticleSystem::emit_burst(int n) {
    for (int i = 0; i < n; ++i) spawn_one();
}

void ParticleSystem::update(float dt) {
    if (dt <= 0.0f) return;
    // Spawn by rate.
    if (emitting_) {
        spawn_accum_ += cfg_.spawn_rate * dt;
        while (spawn_accum_ >= 1.0f) { spawn_one(); spawn_accum_ -= 1.0f; }
    }
    // Integrate + age.
    const float damp = std::max(0.0f, 1.0f - cfg_.drag * dt);
    for (auto& p : particles_) {
        p.vel += cfg_.gravity * dt;
        p.vel *= damp;
        p.pos += p.vel * dt;
        p.age += dt;
    }
    // Reap dead (swap-remove keeps it O(n)).
    particles_.erase(
        std::remove_if(particles_.begin(), particles_.end(),
                       [](const Particle& p) { return p.age >= p.life; }),
        particles_.end());
}

void ParticleSystem::apply_shift(const glm::vec3& delta) {
    if (!cfg_.world_space) return;
    for (auto& p : particles_) p.pos += delta;
}

void ParticleSystem::build_billboards(const glm::vec3& cam_pos, const glm::vec3& cam_up,
                                      std::vector<BillboardVertex>& out,
                                      const glm::mat4& local_to_world) const {
    out.clear();
    out.reserve(particles_.size() * 4);
    for (const Particle& p : particles_) {
        const glm::vec3 world = cfg_.world_space
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

        const float t = p.life > 0.0f ? glm::clamp(p.age / p.life, 0.0f, 1.0f) : 1.0f;
        const float size = glm::mix(cfg_.size_start, cfg_.size_end, t) * 0.5f;
        const glm::vec4 color = glm::mix(cfg_.color_start, cfg_.color_end, t);

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
