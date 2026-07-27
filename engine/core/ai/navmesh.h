#pragma once

// ============================================================================
// NavMesh — triangle navigation mesh with A* pathfinding + funnel string-pull.
// (AI Stage 9.) Built from a set of WALKABLE triangles (e.g. terrain cells and
// flat static-collider tops, filtered by slope). Triangles are the nav polys;
// two triangles sharing an edge are neighbours. `find_path` runs A* over the
// triangles then the Simple Stupid Funnel Algorithm through the shared-edge
// portals to produce a short, smooth point path.
//
// Pure CPU / no GPU or physics deps — the caller supplies walkable triangles.
// (Extracting walkable triangles from arbitrary geometry via voxelisation is a
// separate future step; terrain + flagged meshes provide them directly today.)
// ============================================================================

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace schizo::ai {

class NavMesh {
public:
    /// Build from a triangle soup — every 3 consecutive vertices form one
    /// walkable triangle. Triangles whose edges share two welded endpoints
    /// (within `weld_eps`) become neighbours. Winding is normalised to
    /// CCW-from-above so portal left/right are consistent. Returns false if
    /// there are no triangles.
    bool build(const std::vector<glm::vec3>& triangle_verts, float weld_eps = 0.02f);

    /// Find a smoothed path from `start` to `end`. Snaps both to the nearest
    /// triangle. On success fills `out_path` (front = start … back = end) and
    /// returns true. False if either endpoint is off-mesh or unreachable.
    bool find_path(const glm::vec3& start, const glm::vec3& end,
                   std::vector<glm::vec3>& out_path) const;

    /// Index of the triangle containing `p` (XZ), else the nearest by centroid.
    /// -1 if the mesh is empty.
    int nearest_tri(const glm::vec3& p) const;

    /// Interpolated mesh height (world Y) at XZ `p` on triangle `tri`.
    float height_on(int tri, const glm::vec3& p) const;

    size_t tri_count() const { return tris_.size(); }
    bool   empty()     const { return tris_.empty(); }

private:
    struct Tri {
        glm::vec3 v[3];
        glm::vec3 centroid{0.0f};
        int       neighbor[3] = {-1, -1, -1};  // across edge i = (v[i] -> v[i+1])
    };
    std::vector<Tri> tris_;

    bool astar(int start_tri, int end_tri, const glm::vec3& start,
               const glm::vec3& end, std::vector<int>& out_polys) const;
    void funnel(const std::vector<int>& polys, const glm::vec3& start,
                const glm::vec3& end, std::vector<glm::vec3>& out) const;
};

} // namespace schizo::ai
