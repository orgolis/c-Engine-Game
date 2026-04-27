/**
 * @file culling.cpp
 * @brief Frustum culling implementation.
 */

#include "culling.h"
#include "vulkan_render_graph.h"
#include "vulkan_scene_mesh.h"

#include <spdlog/spdlog.h>
#include <algorithm>

namespace gws::renderer::gpu {

// Helper: extract frustum plane from VP matrix row.
// Standard extraction: multiply VP by plane normals to get plane equations.
static Plane extract_plane_from_matrix(const glm::mat4& vp, int plane_index) {
    // Vulkan/OpenGL frustum extraction:
    // LEFT:   (1, 3) - (0, 3)
    // RIGHT:  (1, 3) + (0, 3)
    // TOP:    (1, 3) + (1, 3)
    // BOTTOM: (1, 3) - (1, 3)
    // NEAR:   (2, 3)
    // FAR:    (3, 3) - (2, 3)

    glm::vec4 row0 = glm::row(vp, 0);
    glm::vec4 row1 = glm::row(vp, 1);
    glm::vec4 row2 = glm::row(vp, 2);
    glm::vec4 row3 = glm::row(vp, 3);

    glm::vec4 plane_vec;
    switch (plane_index) {
        case 0: // LEFT
            plane_vec = row3 + row0;
            break;
        case 1: // RIGHT
            plane_vec = row3 - row0;
            break;
        case 2: // TOP
            plane_vec = row3 - row1;
            break;
        case 3: // BOTTOM
            plane_vec = row3 + row1;
            break;
        case 4: // NEAR
            plane_vec = row2;
            break;
        case 5: // FAR
            plane_vec = row3 - row2;
            break;
        default:
            plane_vec = glm::vec4(0.0f);
    }

    // Plane equation: n·x + d = 0. The extracted vector is (n.x, n.y, n.z, d).
    glm::vec3 normal(plane_vec);
    float offset = plane_vec.w;

    // Normalize the plane so the normal has unit length.
    float len = glm::length(normal);
    if (len > 1e-6f) {
        normal /= len;
        offset /= len;
    }

    return Plane(normal, offset);
}

Frustum Frustum::from_matrix(const glm::mat4& vp) {
    Frustum f;
    for (int i = 0; i < plane_count; ++i) {
        f.planes_[i] = extract_plane_from_matrix(vp, i);
    }
    return f;
}

bool Frustum::is_visible(const AABB& box) const {
    if (box.is_degenerate()) return false;

    // Test AABB against each frustum plane. The AABB is visible if it's on
    // the positive side (or straddling) at least one plane.
    for (int i = 0; i < plane_count; ++i) {
        float dist = planes_[i].distance_to_aabb(box);
        if (dist < 0.0f) {
            // Box is entirely on the negative side of this plane → culled.
            return false;
        }
    }
    return true;
}

bool Frustum::is_sphere_visible(const glm::vec3& center, float radius) const {
    for (int i = 0; i < plane_count; ++i) {
        float dist = planes_[i].distance_to_point(center);
        if (dist < -radius) {
            // Sphere center + radius is outside this plane → culled.
            return false;
        }
    }
    return true;
}

void cull_draw_items_frustum(std::vector<DrawItem>& items,
                              const Frustum& frustum,
                              CullingStats* stats) {
    CullingStats temp_stats{};
    if (!stats) stats = &temp_stats;

    stats->input_items = items.size();

    auto it = items.begin();
    while (it != items.end()) {
        const DrawItem& item = *it;
        bool visible = false;

        if (item.mesh) {
            // Get the mesh's bounding box and transform it by the model matrix.
            AABB world_box = item.mesh->bounding_box().transform(item.model);
            visible = frustum.is_visible(world_box);
        }

        if (visible) {
            ++it;
        } else {
            stats->culled_frustum++;
            it = items.erase(it);
        }
    }

    stats->output_items = items.size();

    spdlog::debug("Frustum culling: {} → {} items (culled {})",
                  stats->input_items, stats->output_items, stats->culled_frustum);
}

} // namespace gws::renderer::gpu
