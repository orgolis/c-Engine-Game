// ====================
// ai_check — headless verification of the AI systems (Stage 9)
// ====================
//
//   1. NavMesh: build a flat grid with a wall gap, verify A* + funnel finds a
//      path that (a) connects start→end, (b) stays on the mesh, (c) detours
//      around the wall (length > straight line), (d) is smooth (few corners).
//   2. NavMesh: an unreachable target (isolated island) returns no path.
//   3. Behavior tree: selector(chase / patrol) with a blackboard — patrol when
//      no target, chase (move) when a target is seen; sequence running-memory;
//      inverter; cooldown one-shot-then-block.
//
// Pure CPU. Prints "ai_check: ALL OK" + exits 0 on pass.

#include "ai/navmesh.h"
#include "ai/behavior_tree.h"
#include "ai/nav_builder.h"
#include "ai/path_follow.h"

#include <glm/glm.hpp>
#include <cmath>
#include <cstdio>
#include <vector>
#include <cstdint>

using namespace schizo::ai;

namespace {
int g_checks = 0;
bool check(bool ok, const char* what) {
    ++g_checks;
    std::printf("  [%s] %s\n", ok ? "OK" : "FAIL", what);
    return ok;
}
#define REQUIRE(c, w) do { if (!check((c), (w))) { \
    std::printf("ai_check: FAILED at '%s'\n", w); return 1; } } while (0)

// Add a quad (two CCW-from-above triangles) at grid cell (x,z), size 1x1.
void add_cell(std::vector<glm::vec3>& tv, int x, int z) {
    const glm::vec3 a(x + 0.0f, 0, z + 0.0f), b(x + 1.0f, 0, z + 0.0f);
    const glm::vec3 c(x + 1.0f, 0, z + 1.0f), d(x + 0.0f, 0, z + 1.0f);
    tv.insert(tv.end(), {a, b, c,  a, c, d});
}

