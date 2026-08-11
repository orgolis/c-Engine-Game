// ============================================================================
// snap_check — snapping rounds the way a level designer expects.
//
// The rounding rules are the part that can be subtly wrong. Snapping negatives
// the wrong way, or drifting when a drag is applied repeatedly, is invisible on
// any single drag and obvious after twenty — by which point the symptom is
// "the walls don't meet" and the cause is three files away.
//
// So the assertions that matter here are about negatives, idempotence and the
// exact halfway case, not about whether 1.1 snaps to 1.0.
// ============================================================================
#include "snapping.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace schizo::editor;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

bool near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) < eps; }
bool near3(const glm::vec3& a, const glm::vec3& b) { return glm::length(a - b) < 1e-4f; }

}  // namespace

int main() {
    std::printf("=== snap_check ===\n\n");

    // ---- 1. the basics --------------------------------------------------------
    {
        check(near(snap_to(1.1f, 1.0f), 1.0f), "1.1 with step 1 -> 1");
        check(near(snap_to(1.6f, 1.0f), 2.0f), "1.6 -> 2");
        check(near(snap_to(0.3f, 0.25f), 0.25f), "0.3 with step 0.25 -> 0.25");
        check(near(snap_to(0.4f, 0.25f), 0.5f), "0.4 -> 0.5");
    }

    // ---- 2. NEGATIVES round symmetrically ------------------------------------
    // A truncating cast rounds toward zero, so -1.6 would land on -1 while +1.6
    // lands on +2. Geometry laid out across the origin then comes out subtly
    // asymmetric — the sort of error nobody thinks to look for.
    {
        check(near(snap_to(-1.6f, 1.0f), -2.0f),
              "-1.6 -> -2, matching +1.6 -> +2 (a truncating cast gives -1)");
        check(near(snap_to(-1.1f, 1.0f), -1.0f), "-1.1 -> -1");
        check(near(snap_to(-0.3f, 0.25f), -0.25f), "-0.3 with step 0.25 -> -0.25");
        check(std::abs(snap_to(-2.5f, 1.0f)) == std::abs(snap_to(2.5f, 1.0f)),
              "the halfway case is symmetric about zero");
    }

    // ---- 3. IDEMPOTENT --------------------------------------------------------
    // A drag applies snapping every frame. If snapping a snapped value moved
    // it, an object would creep across the level for as long as the mouse is
    // held down, and the drift would look like a physics problem.
    {
        const float once  = snap_to(3.7f, 0.25f);
        const float twice = snap_to(once, 0.25f);
        check(near(once, twice), "snapping a snapped value does not move it");

        glm::vec3 p(1.13f, -4.67f, 9.02f);
        glm::vec3 a = snap_position(p, 0.5f);
        for (int i = 0; i < 50; ++i) a = snap_position(a, 0.5f);
        check(near3(a, snap_position(p, 0.5f)),
              "and fifty applications land in exactly the same place");
    }

    // ---- 4. step <= 0 means no snapping ---------------------------------------
    // Callers pass a user setting straight through; a zero step must be a
    // no-op rather than a divide by zero.
    {
        check(near(snap_to(1.234f, 0.0f), 1.234f), "a step of 0 leaves the value alone");
        check(near(snap_to(1.234f, -1.0f), 1.234f), "and so does a negative step");
        check(near3(snap_position(glm::vec3(1.1f, 2.2f, 3.3f), 0.0f),
                    glm::vec3(1.1f, 2.2f, 3.3f)),
              "positions too");
    }

    // ---- 5. relative snapping keeps off-grid layouts intact -------------------
    // This is what makes snapping usable on a level that was not authored on
    // the grid. Snapping to world zero would drag an artist's whole layout onto
    // a grid it was never on; they want the SPACING exact, not the coordinate.
    {
        const glm::vec3 origin(0.13f, 0.0f, -7.77f);      // deliberately off-grid
        const glm::vec3 p = origin + glm::vec3(1.02f, 0.0f, 0.0f);
        const glm::vec3 s = snap_position_relative(p, origin, 1.0f);
        check(near3(s, origin + glm::vec3(1.0f, 0.0f, 0.0f)),
              "relative snapping moves exactly one step from an off-grid origin");
        check(!near3(s, snap_position(p, 1.0f)),
              "which is NOT what absolute snapping would have done");
        check(near3(snap_position_relative(origin, origin, 1.0f), origin),
              "and the origin itself never moves");
    }

    // ---- 6. angles ------------------------------------------------------------
    {
        check(near(snap_angle_deg(43.0f, 15.0f), 45.0f), "43 degrees -> 45 with a 15 step");
        check(near(snap_angle_deg(-43.0f, 15.0f), -45.0f), "and -43 -> -45");
        check(near(snap_angle_deg(7.0f, 15.0f), 0.0f), "7 -> 0");

        const glm::quat q = glm::quat(glm::radians(glm::vec3(0.0f, 43.0f, 0.0f)));
        const glm::quat s = snap_rotation(q, 15.0f);
        const glm::vec3 e = glm::degrees(glm::eulerAngles(s));
        check(near(e.y, 45.0f, 0.01f), "a rotation snaps to the nearest 15 degrees");
    }

    // ---- 7. scale never snaps to zero -----------------------------------------
    // A zero scale collapses the mesh to a point, gives a degenerate collider,
    // and cannot be dragged back out because every later multiply is zero.
    {
        const glm::vec3 tiny(0.02f, 0.04f, 0.01f);
        const glm::vec3 s = snap_scale(tiny, 0.1f);
        check(s.x > 0.0f && s.y > 0.0f && s.z > 0.0f,
              "a scale that would round to zero is floored instead");
        check(near(s.x, 0.1f), "at one full step, not at epsilon — recovering "
                               "from 'very slightly there' is nearly as bad");
        check(near3(snap_scale(glm::vec3(1.04f, 2.0f, 0.97f), 0.1f),
                    glm::vec3(1.0f, 2.0f, 1.0f)),
              "ordinary scales snap normally");
    }

    // ---- 8. the settings gate everything ---------------------------------------
    // Snapping is off by default, so a step must not leak through when it is.
    {
        SnapSettings st;
        check(!st.enabled, "snapping is off by default");
        check(st.step_for_translate() == 0.0f && st.step_for_rotate() == 0.0f &&
              st.step_for_scale() == 0.0f,
              "and every step reads as 0 while it is off");
        st.enabled = true;
        check(st.step_for_translate() == 0.25f && st.step_for_rotate() == 15.0f &&
              st.step_for_scale() == 0.1f,
              "turning it on gives each mode its own step, since 0.25 units, "
              "15 degrees and 0.1 scale are nothing alike");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL snap_check\n");
    return g_fail ? 1 : 0;
}
