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
#include "vfx/effects.h"

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

    // ---- Effects: the data, the built-in set, bursts, streams, quads -------
    {
        const auto defs = parse_effects_text(
            "# guns\n"
            "effect t_muzzle\n"
            "  layer flash count 2 life 0.05 size 0.3 0.1 color 5 3 1 1 to 2 1 0.2 0\n"
            "  layer smoke count 3 life 1 speed 1 2 spread 30 size 0.1 0.5 color 0.5 0.5 0.5 0.4 blend gravity -1 delay 0.1\n"
            "end\n"
            "ability x\n  effect Speed -2 3\nend\n"
            "effect t_trail\n  layer core rate 100 life 0.5 size 0.2\nend\n"
            "effect t_tracer\n  layer round count 1 reach speed 100 size 0.05 streak 0.02\nend\n");
        REQUIRE("effect blocks parse (an ability's effect line is not one)",
                defs.size() == 3 && defs[0].id == "t_muzzle" && defs[0].layers.size() == 2 &&
                defs[0].layers[1].blend && near(defs[0].layers[1].delay, 0.1f) && near(defs[0].layers[1].gravity, -1.0f) &&
                defs[0].layers[0].count == 2 && near(defs[0].layers[0].color_end.a, 0.0f) &&
                near(defs[0].layers[1].color_end.a, 0.0f) && near(defs[0].layers[1].color_end.r, 0.5f));
        for (const auto& d : defs) register_effect(d);
        const char* stages[] = {"windup", "cast", "trail", "beam", "hit", "area"};
        const char* elements[] = {"fire", "cold", "shock", "poison", "physical", "explosive", "magic"};
        bool all = find_effect("muzzle") && find_effect("tracer") && find_effect("impact") && find_effect("impact_actor") &&
                   find_effect("heal_self") && find_effect("heal_windup") && find_effect("magic_self");
        for (const char* e : elements)
            for (const char* s : stages) all = all && find_effect(std::string(e) + "_" + s) != nullptr;
        REQUIRE("the built-in set has every gun and element stage", all);
        REQUIRE("a project's effect of a built-in name replaces it",
                (register_effect(EffectDef{"impact", {}}), find_effect("impact")->layers.empty()));
        REQUIRE("elements from damage types", effect_element("fire", false) == "fire" && effect_element("frost", false) == "cold" &&
                effect_element("kinetic", false) == "physical" && effect_element("", true) == "heal" &&
                effect_element("arcane", false) == "magic");

        EffectParticles fx;
        EffectSpawn at;
        at.pos = glm::vec3(1, 2, 3);
        fx.burst(*find_effect("t_muzzle"), at);
        const size_t first = fx.alive();
        fx.update(0.06f);   // the flash is gone, the smoke not yet born
        const size_t mid = fx.alive();
        fx.update(0.06f);
        REQUIRE("a burst: its layers now, a delayed one later, each gone after its life",
                first == 2 && mid == 0 && fx.alive() == 3);
        std::vector<BillboardVertex> quads;
        fx.build(glm::vec3(0, 2, 10), quads);
        bool covering = quads.size() == 12;
        for (const auto& v : quads) covering = covering && v.color.a < 0.0f;
        REQUIRE("quads, four corners each; a covering (blend) particle is marked by a negative alpha", covering);

        EffectParticles st;
        st.stream(*find_effect("t_trail"), glm::vec3(0), glm::vec3(0, 0, -10), 0.1f);
        REQUIRE("a stream: its rate for the time, along the path", st.alive() == 10);

        EffectParticles tr;
        at.reach = 50.0f;
        at.dir = glm::vec3(1, 0, 0);
        tr.burst(*find_effect("t_tracer"), at);
        tr.update(0.25f);
        const size_t flying = tr.alive();
        tr.update(0.3f);
        std::vector<BillboardVertex> one;
        EffectParticles tr2;
        tr2.burst(*find_effect("t_tracer"), at);
        tr2.update(0.01f);
        tr2.build(glm::vec3(0, 50, 0), one);
        const float len = one.size() == 4 ? glm::length(one[2].pos - one[1].pos) : 0.0f;   // along its motion
        const float wid = one.size() == 4 ? glm::length(one[1].pos - one[0].pos) : 0.0f;
        REQUIRE("a tracer lives until it has reached the end, drawn stretched along its flight",
                flying == 1 && tr.alive() == 0 && len > wid * 10.0f);
    }

    std::printf("vfx_check: ALL OK (%d checks)\n", g_checks);
    return 0;
}
