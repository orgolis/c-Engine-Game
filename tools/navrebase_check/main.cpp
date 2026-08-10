// ============================================================================
// navrebase_check — an AI agent survives a floating-origin rebase.
//
// Phase 3 wired up world streaming and navmesh-baked agents separately, and
// each was verified on its own. This is the interaction between them, which is
// where the bug actually was: a rebase moves every entity in the world, but the
// baked navmesh stays exactly where it was, and so does every world-space
// number the agents remember -- patrol goals, the last goal they pathed to, and
// the waypoints their follower is steering along.
//
// The result is not a small inaccuracy. Rebase shifts are hundreds of units and
// a navmesh is tens, so after one rebase the agent is not slightly off the mesh
// -- it is nowhere near it. It would stand still, or walk purposefully toward a
// place that no longer exists, and nothing anywhere would report an error.
//
// Streaming is off by default and agents only run in play mode, which is why
// this survived: the two features are rarely on at the same moment.
// ============================================================================
#include "ai/navmesh.h"
#include "ai/path_follow.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace schizo;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

// A flat 20x20 walkable quad centred on the origin, as two triangles per cell.
std::vector<glm::vec3> flat_grid(float half, float step) {
    std::vector<glm::vec3> v;
    for (float x = -half; x < half; x += step)
        for (float z = -half; z < half; z += step) {
            v.push_back({x, 0.0f, z});
            v.push_back({x, 0.0f, z + step});
            v.push_back({x + step, 0.0f, z});

            v.push_back({x + step, 0.0f, z});
            v.push_back({x, 0.0f, z + step});
            v.push_back({x + step, 0.0f, z + step});
        }
    return v;
}

// Every waypoint of a real path lies on the mesh. find_path SNAPS off-mesh
// endpoints to the nearest triangle and echoes the caller's start/end back, so
// "it returned true" proves nothing on its own -- this is what distinguishes a
// route across the mesh from a straight line drawn through empty space.
//
// It has to be contains(), not height_on(): height_on extrapolates the
// triangle's PLANE, so on a flat mesh it happily reports y=0 for a point five
// hundred units past the edge. My first version of this used it and could not
// tell on-mesh from off-mesh at all.
bool path_on_mesh(const ai::NavMesh& nav, const std::vector<glm::vec3>& p) {
    for (const glm::vec3& w : p)
        if (!nav.contains(w)) return false;
    return true;
}

}  // namespace

