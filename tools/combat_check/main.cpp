// ====================
// combat_check — headless verification of the frame-data combat substrate
// ====================
//
//   1. Frame windows: the hitbox is live ONLY during the active window
//      [startup, startup+active), and states step Startup→Active→Recovery→Idle.
//   2. Overlap + dedup: an overlapping single-hit attack lands exactly once.
//   3. Range: a target outside reach is never hit.
//   4. Facing: the hitbox rotates with the actor's yaw (whiff vs hit).
//   5. Dodge i-frames: an invulnerable target takes no damage; control does.
//   6. Parry: a parried hit deals no damage and punishes (staggers) the attacker.
//   7. Multi-hit: a multi-hit attack lands on its interval ticks.
//   8. Poise break: enough poise damage staggers a target and interrupts its move.
//   9. Floating-origin: shifting BOTH actors equally preserves the hit (camera
//      rebase consistency — combat works under any player camera / large world).
//
// Pure CPU. Prints "combat_check: ALL OK" + exits 0 on pass.

#include "combat/combat.h"

#include <glm/glm.hpp>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace schizo::combat;

namespace {
int g_checks = 0;
bool check(bool ok, const char* what) {
    ++g_checks;
    std::printf("  [%s] %s\n", ok ? "OK" : "FAIL", what);
    return ok;
}
#define REQUIRE(msg, ...) do { if (!check((__VA_ARGS__), (msg))) { \
    std::printf("combat_check: FAILED at '%s'\n", (msg)); return 1; } } while (0)
bool near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) < e; }

