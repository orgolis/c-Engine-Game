#pragma once

// ============================================================================
// NavMeshBuilder — collect walkable triangles from scene geometry and bake a
// NavMesh. (AI Stage 9 integration.) A triangle is walkable when its upward
// normal is within `max_slope` of world-up; steep walls/ceilings are dropped.
// The caller feeds terrain chunk meshes + static collider meshes (with their
// model transforms); the builder world-transforms + slope-filters them.
// ============================================================================

#include "ai/navmesh.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace schizo::ai {

class NavMeshBuilder {
public:
    void clear() { tris_.clear(); }
    void set_max_slope_deg(float deg);

    /// Add a world-space triangle if it is walkable (slope test).
    void add_triangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);

    /// Add an indexed mesh (positions + uint32 indices), transformed by `model`,
    /// slope-filtered. `stride_floats`/`pos_offset_floats` let callers pass an
    /// interleaved vertex buffer (e.g. SceneVertex: stride 12, pos at 0).
    void add_indexed(const float* positions, size_t stride_floats, size_t pos_offset_floats,
                     const uint32_t* indices, size_t index_count, const glm::mat4& model);

    /// Convenience: a flat vec3 position array + indices.
    void add_indexed(const std::vector<glm::vec3>& positions,
                     const std::vector<uint32_t>& indices, const glm::mat4& model);

    /// Bake into `out`. Returns false if nothing walkable was collected.
    bool build(NavMesh& out, float weld_eps = 0.05f) const;

    size_t walkable_triangle_count() const { return tris_.size() / 3; }

private:
    std::vector<glm::vec3> tris_;      // flat soup (3 verts / tri)
    float cos_max_slope_ = 0.70710678f; // cos(45°)
};

} // namespace schizo::ai
