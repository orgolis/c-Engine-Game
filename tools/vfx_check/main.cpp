// ====================
// vfx_check — headless verification of the particle/VFX system (VFX pillar)
// ====================
//
//   1. Spawn-by-rate: update at a known rate emits the expected particle count.
//   2. Integration: gravity + drag move a particle deterministically.
//   3. Life: particles fade and are reaped when age >= life.
//   4. Burst: emit_burst adds particles immediately.
//   5. Floating-origin: apply_shift moves every world-space particle (rebase).
//   6. Billboards: quads are camera-facing (normal points at the camera) and
//      planar/perpendicular to the view direction — for ANY camera position.
//
// Pure CPU. Prints "vfx_check: ALL OK" + exits 0 on pass.

#include "vfx/particle_system.h"

#include <glm/glm.hpp>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace schizo::vfx;

namespace {
int g_checks = 0;
bool check(bool ok, const char* what) {
    ++g_checks;
    std::printf("  [%s] %s\n", ok ? "OK" : "FAIL", what);
    return ok;
}
#define REQUIRE(msg, ...) do { if (!check((__VA_ARGS__), (msg))) { \
    std::printf("vfx_check: FAILED at '%s'\n", (msg)); return 1; } } while (0)
bool near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) < e; }
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== vfx_check: particle system ===\n");

    // ---- 1. Spawn by rate --------------------------------------------------
    {
        EmitterConfig cfg;
        cfg.spawn_rate = 100.0f;      // 100 / sec
        cfg.life_min = cfg.life_max = 10.0f;   // long life so none die yet
        cfg.gravity = glm::vec3(0);
        ParticleSystem ps(cfg);
        ps.set_seed(42);
        for (int i = 0; i < 100; ++i) ps.update(0.01f);   // 1.0s total
        REQUIRE("spawn-by-rate emits ~rate*time particles",
                ps.alive() >= 99 && ps.alive() <= 101);
    }

    // ---- 2. Deterministic integration (gravity + drag) ---------------------
    {
        EmitterConfig cfg;
        cfg.spawn_rate = 0.0f;                 // no auto spawn
        cfg.base_velocity = glm::vec3(0.0f, 10.0f, 0.0f);
        cfg.velocity_spread = 0.0f;            // no randomness → exact
        cfg.gravity = glm::vec3(0.0f, -10.0f, 0.0f);
        cfg.drag = 0.0f;
        cfg.life_min = cfg.life_max = 100.0f;
        ParticleSystem ps(cfg);
        ps.set_position(glm::vec3(0.0f));
        ps.emit_burst(1);
        REQUIRE("burst emits one", ps.alive() == 1);
        // One 0.1s step: v += g*dt → 10-1=9 ; pos += v*dt → 0.9
        ps.update(0.1f);
        const Particle& p = ps.particles()[0];
        REQUIRE("velocity integrates under gravity", near(p.vel.y, 9.0f));
        REQUIRE("position integrates from velocity", near(p.pos.y, 0.9f));
    }

    // ---- 3. Life fade + reap ----------------------------------------------
    {
        EmitterConfig cfg;
        cfg.spawn_rate = 0.0f;
        cfg.life_min = cfg.life_max = 0.5f;
        cfg.gravity = glm::vec3(0);
        ParticleSystem ps(cfg);
        ps.emit_burst(5);
        REQUIRE("5 particles alive", ps.alive() == 5);
        ps.update(0.4f);
        REQUIRE("still alive before life elapses", ps.alive() == 5);
        ps.update(0.2f);   // total 0.6 > 0.5
        REQUIRE("reaped once past life", ps.alive() == 0);
    }

    // ---- 4. Floating-origin rebase ----------------------------------------
    {
        EmitterConfig cfg;
        cfg.spawn_rate = 0.0f;
        cfg.velocity_spread = 0.0f;
        cfg.base_velocity = glm::vec3(0);
        cfg.gravity = glm::vec3(0);
        cfg.life_min = cfg.life_max = 100.0f;
        cfg.world_space = true;
        ParticleSystem ps(cfg);
        ps.set_position(glm::vec3(1000.0f, 0.0f, 0.0f));
        ps.emit_burst(3);
        const glm::vec3 shift(-1024.0f, 0.0f, 512.0f);
        ps.apply_shift(shift);
        bool all_shifted = true;
        for (const Particle& p : ps.particles())
            if (!near(p.pos.x, 1000.0f - 1024.0f) || !near(p.pos.z, 512.0f))
                all_shifted = false;
        REQUIRE("floating-origin rebase shifts every world particle", all_shifted);
    }

    // ---- 5. Camera-facing billboards --------------------------------------
    {
        EmitterConfig cfg;
        cfg.spawn_rate = 0.0f;
        cfg.velocity_spread = 0.0f;
        cfg.base_velocity = glm::vec3(0);
        cfg.gravity = glm::vec3(0);
        cfg.life_min = cfg.life_max = 100.0f;
        cfg.size_start = cfg.size_end = 2.0f;   // constant size for the check
        ParticleSystem ps(cfg);
        ps.set_position(glm::vec3(0.0f, 0.0f, 0.0f));
        ps.emit_burst(1);

        std::vector<BillboardVertex> quads;

        // Camera off to the side + above, so a naive XY quad would NOT face it.
        const glm::vec3 cam_pos(5.0f, 4.0f, -3.0f);
        const glm::vec3 cam_up(0.0f, 1.0f, 0.0f);
        ps.build_billboards(cam_pos, cam_up, quads);
        REQUIRE("one particle → 4 billboard verts", quads.size() == 4);

        // Quad corners → plane normal (cross of two edges).
        const glm::vec3 e1 = quads[1].pos - quads[0].pos;
        const glm::vec3 e2 = quads[3].pos - quads[0].pos;
        glm::vec3 n = glm::normalize(glm::cross(e1, e2));
        const glm::vec3 center = 0.25f * (quads[0].pos + quads[1].pos +
                                          quads[2].pos + quads[3].pos);
        const glm::vec3 to_cam = glm::normalize(cam_pos - center);
        // Normal is (anti)parallel to the camera direction → the quad faces it.
        REQUIRE("billboard quad faces the camera",
                near(std::fabs(glm::dot(n, to_cam)), 1.0f, 1e-3f));

        // All four corners are coplanar & equidistant from centre (a square).
        const float r0 = glm::length(quads[0].pos - center);
        bool square = true;
        for (int i = 1; i < 4; ++i)
            if (!near(glm::length(quads[i].pos - center), r0, 1e-3f)) square = false;
        REQUIRE("billboard is a centred square", square);

        // Move the camera to the opposite side — the quad re-orients to face it.
        const glm::vec3 cam2(-6.0f, -2.0f, 8.0f);
        ps.build_billboards(cam2, cam_up, quads);
        const glm::vec3 c2 = 0.25f * (quads[0].pos + quads[1].pos +
                                      quads[2].pos + quads[3].pos);
        glm::vec3 n2 = glm::normalize(glm::cross(quads[1].pos - quads[0].pos,
                                                 quads[3].pos - quads[0].pos));
        const glm::vec3 to_cam2 = glm::normalize(cam2 - c2);
        REQUIRE("billboard re-faces a moved camera (any camera rig)",
                near(std::fabs(glm::dot(n2, to_cam2)), 1.0f, 1e-3f));
    }

    std::printf("vfx_check: ALL OK (%d checks)\n", g_checks);
    return 0;
}
