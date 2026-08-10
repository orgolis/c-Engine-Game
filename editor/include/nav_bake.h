#pragma once

// ============================================================================
// nav_bake — build a navmesh from the geometry that is actually in the scene.
//
// The navmesh builder, A* + funnel path search and the path follower were all
// built and verified (28 assertions in ai_check). What was missing was the
// input: the only thing that ever fed the builder was a **synthetic flat grid
// with a hard-coded square hole**, generated inside the animation demo. So the
// AI could path perfectly — around an obstacle that existed nowhere in the
// scene, over ground that was not the ground.
//
// This walks the real scene instead. Two sources, chosen because they are the
// two surfaces a character actually walks on:
//
//   TERRAIN   two triangles per heightmap cell, in world space, skipping cells
//             marked as holes. This is the floor of most outdoor scenes.
//
//   BOX COLLIDERS  the TOP face only. A box's sides and underside are not
//             walkable, and emitting them would produce a navmesh where agents
//             happily path up walls. Platforms, steps and crates become
//             standable surfaces.
//
// Slope filtering is the builder's job (set_max_slope_deg): steep terrain is
// rejected there rather than here, so one rule governs both sources.
//
// NOT COVERED, and reported rather than hidden: sphere/capsule/cylinder/mesh
// colliders. A curved surface has no single walkable face, and mesh colliders
// need the CPU-side index data that only the cooked path retains. The stats say
// how many were skipped, because a navmesh that silently omits geometry sends
// agents walking through the world instead of over it.
// ============================================================================

#include "ai/nav_builder.h"
#include "ai/navmesh.h"
#include "collider_component.h"
#include "entity.h"
#include "scene.h"
#include "terrain_component.h"
#include "transform.h"

#include <spdlog/spdlog.h>

namespace schizo::editor {

struct NavBakeStats {
    size_t terrains       = 0;   // terrain entities contributing
    size_t boxes          = 0;   // box colliders contributing a top face
    size_t skipped_shapes = 0;   // colliders whose shape cannot be baked yet
    size_t triangles      = 0;   // walkable triangles after slope filtering
    bool   ok             = false;
};

/// Collect walkable geometry from `scene` and bake it into `out`.
inline NavBakeStats bake_navmesh_from_scene(const std::shared_ptr<schizo::scene::Scene>& scene,
                                            schizo::ai::NavMesh& out,
                                            float max_slope_deg = 50.0f) {
    NavBakeStats st;
    if (!scene) return st;

    schizo::ai::NavMeshBuilder nb;
    nb.set_max_slope_deg(max_slope_deg);

    for (const auto& ent : scene->GetEntities()) {
        if (!ent || !ent->IsActive()) continue;
        auto* tf = ent->GetTransform();
        if (!tf) continue;
        const glm::mat4 model = tf->GetWorldMatrix();

        // ---- terrain ------------------------------------------------------
        if (auto tc = ent->GetComponent<schizo::scene::TerrainComponent>()) {
            const int   res   = tc->GetResolution();
            const int   n     = tc->VertsPerSide();
            const float size  = tc->GetSize();
            const float cell  = (res > 0) ? size / static_cast<float>(res) : 0.0f;
            const float half  = size * 0.5f;
            const float hscale = tc->GetHeightScale();
            if (res <= 0 || cell <= 0.0f) continue;

            std::vector<glm::vec3> verts;
            std::vector<uint32_t>  idx;
            verts.reserve(static_cast<size_t>(n) * n);
            for (int z = 0; z < n; ++z)
                for (int x = 0; x < n; ++x)
                    verts.push_back({-half + x * cell,
                                     tc->HeightAt(x, z) * hscale,
                                     -half + z * cell});

            for (int z = 0; z < res; ++z)
                for (int x = 0; x < res; ++x) {
                    // A hole is a hole for navigation too — otherwise agents
                    // path straight across a cave mouth they would fall into.
                    if (tc->HasHole(x, z)) continue;
                    const uint32_t a = static_cast<uint32_t>(z) * n + x;
                    const uint32_t b = a + 1;
                    const uint32_t c = a + n;
                    const uint32_t d = c + 1;
                    idx.insert(idx.end(), {a, c, b,  b, c, d});
                }

            if (!idx.empty()) {
                nb.add_indexed(verts, idx, model);
                ++st.terrains;
            }
            continue;   // a terrain entity is not also a walkable box
        }

        // ---- static box colliders: the TOP face ---------------------------
        if (auto col = ent->GetComponent<schizo::scene::ColliderComponent>()) {
            if (col->GetShape() != schizo::scene::ColliderShape::Box) {
                ++st.skipped_shapes;
                continue;
            }
            const glm::vec3 h = col->GetHalfExtents();
            // Top face only. Emitting the sides would give agents a navmesh
            // they can path up vertical walls on.
            const glm::vec3 p0(-h.x, h.y, -h.z);
            const glm::vec3 p1( h.x, h.y, -h.z);
            const glm::vec3 p2( h.x, h.y,  h.z);
            const glm::vec3 p3(-h.x, h.y,  h.z);
            std::vector<glm::vec3> verts{p0, p1, p2, p3};
            std::vector<uint32_t>  idx{0, 2, 1, 0, 3, 2};
            nb.add_indexed(verts, idx, model);
            ++st.boxes;
        }
    }

    st.triangles = nb.walkable_triangle_count();
    st.ok = nb.build(out);
    if (!st.ok) {
        spdlog::warn("[nav] bake produced no walkable geometry "
                     "({} terrain(s), {} box collider(s), {} shape(s) skipped)",
                     st.terrains, st.boxes, st.skipped_shapes);
    } else {
        spdlog::info("[nav] baked {} walkable triangles from {} terrain(s) and {} box collider(s)"
                     "{}", st.triangles, st.terrains, st.boxes,
                     st.skipped_shapes ? fmt::format(" — {} collider(s) skipped (unsupported shape)",
                                                     st.skipped_shapes)
                                       : std::string());
    }
    return st;
}

}  // namespace schizo::editor