float path_len(const std::vector<glm::vec3>& p) {
    float L = 0.0f;
    for (size_t i = 1; i < p.size(); ++i) L += glm::length(p[i] - p[i - 1]);
    return L;
}
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== ai_check: navmesh + behavior trees (Stage 9) ===\n");

    // ---- 1. NavMesh with a wall forcing a detour --------------------------
    // 10x10 floor. Wall = remove cells x==5 for z in [0..7] (a barrier from the
    // bottom edge with a gap at z 8,9 up top), so a path from bottom-left to
    // bottom-right must go up and around.
    {
        std::vector<glm::vec3> tv;
        for (int z = 0; z < 10; ++z)
            for (int x = 0; x < 10; ++x) {
                if (x == 5 && z <= 7) continue;   // wall with a gap at top
                add_cell(tv, x, z);
            }
        NavMesh nav;
        REQUIRE(nav.build(tv), "navmesh builds from walkable triangles");
        REQUIRE(nav.tri_count() == static_cast<size_t>((100 - 8) * 2),
                "triangle count matches (100 cells - 8 wall) x2");

        const glm::vec3 start(1.5f, 0, 1.5f);
        const glm::vec3 end(8.5f, 0, 1.5f);
        std::vector<glm::vec3> path;
        REQUIRE(nav.find_path(start, end, path), "path found across the barrier");
        REQUIRE(path.size() >= 2, "path has at least start+end");
        REQUIRE(glm::length(path.front() - start) < 0.01f, "path starts at start");
        REQUIRE(glm::length(path.back() - end) < 0.01f, "path ends at end");

        // Every corner sits on the mesh.
        bool on_mesh = true;
        for (const auto& p : path) if (nav.nearest_tri(p) < 0) on_mesh = false;
        REQUIRE(on_mesh, "every path corner is on the navmesh");

        const float straight = glm::length(end - start);      // ~7
        const float L = path_len(path);
        std::printf("       path: %zu corners, length %.2f (straight %.2f)\n",
                    path.size(), L, straight);
        REQUIRE(L > straight * 1.3f, "path detours around the wall (longer than straight)");
        REQUIRE(path.size() <= 6, "funnel keeps the path smooth (few corners)");

        // A clear straight shot on open floor should be ~straight + near 2 pts.
        std::vector<glm::vec3> open_path;
        nav.find_path(glm::vec3(1.5f, 0, 8.5f), glm::vec3(4.0f, 0, 8.5f), open_path);
        REQUIRE(open_path.size() == 2 &&
                std::abs(path_len(open_path) - 2.5f) < 0.1f,
                "open-floor path is a straight 2-point segment");
    }

    // ---- 2. Unreachable target (disconnected island) ----------------------
    {
        std::vector<glm::vec3> tv;
        add_cell(tv, 0, 0);            // island A
        add_cell(tv, 50, 50);          // island B (far, unconnected)
        NavMesh nav;
        REQUIRE(nav.build(tv), "two-island navmesh builds");
        std::vector<glm::vec3> path;
        const bool ok = nav.find_path(glm::vec3(0.5f, 0, 0.5f),
                                      glm::vec3(50.5f, 0, 50.5f), path);
        REQUIRE(!ok, "no path between disconnected islands");
    }

    // ---- 3. Behavior tree -------------------------------------------------
    {
        int patrol_ticks = 0, move_ticks = 0;
        BehaviorTree tree;
        auto root = std::make_unique<Selector>();
        // Chase: IF can_see_target THEN move_to_target.
        auto chase = std::make_unique<Sequence>();
        chase->add(make_condition([](Blackboard& bb) { return bb.get<bool>("can_see_target"); }));
        chase->add(make_action([&](Blackboard&, float) { ++move_ticks; return BtStatus::Success; }));
        root->add(std::move(chase));
        // Fallback: patrol.
        root->add(make_action([&](Blackboard&, float) { ++patrol_ticks; return BtStatus::Success; }));
        tree.set_root(std::move(root));

        Blackboard bb;
        bb.set<bool>("can_see_target", false);
        tree.tick(bb, 0.016f);
        REQUIRE(patrol_ticks == 1 && move_ticks == 0, "no target → patrols");

        bb.set<bool>("can_see_target", true);
        const BtStatus s = tree.tick(bb, 0.016f);
        REQUIRE(s == BtStatus::Success && move_ticks == 1 && patrol_ticks == 1,
                "target seen → chase runs move (patrol not run)");

        // Sequence running-memory: a Running action holds the sequence and
        // resumes at that child without re-running earlier ones.
        int cond_evals = 0, longact_runs = 0; bool finish = false;
        Sequence seq;
        seq.add(make_condition([&](Blackboard&) { ++cond_evals; return true; }));
        seq.add(make_action([&](Blackboard&, float) {
            ++longact_runs; return finish ? BtStatus::Success : BtStatus::Running; }));
        REQUIRE(seq.tick(bb, 0.016f) == BtStatus::Running, "sequence is Running on a Running child");
        seq.tick(bb, 0.016f);   // second tick
        REQUIRE(cond_evals == 1 && longact_runs == 2,
                "running-memory: condition not re-evaluated, action resumed");
        finish = true;
        REQUIRE(seq.tick(bb, 0.016f) == BtStatus::Success, "sequence completes when action succeeds");

        // Inverter.
        Inverter inv(make_condition([](Blackboard&) { return false; }));
        REQUIRE(inv.tick(bb, 0.016f) == BtStatus::Success, "inverter flips Failure→Success");

        // Cooldown: succeeds once, then blocks (Failure) for the duration.
        int cd_runs = 0;
        Cooldown cd(1.0f, make_action([&](Blackboard&, float) { ++cd_runs; return BtStatus::Success; }));
        REQUIRE(cd.tick(bb, 0.016f) == BtStatus::Success, "cooldown: first tick succeeds");
        REQUIRE(cd.tick(bb, 0.016f) == BtStatus::Failure && cd_runs == 1,
                "cooldown: blocks (child not re-run) during cooldown");
        // Tick until it fires again (don't overshoot into a second cooldown).
        BtStatus cs = BtStatus::Failure; int elapsed = 0;
        while (elapsed < 200 && (cs = cd.tick(bb, 0.016f)) != BtStatus::Success) ++elapsed;
        REQUIRE(cs == BtStatus::Success && cd_runs == 2 && elapsed >= 55,
                "cooldown: child runs again only after the ~1s cooldown elapses");
    }

    // ---- 4. NavMeshBuilder slope filter -----------------------------------
    {
        NavMeshBuilder b;
        b.set_max_slope_deg(45.0f);
        // Flat floor triangle (walkable) + a vertical wall triangle (steep).
        b.add_triangle({0, 0, 0}, {1, 0, 0}, {0, 0, 1});          // normal ~+Y
        b.add_triangle({0, 0, 0}, {0, 2, 0}, {0, 0, 1});          // normal ~±X (wall)
        REQUIRE(b.walkable_triangle_count() == 1,
                "builder keeps flat floor, drops the steep wall");
    }

    // ---- 5. Full agent: bake → path → steer → arrive ----------------------
    {
        // 12x12 floor with a wall at x==6, z<=9 (gap at top) forcing a detour.
        std::vector<glm::vec3> verts;
        std::vector<uint32_t>  idx;
        auto quad = [&](int x, int z) {
            const uint32_t base = static_cast<uint32_t>(verts.size());
            verts.push_back({x + 0.0f, 0, z + 0.0f});
            verts.push_back({x + 1.0f, 0, z + 0.0f});
            verts.push_back({x + 1.0f, 0, z + 1.0f});
            verts.push_back({x + 0.0f, 0, z + 1.0f});
            idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        };
        for (int z = 0; z < 12; ++z)
            for (int x = 0; x < 12; ++x) {
                if (x == 6 && z <= 9) continue;
                quad(x, z);
            }
        NavMeshBuilder builder;
        builder.add_indexed(verts, idx, glm::mat4(1.0f));
        NavMesh nav;
        REQUIRE(builder.build(nav), "builder bakes the floor navmesh");

        const glm::vec3 start(1.5f, 0, 1.5f), goal(10.5f, 0, 1.5f);
        std::vector<glm::vec3> path;
        REQUIRE(nav.find_path(start, goal, path), "agent gets a path to the goal");

        PathFollower follower;
        follower.set_path(path);

        // Simulate an agent stepping along the corridor.
        glm::vec3 pos = start;
        float yaw = 0.0f;
        bool on_mesh = true;
        int steps = 0;
        const float dt = 1.0f / 60.0f, spd = 3.0f;
        for (; steps < 2000 && !follower.arrived(); ++steps) {
            auto st = follower.steer(pos, spd, yaw);
            if (st.arrived) break;
            pos += st.velocity * dt;
            yaw = st.yaw;
            if (nav.nearest_tri(pos) < 0) on_mesh = false;
        }
        std::printf("       agent walked to (%.2f,%.2f) in %d steps (goal 10.5,1.5)\n",
                    pos.x, pos.z, steps);
        REQUIRE(follower.arrived() || glm::length(pos - goal) < 0.5f,
                "agent path-follows to the goal");
        REQUIRE(glm::length(pos - goal) < 0.6f, "agent ends near the goal");
        REQUIRE(on_mesh, "agent stayed on the navmesh the whole way");
        REQUIRE(steps < 2000, "agent arrived in a bounded number of steps");
    }

    std::printf("ai_check: ALL OK (%d checks)\n", g_checks);
    return 0;
}
