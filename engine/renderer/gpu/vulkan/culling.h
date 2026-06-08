/**
 * @file culling.h
 * @brief GPU-agnostic culling utilities for the deferred pipeline (Phase 5 Week 3).
 *
 * Provides frustum-based CPU culling and HZB-based GPU culling to reduce draw calls.
 * Frustum culling is a fast baseline; HZB provides occlusion-aware filtering.
 */

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <cstdint>
#include <vector>

namespace gws::renderer::gpu {

// Forward declarations
struct DrawItem;

/**
 * @struct AABB
 * @brief Axis-aligned bounding box for frustum testing.
 */
struct AABB {
    glm::vec3 min = glm::vec3(0.0f);
    glm::vec3 max = glm::vec3(0.0f);

    AABB() = default;
    AABB(glm::vec3 min_v, glm::vec3 max_v) : min(min_v), max(max_v) {}

    /// Check if AABB is degenerate (any min strictly greater than max).
    /// Note: an AABB with `min == max` on one axis is a *valid* flat box
    /// (e.g. the bounding box of a ground plane). It should still cull
    /// correctly against the frustum, so `>=` would be too aggressive.
    bool is_degenerate() const {
        return min.x > max.x || min.y > max.y || min.z > max.z;
    }

    /// Get the bounding sphere radius (half diagonal).
    float bounding_radius() const {
        return glm::length(max - min) * 0.5f;
    }

    /// Get the center of the box.
    glm::vec3 center() const {
        return (min + max) * 0.5f;
    }

    /// Transform AABB by a 4x4 matrix, returning new AABB (conservative).
    AABB transform(const glm::mat4& m) const {
        glm::vec3 corners[8] = {
            {min.x, min.y, min.z},
            {max.x, min.y, min.z},
            {min.x, max.y, min.z},
            {max.x, max.y, min.z},
            {min.x, min.y, max.z},
            {max.x, min.y, max.z},
            {min.x, max.y, max.z},
            {max.x, max.y, max.z},
        };

        glm::vec3 new_min(1e9f);
        glm::vec3 new_max(-1e9f);

        for (int i = 0; i < 8; ++i) {
            glm::vec4 p = m * glm::vec4(corners[i], 1.0f);
            glm::vec3 p3 = glm::vec3(p) / p.w;
            new_min = glm::min(new_min, p3);
            new_max = glm::max(new_max, p3);
        }

        return AABB(new_min, new_max);
    }
};

/**
 * @struct Plane
 * @brief A 3D plane represented as (n, d) where equation is n·x + d = 0.
 *
 * Used for frustum culling. A point is "inside" (on the positive side) if
 * n·p + d >= 0.
 */
struct Plane {
    glm::vec3 normal = glm::vec3(0.0f, 0.0f, 1.0f);
    float     offset = 0.0f;

    Plane() = default;
    Plane(glm::vec3 n, float d) : normal(glm::normalize(n)), offset(d) {}

    /// Signed distance from a point to the plane (positive = inside).
    float distance_to_point(const glm::vec3& p) const {
        return glm::dot(normal, p) + offset;
    }

    /// Signed distance from an AABB. Negative means the box is entirely on
    /// the negative side; positive means inside or straddling.
    float distance_to_aabb(const AABB& box) const {
        glm::vec3 p = box.min;
        if (normal.x > 0.0f) p.x = box.max.x;
        if (normal.y > 0.0f) p.y = box.max.y;
        if (normal.z > 0.0f) p.z = box.max.z;

        // Positive extent (toward the plane's positive side)
        return glm::dot(normal, p) + offset;
    }
};

/**
 * @class Frustum
 * @brief View frustum defined by 6 planes (left, right, top, bottom, near, far).
 *
 * Extract from view+projection matrices. Supports quick AABB rejection tests.
 */
class Frustum {
public:
    /// Build from combined VP matrix. Extracts 6 planes in the standard order.
    static Frustum from_matrix(const glm::mat4& vp);

    Frustum() = default;

    /// Test if an AABB is visible (intersects at least one frustum plane positively).
    bool is_visible(const AABB& box) const;

    /// Test if a sphere is visible.
    bool is_sphere_visible(const glm::vec3& center, float radius) const;

    const Plane* planes() const { return planes_; }
    static constexpr int plane_count = 6;

private:
    Plane planes_[6];  // LEFT, RIGHT, TOP, BOTTOM, NEAR, FAR
};

/**
 * @struct CullingStats
 * @brief Counters for culling results.
 */
struct CullingStats {
    uint32_t input_items   = 0;
    uint32_t output_items  = 0;
    uint32_t culled_frustum = 0;
    uint32_t culled_occlusion = 0;
};

/**
 * @brief Filter a draw list using frustum culling.
 *
 * Mutates the input list in-place, removing invisible items. Each DrawItem
 * references a Mesh whose bounding box is tested against the frustum.
 *
 * @param items       [in/out] Draw items to cull (resized to output count).
 * @param frustum     [in]     Frustum to test against.
 * @param stats       [out]    Optional statistics (nullptr to skip).
 */
void cull_draw_items_frustum(std::vector<DrawItem>& items,
                              const Frustum& frustum,
                              CullingStats* stats = nullptr);

} // namespace gws::renderer::gpu
