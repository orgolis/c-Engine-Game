// ============================================================================
// navbake_check — the navmesh must describe the scene that actually exists.
//
// The builder, A* + funnel search and the path follower were all verified long
// before this. What was never verified is the INPUT: the only thing that ever
// fed the builder was a synthetic flat grid with a hard-coded square hole,
// generated inside the animation demo. The AI pathed perfectly — around an
// obstacle that existed nowhere in the scene, over ground that was not the
// ground.
//
// The failures worth catching here are all silent ones. A navmesh that omits
// geometry does not error; agents simply walk through the world. A navmesh that
// covers a hole does not error either; agents walk into a pit. So this asserts
// what the bake CONTAINS and what it deliberately EXCLUDES.
// ============================================================================
#include "nav_bake.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

using namespace schizo;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

std::shared_ptr<scene::Entity> add_terrain(const std::shared_ptr<scene::Scene>& s,
                                           const char* name, int res, float size) {
    auto e = s->CreateEntity(name);
    auto t = e->AddComponent<scene::TerrainComponent>();
    t->Resize(res, size);
    return e;
}

}  // namespace

int main() {
    std::printf("=== navbake_check ===\n\n");

    // ---- 1. a flat terrain bakes, and covers its own extent ---------------
    {
        auto s = std::make_shared<scene::Scene>("t");
        add_terrain(s, "Ground", 8, 80.0f);

        ai::NavMesh nav;
        const auto st = editor::bake_navmesh_from_scene(s, nav);
        check(st.ok, "a flat terrain bakes successfully");
        check(st.terrains == 1, "the terrain is counted as a source");
        // 8x8 cells, two triangles each.
        check(st.triangles == 128, "every non-hole cell contributes two triangles");
        check(nav.tri_count() > 0, "the navmesh is non-empty");
    }

    // ---- 2. holes are holes for navigation too -----------------------------
    // Without this an agent paths straight across a cave mouth it would fall
    // into — and nothing errors, which is what makes it worth asserting.
    {
        auto s = std::make_shared<scene::Scene>("t");
        auto e = add_terrain(s, "Ground", 8, 80.0f);
        auto t = e->GetComponent<scene::TerrainComponent>();
        t->SetHole(3, 3, true);
        t->SetHole(4, 3, true);

        ai::NavMesh nav;
        const auto st = editor::bake_navmesh_from_scene(s, nav);
        check(st.ok, "terrain with holes still bakes");
        check(st.triangles == 124, "holed cells contribute NO triangles (128 - 2*2)");
    }

    // ---- 3. an empty scene fails honestly ----------------------------------
    // Reporting success on nothing would give agents an empty navmesh and no
    // indication why they refuse to move.
    {
        auto s = std::make_shared<scene::Scene>("t");
        s->CreateEntity("JustACube");
        ai::NavMesh nav;
        const auto st = editor::bake_navmesh_from_scene(s, nav);
        check(!st.ok, "a scene with no walkable geometry reports failure");
        check(st.triangles == 0, "and collects nothing");
    }

    // ---- 4. box colliders contribute their TOP face only -------------------
    // Emitting the sides would produce a navmesh agents can path up walls on.
    {
        auto s = std::make_shared<scene::Scene>("t");
        auto e = s->CreateEntity("Platform");
        auto c = e->AddComponent<scene::ColliderComponent>();
        c->SetShape(scene::ColliderShape::Box);
        c->SetHalfExtents(glm::vec3(4.0f, 0.5f, 4.0f));

        ai::NavMesh nav;
        const auto st = editor::bake_navmesh_from_scene(s, nav);
        check(st.ok, "a box collider alone bakes a standable surface");
        check(st.boxes == 1, "the box is counted");
        check(st.triangles == 2, "exactly two triangles — the top face, not all six sides");
    }

    // ---- 5. unsupported shapes are counted, not silently dropped -----------
    {
        auto s = std::make_shared<scene::Scene>("t");
        add_terrain(s, "Ground", 4, 40.0f);
        for (auto shape : {scene::ColliderShape::Sphere, scene::ColliderShape::Capsule}) {
            auto e = s->CreateEntity("Round");
            auto c = e->AddComponent<scene::ColliderComponent>();
            c->SetShape(shape);
        }
        ai::NavMesh nav;
        const auto st = editor::bake_navmesh_from_scene(s, nav);
        check(st.skipped_shapes == 2, "curved colliders are REPORTED as skipped");
        check(st.ok, "and do not prevent the rest of the scene baking");
    }

    // ---- 6. inactive entities contribute nothing ---------------------------
    // A disabled entity is not in the world, so it must not be in the navmesh.
    {
        auto s = std::make_shared<scene::Scene>("t");
        auto e = add_terrain(s, "Ground", 4, 40.0f);
        e->SetActive(false);
        ai::NavMesh nav;
        const auto st = editor::bake_navmesh_from_scene(s, nav);
        check(st.terrains == 0, "an inactive terrain is not baked");
        check(!st.ok, "leaving nothing walkable");
    }

    // ---- 7. the entity's transform places the geometry ---------------------
    // Baking in local space would put the navmesh under the world origin
    // regardless of where the terrain actually is.
    {
        auto s = std::make_shared<scene::Scene>("t");
        auto e = add_terrain(s, "Ground", 4, 40.0f);
        e->GetTransform()->SetLocalPosition({1000.0f, 7.0f, -500.0f});

        ai::NavMesh nav;
        const auto st = editor::bake_navmesh_from_scene(s, nav);
        check(st.ok, "an offset terrain bakes");

        // A path between two points ON the offset terrain must exist; the same
        // query at the origin must not. Baking in local space would invert both.
        std::vector<glm::vec3> path;
        const glm::vec3 a(1000.0f - 10.0f, 7.0f, -500.0f - 10.0f);
        const glm::vec3 b(1000.0f + 10.0f, 7.0f, -500.0f + 10.0f);
        check(nav.find_path(a, b, path), "a path exists across the terrain's real position");

        // NOT "a path at the origin must fail": find_path snaps start and end to
        // the NEAREST triangle with no distance limit, so an origin query
        // legitimately finds this mesh 1100 units away. Asserting otherwise
        // tests a behaviour find_path does not have. What actually proves the
        // transform was applied is WHERE the returned path lies.
        check(!path.empty(), "the path has points to inspect");
        bool all_near_terrain = !path.empty();
        for (const auto& p : path)
            if (std::abs(p.x - 1000.0f) > 60.0f || std::abs(p.z + 500.0f) > 60.0f)
                all_near_terrain = false;
        check(all_near_terrain,
              "every path point lies on the terrain's real position, not near the origin");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL navbake_check\n");
    return g_fail ? 1 : 0;
}
