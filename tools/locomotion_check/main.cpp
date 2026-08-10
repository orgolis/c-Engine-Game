// ============================================================================
// locomotion_check — idle vs walk must not flicker.
//
// The rule looks trivial ("moving? play the walk clip") and its failure mode is
// entirely visual: a character standing still that twitches between idle and
// walk every frame, or one that keeps running after it stops. Neither errors,
// neither shows up in a log, and both are obvious the moment a human watches
// them — which is exactly the kind of thing that should be an assertion instead
// of a thing someone notices later.
//
// Speed comes from a position delta, so it is noisy by construction. That is
// what makes hysteresis necessary and what makes a single threshold wrong.
// ============================================================================
#include "locomotion_clip.h"

#include <cstdio>
#include <string>

using namespace schizo::editor;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

constexpr float kDt = 1.0f / 60.0f;
constexpr float kThreshold = 0.15f;

// Walk in a straight line at `speed` for `frames`, return the final decision.
bool walk(LocomotionState& st, float speed, int frames) {
    bool moving = st.moving;
    glm::vec3 p = st.has_last ? st.last_pos : glm::vec3(0.0f);
    for (int i = 0; i < frames; ++i) {
        p.x += speed * kDt;
        moving = locomotion_wants_move(st, p, kDt, kThreshold);
    }
    return moving;
}

}  // namespace

int main() {
    std::printf("=== locomotion_check ===\n\n");

    // ---- 1. the first frame cannot know ------------------------------------
    // There is no previous position, so any speed it reported would be invented.
    {
        LocomotionState st;
        const bool m = locomotion_wants_move(st, glm::vec3(0, 0, 0), kDt, kThreshold);
        check(!m, "the first frame reports idle rather than guessing a speed");
        check(st.has_last, "and records the position to measure against next time");
    }

    // ---- 2. clearly moving / clearly still ---------------------------------
    {
        LocomotionState st;
        check(walk(st, 3.0f, 10), "walking at 3 m/s selects the move clip");

        LocomotionState st2;
        walk(st2, 0.0f, 10);
        check(!st2.moving, "standing perfectly still selects the idle clip");
    }

    // ---- 3. THE ONE THAT MATTERS: no flicker at the threshold --------------
    // A character hovering at the threshold is the common case, not the edge
    // case: arriving at a waypoint decelerates through exactly this speed.
    {
        LocomotionState st;
        walk(st, 3.0f, 10);                       // definitely moving
        int flips = 0;
        bool prev = st.moving;
        glm::vec3 p = st.last_pos;
        // Hover just under the start threshold, where a single-threshold test
        // would toggle on tiny variations.
        for (int i = 0; i < 60; ++i) {
            const float jitter = (i % 2) ? 0.001f : -0.001f;
            p.x += (kThreshold * 0.9f + jitter) * kDt;
            const bool m = locomotion_wants_move(st, p, kDt, kThreshold);
            if (m != prev) ++flips;
            prev = m;
        }
        check(flips == 0, "hovering just below the start threshold does not flicker");
    }

    // ---- 4. hysteresis is asymmetric ---------------------------------------
    {
        LocomotionState st;
        // Below start: never begins moving.
        walk(st, kThreshold * 0.8f, 20);
        check(!st.moving, "speed below the start threshold does not begin the move clip");

        // Once moving, the same speed KEEPS it moving.
        LocomotionState st2;
        walk(st2, 3.0f, 10);
        const bool still = walk(st2, kThreshold * 0.8f, 20);
        check(still, "the same speed keeps a moving character moving (hysteresis)");

        // Well below stop: genuinely stops.
        const bool stopped = !walk(st2, 0.0f, 20);
        check(stopped, "coming to a real halt returns to idle");
    }

    // ---- 5. vertical motion is not walking ---------------------------------
    // Falling, terrain-following and rebasing all move Y. None mean "walking".
    {
        LocomotionState st;
        glm::vec3 p(0.0f);
        bool moving = false;
        for (int i = 0; i < 30; ++i) {
            p.y -= 9.8f * kDt;                    // free fall
            moving = locomotion_wants_move(st, p, kDt, kThreshold);
        }
        check(!moving, "falling straight down does not select the walk clip");
    }

    // ---- 6. a floating-origin rebase must not look like a sprint -----------
    // The entity teleports by up to 1024 units in one frame. Without adjusting
    // the remembered position that reads as an enormous speed and snaps every
    // character in the scene into its run cycle for a frame.
    {
        LocomotionState st;
        walk(st, 0.0f, 10);
        check(!st.moving, "standing still before the rebase");

        const glm::vec3 shift(-1024.0f, 0.0f, 512.0f);
        locomotion_apply_origin_shift(st, shift);
        const glm::vec3 after = st.last_pos;      // already shifted
        const bool m = locomotion_wants_move(st, after, kDt, kThreshold);
        check(!m, "a rebase does not register as movement");
        check(st.speed < 1.0f, "and does not report an absurd speed");
    }

    // ---- 7. dt <= 0 changes nothing ----------------------------------------
    // A paused editor frame must not be read as "infinitely fast".
    {
        LocomotionState st;
        walk(st, 3.0f, 10);
        const bool before = st.moving;
        const bool m = locomotion_wants_move(st, st.last_pos + glm::vec3(5, 0, 0), 0.0f, kThreshold);
        check(m == before, "a zero-length frame leaves the decision untouched");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL locomotion_check\n");
    return g_fail ? 1 : 0;
}
