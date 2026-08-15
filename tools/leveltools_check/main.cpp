// ============================================================================
// leveltools_check — arrays, scatter and splines place things correctly.
//
// These are the operations that place MANY objects, so an error is multiplied
// rather than isolated: an array whose spacing drifts, a scatter that clumps
// toward its centre, a spline that misses its own control points. None of them
// looks broken in a screenshot and all of them are arithmetic.
//
// The reproducibility assertions matter as much as the geometric ones. A
// scatter that reshuffles on reload is one nobody can build a level on.
// ============================================================================
#include "level_tools.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

using namespace schizo::editor;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

bool near(float a, float b, float eps = 1e-3f) { return std::abs(a - b) < eps; }
bool near3(const glm::vec3& a, const glm::vec3& b, float eps = 1e-3f) {
    return glm::length(a - b) < eps;
}

}  // namespace

int main() {
    std::printf("=== leveltools_check ===\n\n");

    // ---- 1. an array starts AT the source ----------------------------------
    // If it did not, the first duplicate would sit exactly on top of the
    // original: invisible until something behaves oddly.
    {
        ArrayParams p; p.count_x = 4; p.step = glm::vec3(2.0f, 0.0f, 0.0f);
        const auto a = build_array(glm::vec3(10.0f, 0.0f, 5.0f), p);
        check(a.size() == 4, "an array of 4 produces 4 placements");
        check(near3(a[0].position, glm::vec3(10.0f, 0.0f, 5.0f)),
              "the first is the source's own position");
        check(near3(a[3].position, glm::vec3(16.0f, 0.0f, 5.0f)),
              "and the last is three steps along, not four");
    }

    // ---- 2. spacing does not drift -----------------------------------------
    // The failure that only shows up at the far end of a long fence.
    {
        ArrayParams p; p.count_x = 50; p.step = glm::vec3(1.37f, 0.0f, 0.0f);
        const auto a = build_array(glm::vec3(0.0f), p);
        bool even = true;
        for (size_t i = 1; i < a.size(); ++i)
            if (!near(a[i].position.x - a[i - 1].position.x, 1.37f, 1e-3f)) even = false;
        check(even, "fifty steps of 1.37 are all still 1.37 apart at the end");
        check(near(a.back().position.x, 49.0f * 1.37f, 1e-2f),
              "and the total run is exactly 49 gaps, not 50");
    }

    // ---- 3. 3D arrays fill the volume ---------------------------------------
    {
        ArrayParams p; p.count_x = 2; p.count_y = 3; p.count_z = 4;
        p.step = glm::vec3(1.0f, 2.0f, 3.0f);
        const auto a = build_array(glm::vec3(0.0f), p);
        check(a.size() == 24, "2x3x4 produces 24 placements");
        check(near3(a.back().position, glm::vec3(1.0f, 4.0f, 9.0f)),
              "with the far corner at (1, 4, 9)");
    }

    // ---- 4. centring puts the source in the middle --------------------------
    {
        ArrayParams p; p.count_x = 5; p.step = glm::vec3(2.0f, 0.0f, 0.0f);
        p.center = true;
        const auto a = build_array(glm::vec3(0.0f), p);
        check(near(a[2].position.x, 0.0f),
              "an odd centred array puts its middle element on the source");
        check(near(a[0].position.x, -4.0f) && near(a[4].position.x, 4.0f),
              "spreading symmetrically either side");
    }

    // ---- 5. scatter is REPRODUCIBLE -----------------------------------------
    // The property a level depends on: same seed, same layout, every time.
    {
        ScatterParams p; p.count = 25; p.radius = 12.0f; p.seed = 4242u;
        const auto a = build_scatter(glm::vec3(3.0f, 0.0f, -7.0f), p);
        const auto b = build_scatter(glm::vec3(3.0f, 0.0f, -7.0f), p);
        bool same = a.size() == b.size();
        for (size_t i = 0; same && i < a.size(); ++i)
            if (!near3(a[i].position, b[i].position)) same = false;
        check(same, "the same seed gives byte-identical placements");

        p.seed = 4243u;
        const auto c = build_scatter(glm::vec3(3.0f, 0.0f, -7.0f), p);
        bool differs = false;
        for (size_t i = 0; i < std::min(a.size(), c.size()); ++i)
            if (!near3(a[i].position, c[i].position)) differs = true;
        check(differs, "and a different seed gives a different layout");
    }

    // ---- 6. scatter stays inside its radius ---------------------------------
    {
        ScatterParams p; p.count = 200; p.radius = 5.0f;
        const glm::vec3 c(100.0f, 0.0f, -50.0f);
        const auto a = build_scatter(c, p);
        bool inside = true;
        for (const auto& pl : a) {
            const float dx = pl.position.x - c.x, dz = pl.position.z - c.z;
            if (std::sqrt(dx * dx + dz * dz) > 5.0f + 1e-3f) inside = false;
        }
        check(inside, "every instance lands inside the requested radius");
    }

    // ---- 7. scatter does not CLUMP toward the centre -------------------------
    // Sampling radius uniformly gives uniform density per ring, not per unit
    // area, and the result looks like a target. The inner half of a disc is a
    // quarter of its area, so roughly a quarter of the points belong there.
    {
        ScatterParams p; p.count = 600; p.radius = 10.0f; p.seed = 99u;
        const auto a = build_scatter(glm::vec3(0.0f), p);
        int inner = 0;
        for (const auto& pl : a)
            if (glm::length(glm::vec3(pl.position.x, 0.0f, pl.position.z)) < 5.0f) ++inner;
        const float frac = float(inner) / float(a.size());
        check(frac > 0.15f && frac < 0.35f,
              "about a quarter of points fall in the inner half-radius "
              "(uniform by AREA, not by radius)");
    }

    // ---- 8. min_distance is respected, and honestly ---------------------------
    {
        ScatterParams p; p.count = 30; p.radius = 20.0f; p.min_distance = 3.0f;
        const auto a = build_scatter(glm::vec3(0.0f), p);
        bool spaced = true;
        for (size_t i = 0; i < a.size(); ++i)
            for (size_t j = i + 1; j < a.size(); ++j) {
                const float dx = a[i].position.x - a[j].position.x;
                const float dz = a[i].position.z - a[j].position.z;
                if (std::sqrt(dx * dx + dz * dz) < 3.0f - 1e-3f) spaced = false;
            }
        check(spaced, "no two instances are closer than min_distance");

        // Unsatisfiable request: fewer is the honest answer.
        ScatterParams tight; tight.count = 50; tight.radius = 3.0f; tight.min_distance = 3.0f;
        const auto few = build_scatter(glm::vec3(0.0f), tight);
        check(few.size() < 50,
              "an impossible density returns FEWER rather than overlapping them");
    }

    // ---- 9. a spline passes THROUGH its control points -------------------------
    // The whole reason for Catmull-Rom over Bezier. "The path does not go where
    // I put it" is the first complaint about any spline tool that approximates.
    {
        Spline s;
        s.add_point({0.0f, 0.0f, 0.0f});
        s.add_point({5.0f, 0.0f, 5.0f});
        s.add_point({10.0f, 0.0f, 0.0f});
        s.add_point({15.0f, 0.0f, 5.0f});

        check(near3(s.evaluate(0.0f), glm::vec3(0.0f, 0.0f, 0.0f)),
              "t=0 is exactly the first control point");
        check(near3(s.evaluate(1.0f), glm::vec3(15.0f, 0.0f, 5.0f)),
              "t=1 is exactly the last");
        check(near3(s.evaluate(1.0f / 3.0f), glm::vec3(5.0f, 0.0f, 5.0f), 1e-2f),
              "and an interior control point is hit exactly, not approximated");
    }

    // ---- 10. distribution is even by ARC LENGTH ------------------------------
    // Spacing evenly in t bunches instances at corners and stretches them on
    // straights -- most visible exactly where it matters.
    {
        Spline s;
        s.add_point({0.0f, 0.0f, 0.0f});
        s.add_point({10.0f, 0.0f, 0.0f});
        s.add_point({10.0f, 0.0f, 10.0f});   // a right angle
        s.add_point({20.0f, 0.0f, 10.0f});

        const auto pts = s.distribute(12, false);
        check(pts.size() == 12, "twelve requested, twelve placed");

        float min_gap = 1e9f, max_gap = 0.0f;
        for (size_t i = 1; i < pts.size(); ++i) {
            const float d = glm::length(pts[i].position - pts[i - 1].position);
            min_gap = std::min(min_gap, d);
            max_gap = std::max(max_gap, d);
        }
        check(max_gap / min_gap < 1.35f,
              "gaps stay within a third of each other around a right angle");
        check(near3(pts.front().position, glm::vec3(0.0f), 0.05f),
              "the first sits at the start of the path");
    }

    // ---- 11. degenerate inputs do not crash -----------------------------------
    {
        Spline empty;
        check(near3(empty.evaluate(0.5f), glm::vec3(0.0f)), "an empty spline evaluates to 0");
        check(empty.distribute(5, true).empty(), "and distributes nothing");

        Spline one; one.add_point({1.0f, 2.0f, 3.0f});
        check(near3(one.evaluate(0.7f), glm::vec3(1.0f, 2.0f, 3.0f)),
              "a one-point spline is that point everywhere");

        Spline two; two.add_point({0.0f, 0.0f, 0.0f}); two.add_point({10.0f, 0.0f, 0.0f});
        check(near3(two.evaluate(0.5f), glm::vec3(5.0f, 0.0f, 0.0f)),
              "a two-point spline is a straight line");

        ArrayParams zero; zero.count_x = 0; zero.count_y = 0; zero.count_z = 0;
        check(build_array(glm::vec3(0.0f), zero).size() == 1,
              "a zero-count array still produces the source itself, not nothing");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL leveltools_check\n");
    return g_fail ? 1 : 0;
}
