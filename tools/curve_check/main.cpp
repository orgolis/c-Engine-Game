// ============================================================================
// curve_check — curves and gradients evaluate correctly.
//
// This is the half of 4.5 that can be wrong without looking wrong. A gradient
// that reads its last stop slightly early, or a curve that steps where it
// should ease, is invisible in a screenshot and obvious in motion — by which
// point it reads as "the effect looks cheap" rather than as a bug with a
// location.
//
// So these assert the shape of the interpolation, not just its endpoints.
// Endpoints are the part that is nearly impossible to get wrong.
// ============================================================================
#include "anim/curve.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace gws::anim;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

bool near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) < eps; }

}  // namespace

int main() {
    std::printf("=== curve_check ===\n\n");

    // ---- 1. keys are honoured exactly ---------------------------------------
    {
        Curve c({{0.0f, 0.0f}, {1.0f, 10.0f}});
        check(near(c.evaluate(0.0f), 0.0f) && near(c.evaluate(1.0f), 10.0f),
              "a curve passes exactly through its keys");
    }

    // ---- 2. it EASES rather than stepping or ramping -------------------------
    // The midpoint of a flat-tangent Hermite is exactly halfway, but the
    // quarter points are not: 0.25 lands at 0.15625, not 0.25. A linear
    // implementation and a stepping one both pass an endpoint-only test.
    {
        Curve c({{0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f, 0.0f}});
        check(near(c.evaluate(0.5f), 0.5f),
              "with flat tangents the midpoint is halfway");
        check(near(c.evaluate(0.25f), 0.15625f),
              "and the quarter point EASES (0.15625), rather than ramping "
              "linearly to 0.25");
        check(near(c.evaluate(0.75f), 0.84375f), "symmetrically at the far end");
    }

    // ---- 3. clamped, not extrapolated ---------------------------------------
    // Extrapolating a cubic runs away fast. A fade that overshoots into
    // negative alpha because a particle lived one frame past the curve is
    // exactly the edge-case bug this prevents.
    {
        Curve c({{0.0f, 2.0f}, {1.0f, 5.0f}});
        check(near(c.evaluate(-10.0f), 2.0f), "before the first key it holds that key");
        check(near(c.evaluate(10.0f), 5.0f),  "after the last key it holds that one");
    }

    // ---- 4. keys sort themselves --------------------------------------------
    // An editor drags keys past each other constantly, and an unsorted curve
    // evaluates to nonsense rather than failing.
    {
        Curve c;
        c.add({1.0f, 10.0f});
        c.add({0.0f, 0.0f});          // inserted BEFORE the existing key
        c.add({0.5f, 5.0f});
        check(c.keys()[0].time == 0.0f && c.keys()[1].time == 0.5f &&
              c.keys()[2].time == 1.0f,
              "keys added out of order sort themselves");
        check(near(c.evaluate(0.5f), 5.0f), "and evaluate correctly afterwards");
    }

    // ---- 5. tangents actually change the shape -------------------------------
    // If tangents were ignored, every curve would be the same ease and nothing
    // above would notice.
    {
        Curve flat({{0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f, 0.0f}});
        Curve steep({{0.0f, 0.0f, 0.0f, 3.0f}, {1.0f, 1.0f, 3.0f, 0.0f}});
        check(!near(flat.evaluate(0.25f), steep.evaluate(0.25f)),
              "a steep out-tangent produces a different curve, so tangents "
              "are not being ignored");
        check(steep.evaluate(0.25f) > flat.evaluate(0.25f),
              "and it leaves the first key faster, which is what steep means");
    }

    // ---- 6. degenerate curves do not crash or produce garbage ----------------
    {
        Curve empty;
        check(near(empty.evaluate(0.5f), 0.0f), "an empty curve evaluates to 0");
        Curve one({{0.3f, 7.0f}});
        check(near(one.evaluate(0.0f), 7.0f) && near(one.evaluate(9.0f), 7.0f),
              "a one-key curve is constant everywhere");
        Curve dup({{0.5f, 1.0f}, {0.5f, 9.0f}});
        check(near(dup.evaluate(0.5f), 9.0f) || near(dup.evaluate(0.5f), 1.0f),
              "coincident keys give one of the two values, not a divide by zero");
    }

    // ---- 7. gradients interpolate, alpha included ---------------------------
    // Alpha is the channel that carries fades. Forgetting it is the difference
    // between an effect that dissolves and one that pops out of existence.
    {
        Gradient g({{0.0f, glm::vec4(1, 0, 0, 1)}, {1.0f, glm::vec4(0, 0, 1, 0)}});
        const glm::vec4 mid = g.sample(0.5f);
        check(near(mid.r, 0.5f) && near(mid.b, 0.5f), "colour interpolates linearly");
        check(near(mid.a, 0.5f), "and so does ALPHA");
        check(near(g.sample(0.0f).r, 1.0f) && near(g.sample(1.0f).b, 1.0f),
              "the endpoints are exact");
    }

    // ---- 8. multi-stop gradients pick the right segment ---------------------
    // A two-stop test cannot tell a correct implementation from one that only
    // ever looks at the first and last stop.
    {
        Gradient g({{0.0f, glm::vec4(0, 0, 0, 1)},
                    {0.5f, glm::vec4(1, 1, 1, 1)},
                    {1.0f, glm::vec4(0, 0, 0, 1)}});
        check(near(g.sample(0.5f).r, 1.0f), "the middle stop is reached exactly");
        check(near(g.sample(0.25f).r, 0.5f), "the first segment interpolates");
        check(near(g.sample(0.75f).r, 0.5f), "and so does the second");
        check(near(g.sample(1.0f).r, 0.0f),
              "and the last stop is not read early — a three-stop gradient is "
              "what catches an implementation that only sees two");
    }

    // ---- 9. clamped at both ends ---------------------------------------------
    {
        Gradient g = Gradient::two_stop(glm::vec4(1, 0, 0, 1), glm::vec4(0, 1, 0, 1));
        check(near(g.sample(-5.0f).r, 1.0f), "sampling before the first stop clamps");
        check(near(g.sample(5.0f).g, 1.0f),  "and after the last one");
    }

    // ---- 10. stops sort themselves too ---------------------------------------
    {
        Gradient g;
        g.add({1.0f, glm::vec4(0, 0, 1, 1)});
        g.add({0.0f, glm::vec4(1, 0, 0, 1)});
        check(near(g.sample(0.0f).r, 1.0f) && near(g.sample(1.0f).b, 1.0f),
              "stops added out of order sort themselves");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL curve_check\n");
    return g_fail ? 1 : 0;
}
