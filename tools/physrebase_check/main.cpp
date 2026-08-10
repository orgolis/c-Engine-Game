// ============================================================================
// physrebase_check — the simulation survives a floating-origin rebase.
//
// A rebase moves the whole scene while play mode is running. The Jolt world
// knows nothing about it, so its bodies stay at their old coordinates and the
// per-step write-back then stamps those old positions back onto the entities:
// the dynamic half of the level snaps to the previous origin while the static
// half moves with the rebase, and the level visibly tears in two. The player is
// a CharacterVirtual rather than a rigid body, so it is not even in the body
// list -- without special handling it is the one thing left behind.
//
// The property under test is that a rebase is a change of coordinates and
// NOTHING ELSE: after it, every relative position is identical, velocities are
// unchanged, contacts still hold, and stepping continues as if nothing had
// happened. Anything else means the rebase perturbed the simulation, which over
// a long session is indistinguishable from physics being unreliable.
// ============================================================================
#include "physics/jolt_physics.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace schizo::physics;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

bool near(const glm::vec3& a, const glm::vec3& b, float eps) {
    return glm::length(a - b) < eps;
}

}  // namespace

int main() {
    std::printf("=== physrebase_check ===\n\n");

    // The magnitude the streaming manager actually produces.
    const glm::vec3 kShift(-512.0f, 0.0f, 256.0f);
    const float dt = 1.0f / 66.0f;

    PhysicsWorld w;
    w.init(glm::vec3(0.0f, -9.81f, 0.0f));

    BodyDesc floor;
    floor.shape        = ShapeType::Box;
    floor.half_extents = glm::vec3(50.0f, 0.5f, 50.0f);
    floor.position     = glm::vec3(0.0f, 0.0f, 0.0f);
    floor.motion       = MotionType::Static;
    const BodyId floor_id = w.add_body(floor);

    BodyDesc box;
    box.shape        = ShapeType::Box;
    box.half_extents = glm::vec3(0.5f);
    box.position     = glm::vec3(0.0f, 8.0f, 0.0f);
    box.motion       = MotionType::Dynamic;
    const BodyId box_id = w.add_body(box);

    // Away from the box: CharacterDesc defaults to (0,1,0), which is exactly
    // where the box comes to rest -- they collide and shove each other out of
    // the scene being measured.
    CharacterDesc cd;
    cd.position = glm::vec3(10.0f, 2.0f, 10.0f);
    const CharacterId ch = w.add_character(cd);

    // Let it fall and settle so there is a real resting contact to preserve.
    for (int i = 0; i < 180; ++i) {
        w.update_character(ch, glm::vec3(0.0f), dt);
        w.step(dt);
    }

    const BodyState rest_box   = w.get_state(box_id);
    const BodyState rest_floor = w.get_state(floor_id);
    const glm::vec3 rest_char  = w.character_position(ch);

    // Floor top is y=0.5, box half-extent 0.5, so a settled box sits at y=1.
    std::printf("  ..   box rests at y=%.3f, character at y=%.3f\n",
                rest_box.position.y, rest_char.y);
    check(std::abs(rest_box.position.y - 1.0f) < 0.05f,
          "a dropped box has settled on the floor before the rebase");

    // ---- 1. everything moves, and moves by exactly the shift ---------------
    {
        w.translate_world(kShift);

        check(near(w.get_state(box_id).position, rest_box.position + kShift, 1e-3f),
              "the dynamic body moved by exactly the shift");
        check(near(w.get_state(floor_id).position, rest_floor.position + kShift, 1e-3f),
              "so did the static floor — statics are not skipped");
        check(near(w.character_position(ch), rest_char + kShift, 1e-3f),
              "and the player character, which is not a rigid body and not in "
              "the body list at all");
    }

    // ---- 2. it is a change of coordinates, not a change of state -----------
    {
        check(near(w.get_state(box_id).linear_velocity, rest_box.linear_velocity, 1e-4f),
              "velocities are untouched — a rigid translation preserves them");

        const glm::vec3 before = rest_box.position - rest_floor.position;
        const glm::vec3 after  = w.get_state(box_id).position - w.get_state(floor_id).position;
        check(near(before, after, 1e-3f),
              "and every relative position is identical, to the millimetre");
    }

    // ---- 3. the contact survives --------------------------------------------
    // The real risk: a body moved without its broadphase proxy following would
    // lose its contact and drop through the floor on the next step.
    {
        const float y_before = w.get_state(box_id).position.y;
        for (int i = 0; i < 120; ++i) {
            w.update_character(ch, glm::vec3(0.0f), dt);
            w.step(dt);
        }
        const float y_after = w.get_state(box_id).position.y;
        check(std::abs(y_after - y_before) < 0.05f,
              "the box is still resting 120 steps later — it did not fall "
              "through the floor that moved with it");
        // Weaker than it looks, and worth saying so: update_character with a
        // zero velocity applies no gravity, so the character holds its height
        // regardless. What this catches is a character DISPLACED by the rebase
        // -- pushed out by a contact that resolved wrongly across the shift.
        check(std::abs(w.character_position(ch).y - (rest_char.y + kShift.y)) < 0.05f,
              "and the character kept its height through the rebase and after");
    }

    // ---- 4. the simulation is the same simulation ---------------------------
    // The strongest form of the property: run the same scene from scratch
    // WITHOUT a rebase, and with one halfway through. The results must agree
    // once expressed in the same frame. If a rebase perturbs anything, this is
    // where it shows.
    {
        auto build = [&](PhysicsWorld& p) {
            p.init(glm::vec3(0.0f, -9.81f, 0.0f));
            BodyDesc f = floor; p.add_body(f);
            BodyDesc b; b.shape = ShapeType::Box; b.half_extents = glm::vec3(0.5f);
            b.position = glm::vec3(0.3f, 6.0f, -0.2f);        // off-centre: it topples
            b.motion = MotionType::Dynamic;
            return p.add_body(b);
        };

        PhysicsWorld plain, rebased;
        const BodyId pb = build(plain);
        const BodyId rb = build(rebased);

        for (int i = 0; i < 200; ++i) {
            plain.step(dt);
            rebased.step(dt);
            if (i == 100) rebased.translate_world(kShift);   // mid-flight
        }

        const glm::vec3 a = plain.get_state(pb).position;
        const glm::vec3 b = rebased.get_state(rb).position - kShift;
        check(near(a, b, 0.02f),
              "a run with a rebase halfway through lands where a run without "
              "one does — the rebase changed the coordinates and nothing else");
    }

    // ---- 5. shifting back is exact -----------------------------------------
    // Rebases accumulate over a session; drift here would compound silently.
    {
        const glm::vec3 p0 = w.get_state(box_id).position;
        w.translate_world(-kShift);
        w.translate_world(kShift);
        check(near(w.get_state(box_id).position, p0, 1e-3f),
              "out and back leaves the body where it was (rebases accumulate)");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL physrebase_check\n");
    return g_fail ? 1 : 0;
}
