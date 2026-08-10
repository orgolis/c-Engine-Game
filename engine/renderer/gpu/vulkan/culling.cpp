/**
 * @file culling.cpp
 * @brief Frustum culling implementation.
 */

#include "culling.h"
#include "vulkan_render_graph.h"
#include "vulkan_scene_mesh.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include "jobs/job_system.h"

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
    // the positive side (or straddling) at least one plane. A small world-
    // space slack keeps boxes that graze a plane (numerical boundary) from
    // flickering between visible/culled across frames — a borderline object
    // must be CLEARLY outside before it's dropped.
    constexpr float kSlack = 0.05f;   // world units
    for (int i = 0; i < plane_count; ++i) {
        float dist = planes_[i].distance_to_aabb(box);
        if (dist < -kSlack) {
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

    stats->input_items = static_cast<uint32_t>(items.size());

    // Single-pass stable compaction (the old erase-in-loop was O(n²): every
    // erase shifted the whole tail; on a 10k-draw list that's ~50M moves).
    //
    // Per item, a bounding-SPHERE test runs first — one matrix*vec4 + six
    // dot products. Only when the sphere straddles a plane does the exact
    // 8-corner AABB transform + test run. Fully-outside and fully-inside
    // items (the vast majority) never pay the corner transform.
    auto keep = [&](const DrawItem& item) -> bool {
        if (!item.mesh) return false;

        const AABB& local = item.mesh->bounding_box();

        // World-space bounding sphere: transformed center + radius scaled by
        // the largest column length (max scale factor) of the model matrix.
        const glm::vec3 center =
            glm::vec3(item.model * glm::vec4(local.center(), 1.0f));
        const float sx = glm::length(glm::vec3(item.model[0]));
        const float sy = glm::length(glm::vec3(item.model[1]));
        const float sz = glm::length(glm::vec3(item.model[2]));
        const float radius =
            local.bounding_radius() * std::max(sx, std::max(sy, sz));

        bool fully_inside = true;
        for (int i = 0; i < Frustum::plane_count; ++i) {
            const float dist = frustum.planes()[i].distance_to_point(center);
            if (dist < -radius) return false;          // sphere fully outside
            if (dist < radius)  fully_inside = false;  // straddles this plane
        }
        if (fully_inside) return true;                 // sphere fully inside

        // Borderline: exact (conservative) AABB test.
        return frustum.is_visible(local.transform(item.model));
    };

    // The TEST is embarrassingly parallel; the COMPACTION is not, because the
    // output order has to stay stable (draw order affects both batching and
    // blended geometry). So: evaluate the mask across workers, then compact in
    // one cheap serial pass over a byte array.
    //
    // Only above a threshold. Below it the job-system fan-out costs more than
    // the work it distributes, and a "parallel" version that is slower on the
    // scenes people actually have would be a regression dressed as an
    // optimisation. kParallelCullMin was picked by measuring both paths (see
    // tools/cullbench_check).
    constexpr size_t kParallelCullMin = 2048;

    size_t out = 0;
    if (items.size() >= kParallelCullMin) {
        std::vector<uint8_t> visible(items.size(), 0);
        gws::jobs::JobSystem::instance().parallel_for(
            0, items.size(), [&](size_t i) { visible[i] = keep(items[i]) ? 1u : 0u; });
        for (size_t i = 0; i < items.size(); ++i) {
            if (visible[i]) {
                if (out != i) items[out] = items[i];
                ++out;
            } else {
                stats->culled_frustum++;
            }
        }
    } else {
        for (size_t i = 0; i < items.size(); ++i) {
            if (keep(items[i])) {
                if (out != i) items[out] = items[i];
                ++out;
            } else {
                stats->culled_frustum++;
            }
        }
    }
    items.resize(out);

    stats->output_items = static_cast<uint32_t>(items.size());

    spdlog::debug("Frustum culling: {} → {} items (culled {})",
                  stats->input_items, stats->output_items, stats->culled_frustum);
}

} // namespace gws::renderer::gpu