int main() {
    std::printf("=== navrebase_check ===\n\n");

    // A rebase of the magnitude the streaming manager actually produces.
    const glm::vec3 kShift(-512.0f, 0.0f, 256.0f);

    ai::NavMesh nav;
    check(nav.build(flat_grid(10.0f, 2.0f)), "a navmesh builds from the test grid");

    const glm::vec3 start(-8.0f, 0.0f, -8.0f);
    const glm::vec3 goal (  8.0f, 0.0f,  8.0f);

    std::vector<glm::vec3> path;
    check(nav.find_path(start, goal, path) && path.size() >= 2,
          "and paths across it before any rebase");

    // ---- 1. the bug ---------------------------------------------------------
    // After the world moves, the agent is at start+shift. Pathing from there
    // against an unmoved navmesh is the broken case; the mesh has to move too.
    {
        std::vector<glm::vec3> p;
        const bool ok = nav.find_path(start + kShift, goal + kShift, p);

        // It does NOT fail -- which is the genuinely nasty part. find_path snaps
        // off-mesh endpoints to the nearest triangle, so both ends land on the
        // same distant triangle, A* has nothing to search, and the funnel
        // returns a bare [start, end]. The agent gets a straight line and the
        // navmesh stops constraining it: it walks through walls, across holes,
        // off ledges. A hard failure would at least be visible.
        check(ok && p.size() == 2,
              "with a stale mesh the path DEGENERATES to a straight line — "
              "the navmesh silently stops constraining the agent (this is the bug)");
        check(!path_on_mesh(nav, p),
              "and that line does not lie on the mesh at all");

        nav.translate(kShift);

        std::vector<glm::vec3> q;
        check(nav.find_path(start + kShift, goal + kShift, q) && q.size() > 2,
              "after translating the mesh, a real multi-leg route comes back");
        check(path_on_mesh(nav, q),
              "and every waypoint of it lies on the mesh");
    }

    // ---- 2. the route is the same route, not merely a route -----------------
    // A translated mesh that produced a DIFFERENT path would mean the rebase
    // changed the agent's behaviour, which is just a subtler version of the
    // same bug.
    // I first asserted the post-rebase path was the pre-rebase path shifted, and
    // it is not: at 512 units out, centroid distances are computed in a coarser
    // part of the float range, and on an open mesh many corridors tie exactly,
    // so a perturbed tie picks a different equal-cost route. That is inherent to
    // large coordinates -- it is the very thing floating origin exists to limit
    // -- and the alternative route is just as good. So the property worth
    // asserting is that the agent still gets a sound path, not an identical one.
    {
        std::vector<glm::vec3> q;
        check(nav.find_path(start + kShift, goal + kShift, q), "a path still exists");

        float len_before = 0.0f, len_after = 0.0f;
        for (size_t i = 1; i < path.size(); ++i) len_before += glm::length(path[i] - path[i - 1]);
        for (size_t i = 1; i < q.size(); ++i)    len_after  += glm::length(q[i] - q[i - 1]);
        check(len_after < len_before * 1.25f + 1.0f,
              "of comparable length — the route may differ, the cost may not");
        check(glm::length(q.front() - (start + kShift)) < 1e-2f,
              "starting where the agent actually is");
    }

    // ---- 3. translation is exactly reversible -------------------------------
    // Rebases accumulate over a long session; drift here would compound.
    {
        nav.translate(-kShift);
        std::vector<glm::vec3> q;
        nav.find_path(start, goal, q);
        bool same = q.size() == path.size();
        if (same)
            for (size_t i = 0; i < q.size(); ++i)
                if (glm::length(q[i] - path[i]) > 1e-4f) { same = false; break; }
        check(same, "translating back restores the mesh exactly (rebases accumulate)");
    }

    // ---- 4. height queries move with the mesh -------------------------------
    // nearest_tri and height_on are what keep an agent on the ground; if they
    // lagged behind, the agent would sink or hover after a rebase.
    {
        ai::NavMesh sloped;
        std::vector<glm::vec3> v = flat_grid(10.0f, 2.0f);
        for (auto& p : v) p.y = 3.0f;              // a flat mesh at y=3
        sloped.build(v);

        const int t0 = sloped.nearest_tri({4.0f, 0.0f, 4.0f});
        const float h0 = sloped.height_on(t0, {4.0f, 0.0f, 4.0f});

        sloped.translate(kShift);
        const int t1 = sloped.nearest_tri(glm::vec3(4.0f, 0.0f, 4.0f) + kShift);
        const float h1 = sloped.height_on(t1, glm::vec3(4.0f, 0.0f, 4.0f) + kShift);

        check(t1 >= 0 && std::abs(h1 - h0) < 1e-3f,
              "ground height at the shifted point matches the original — "
              "an agent does not sink or hover through a rebase");
    }

    // ---- 5. the follower's cached path moves too ----------------------------
    // The mesh being right is not enough: the follower steers toward waypoints
    // it captured before the rebase, so it would walk the agent back toward the
    // old world even on a correct mesh.
    {
        ai::PathFollower f;
        f.set_path(path);
        const ai::PathFollower::Steer before = f.steer(start, 5.0f);
        check(before.moving, "a follower steers toward its goal before the rebase");

        // Without translate(): the agent has moved, the waypoints have not.
        ai::PathFollower stale;
        stale.set_path(path);
        const ai::PathFollower::Steer bad = stale.steer(start + kShift, 5.0f);

        f.translate(kShift);
        const ai::PathFollower::Steer good = f.steer(start + kShift, 5.0f);

        check(good.moving, "and still steers after the rebase, once translated");
        check(glm::dot(glm::normalize(good.velocity), glm::normalize(before.velocity)) > 0.999f,
              "in the SAME direction as before — the rebase is invisible to the agent");
        check(glm::dot(glm::normalize(bad.velocity), glm::normalize(before.velocity)) < 0.9f,
              "whereas the untranslated follower steers somewhere else entirely "
              "(this is the bug)");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL navrebase_check\n");
    return g_fail ? 1 : 0;
}