AttackFrameData basic_attack() {
    AttackFrameData a;
    a.startup = 6; a.active = 4; a.recovery = 10;
    a.hitbox_offset = glm::vec3(0, 1, 1.3f);
    a.hitbox_radius = 0.7f;
    a.damage = 25.0f; a.poise_damage = 30.0f;
    return a;
}
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== combat_check: frame-data combat ===\n");

    // ---- 1. Frame windows --------------------------------------------------
    {
        CombatActor a(1);
        REQUIRE("starts Idle", a.state() == CombatState::Idle);
        REQUIRE("start_attack accepted from Idle", a.start_attack(basic_attack()));
        REQUIRE("can't start a second attack mid-move", !a.start_attack(basic_attack()));

        int active_frames = 0, first_active = -1, last_active = -1;
        for (int f = 1; f <= 20; ++f) {
            a.advance_frame();
            if (a.hitbox_active()) {
                ++active_frames;
                if (first_active < 0) first_active = f;
                last_active = f;
            }
        }
        REQUIRE("hitbox live for exactly `active` frames", active_frames == 4);
        REQUIRE("active window begins after startup", first_active == 7);
        REQUIRE("active window ends at startup+active", last_active == 10);
        a.advance_frame();   // frame 21 → past total(20)
        REQUIRE("returns to Idle after recovery", a.state() == CombatState::Idle);
    }

    // ---- 2. Overlap + dedup ------------------------------------------------
    {
        CombatActor atk(1, 100.0f), tgt(2, 100.0f);
        atk.set_position({0, 0, 0}); atk.set_facing_yaw(0.0f);
        tgt.set_position({0, 0, 1.3f});   // hurtbox coincident with the hitbox
        std::vector<CombatActor*> world{&atk, &tgt};
        atk.start_attack(basic_attack());

        int total_hits = 0;
        for (int f = 0; f < 20; ++f) {
            atk.advance_frame(); tgt.advance_frame();
            auto hits = resolve_combat(world);
            total_hits += static_cast<int>(hits.size());
        }
        REQUIRE("single-hit attack lands exactly once (dedup)", total_hits == 1);
        REQUIRE("target lost one hit of health", near(tgt.health(), 75.0f));
    }

    // ---- 3. Out of range ---------------------------------------------------
    {
        CombatActor atk(1), tgt(2);
        atk.set_position({0, 0, 0});
        tgt.set_position({0, 0, 10.0f});   // far away
        std::vector<CombatActor*> world{&atk, &tgt};
        atk.start_attack(basic_attack());
        int total = 0;
        for (int f = 0; f < 20; ++f) {
            atk.advance_frame(); tgt.advance_frame();
            total += static_cast<int>(resolve_combat(world).size());
        }
        REQUIRE("no hit when the target is out of reach", total == 0);
        REQUIRE("out-of-range target keeps full health", near(tgt.health(), 100.0f));
    }

    // ---- 4. Facing (hitbox rotates with yaw) -------------------------------
    {
        // Target on the actor's +X side. Facing +Z (yaw 0) whiffs; yaw +90° hits.
        auto run = [](float yaw) {
            CombatActor atk(1), tgt(2);
            atk.set_position({0, 0, 0}); atk.set_facing_yaw(yaw);
            tgt.set_position({1.3f, 0, 0});
            std::vector<CombatActor*> world{&atk, &tgt};
            atk.start_attack(basic_attack());
            int total = 0;
            for (int f = 0; f < 20; ++f) {
                atk.advance_frame(); tgt.advance_frame();
                total += static_cast<int>(resolve_combat(world).size());
            }
            return total;
        };
        REQUIRE("facing +Z whiffs a target to the side", run(0.0f) == 0);
        REQUIRE("yaw rotates the hitbox onto the side target",
                run(1.5707963f) == 1);
    }

    // ---- 5. Dodge i-frames -------------------------------------------------
    {
        CombatActor atk(1), tgt(2, 100.0f);
        atk.set_position({0, 0, 0});
        tgt.set_position({0, 0, 1.3f});
        tgt.start_dodge(12);   // i-frames span the whole startup+active window
        std::vector<CombatActor*> world{&atk, &tgt};
        atk.start_attack(basic_attack());
        int hits = 0;
        for (int f = 0; f < 20; ++f) {
            atk.advance_frame(); tgt.advance_frame();
            hits += static_cast<int>(resolve_combat(world).size());
        }
        REQUIRE("dodge i-frames avoid the hit entirely", hits == 0);
        REQUIRE("invulnerable target takes no damage", near(tgt.health(), 100.0f));
    }

    // ---- 6. Parry ----------------------------------------------------------
    {
        CombatActor atk(1), tgt(2, 100.0f);
        atk.set_position({0, 0, 0});
        tgt.set_position({0, 0, 1.3f});
        tgt.start_parry(12);   // parry window covers the active frames
        std::vector<CombatActor*> world{&atk, &tgt};
        atk.start_attack(basic_attack());
        bool saw_parry = false;
        for (int f = 0; f < 20; ++f) {
            atk.advance_frame(); tgt.advance_frame();
            for (const CombatHit& h : resolve_combat(world))
                if (h.parried) saw_parry = true;
        }
        REQUIRE("a parried hit is flagged parried", saw_parry);
        REQUIRE("parry deals no damage", near(tgt.health(), 100.0f));
        REQUIRE("parry punishes the attacker (stagger)", atk.staggered());
    }

    // ---- 7. Multi-hit ------------------------------------------------------
    {
        AttackFrameData atk_def = basic_attack();
        atk_def.multi_hit = true;
        atk_def.multi_hit_interval = 2;   // active frames 1,3 → 2 landings
        atk_def.damage = 10.0f;
        atk_def.poise_damage = 5.0f;      // never breaks poise here
        CombatActor atk(1), tgt(2, 100.0f);
        atk.set_position({0, 0, 0});
        tgt.set_position({0, 0, 1.3f});
        std::vector<CombatActor*> world{&atk, &tgt};
        atk.start_attack(atk_def);
        int hits = 0;
        for (int f = 0; f < 20; ++f) {
            atk.advance_frame(); tgt.advance_frame();
            hits += static_cast<int>(resolve_combat(world).size());
        }
        REQUIRE("multi-hit lands on its interval ticks", hits == 2);
        REQUIRE("multi-hit applies each landing's damage", near(tgt.health(), 80.0f));
    }

    // ---- 8. Poise break interrupts the target's own attack -----------------
    {
        AttackFrameData heavy = basic_attack();
        heavy.poise_damage = 130.0f;   // one hit breaks a 100-poise target
        CombatActor atk(1), tgt(2, 200.0f);
        atk.set_position({0, 0, 0});
        tgt.set_position({0, 0, 1.3f}); tgt.set_facing_yaw(3.14159f);  // faces back at atk
        std::vector<CombatActor*> world{&atk, &tgt};
        atk.start_attack(heavy);
        tgt.start_attack(basic_attack());   // target is mid-swing…
        bool staggered_and_interrupted = false;
        for (int f = 0; f < 20; ++f) {
            atk.advance_frame(); tgt.advance_frame();
            auto hits = resolve_combat(world);
            if (!hits.empty() && tgt.staggered() && !tgt.attacking()) {
                staggered_and_interrupted = true; break;
            }
        }
        REQUIRE("poise break staggers the target and interrupts its move",
                staggered_and_interrupted);
    }

    // ---- 9. Floating-origin / camera-rebase consistency --------------------
    {
        CombatActor atk(1, 100.0f), tgt(2, 100.0f);
        atk.set_position({0, 0, 0}); tgt.set_position({0, 0, 1.3f});
        // Simulate a floating-origin rebase: shift EVERY actor (and the camera)
        // by the same delta. Relative geometry — and thus the hit — is preserved.
        const glm::vec3 shift(-4096.0f, 0.0f, 2048.0f);
        atk.translate(shift); tgt.translate(shift);
        std::vector<CombatActor*> world{&atk, &tgt};
        atk.start_attack(basic_attack());
        int hits = 0;
        for (int f = 0; f < 20; ++f) {
            atk.advance_frame(); tgt.advance_frame();
            hits += static_cast<int>(resolve_combat(world).size());
        }
        REQUIRE("hit still lands after an equal rebase of both actors", hits == 1);
        REQUIRE("actors carry the rebase shift", near(atk.position().x, -4096.0f));
    }

    std::printf("combat_check: ALL OK (%d checks)\n", g_checks);
    return 0;
}
