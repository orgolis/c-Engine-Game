// ============================================================================
// nav_builder.cpp — walkable-triangle collection + navmesh bake. See nav_builder.h.
// ============================================================================

#include "ai/nav_builder.h"
#include <cmath>
#include <cstdint>

namespace schizo::ai {

void NavMeshBuilder::set_max_slope_deg(float deg) {
    cos_max_slope_ = std::cos(deg * 3.14159265358979f / 180.0f);
}

void NavMeshBuilder::add_triangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 n = glm::cross(b - a, c - a);
    const float len = glm::length(n);
    if (len < 1e-8f) return;                       // degenerate
    const float up = std::abs(n.y) / len;          // |cos(slope)| (winding-agnostic)
    if (up < cos_max_slope_) return;               // too steep → not walkable
    tris_.push_back(a); tris_.push_back(b); tris_.push_back(c);
}

void NavMeshBuilder::add_indexed(const float* positions, size_t stride_floats,
                                 size_t pos_offset_floats, const uint32_t* indices,
                                 size_t index_count, const glm::mat4& model) {
    if (!positions || !indices) return;
    auto vert = [&](uint32_t i) {
        const float* p = positions + static_cast<size_t>(i) * stride_floats + pos_offset_floats;
        return glm::vec3(model * glm::vec4(p[0], p[1], p[2], 1.0f));
    };
    for (size_t i = 0; i + 2 < index_count; i += 3)
        add_triangle(vert(indices[i]), vert(indices[i + 1]), vert(indices[i + 2]));
}

void NavMeshBuilder::add_indexed(const std::vector<glm::vec3>& positions,
                                 const std::vector<uint32_t>& indices,
                                 const glm::mat4& model) {
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        if (indices[i] >= positions.size() || indices[i + 1] >= positions.size() ||
            indices[i + 2] >= positions.size()) continue;
        add_triangle(glm::vec3(model * glm::vec4(positions[indices[i]], 1.0f)),
                     glm::vec3(model * glm::vec4(positions[indices[i + 1]], 1.0f)),
                     glm::vec3(model * glm::vec4(positions[indices[i + 2]], 1.0f)));
    }
}

bool NavMeshBuilder::build(NavMesh& out, float weld_eps) const {
    if (tris_.empty()) return false;
    return out.build(tris_, weld_eps);
}

} // namespace schizo::ai
