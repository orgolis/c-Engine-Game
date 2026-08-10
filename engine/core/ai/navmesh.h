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

    /// True if `p` is actually ON the mesh — some triangle's XZ footprint
    /// contains it.
    ///
    /// Distinct from nearest_tri(), which never says no: it falls back to the
    /// nearest centroid, so a point a thousand units away still gets a
    /// triangle. find_path() snaps through that same fallback, which is why an
    /// off-mesh query returns a confident straight line instead of failing.
    /// Callers that need to know whether an agent is standing on navigable
    /// ground have to ask this instead.
    bool contains(const glm::vec3& p) const;

    /// Index of the triangle containing `p` (XZ), else the nearest by centroid.
    /// -1 if the mesh is empty.
    int nearest_tri(const glm::vec3& p) const;

    /// Interpolated mesh height (world Y) at XZ `p` on triangle `tri`.
    float height_on(int tri, const glm::vec3& p) const;

    /// Move the whole mesh by `d`, for a floating-origin rebase.
    ///
    /// A rebase moves every entity in the world but leaves a baked navmesh
    /// where it was, so agents end up pathing against geometry offset by the
    /// shift -- walking into walls, or off the mesh entirely, since the shift
    /// is far larger than the mesh. Translating is exact and cheap: topology,
    /// adjacency and winding are all unchanged by a rigid translation, so only
    /// the positions move and nothing has to be rebuilt.
    void translate(const glm::vec3& d) {
        if (d == glm::vec3(0.0f)) return;
        for (Tri& t : tris_) {
            t.v[0] += d; t.v[1] += d; t.v[2] += d;
            // RECOMPUTED, not shifted. Translating the stored centroid loses
            // precision -- a rebase of 512 pushes it into a coarser part of the
            // float range and it does not come back, so translating out and
            // home again leaves the mesh subtly different from the one that was
            // baked. A* costs are centroid distances, ties on an open mesh are
            // common, and a perturbed tie picks a different corridor. Deriving
            // it from the vertices keeps the centroid true and makes the
            // operation exactly reversible, which matters because rebases
            // accumulate over a session.
            t.centroid = (t.v[0] + t.v[1] + t.v[2]) / 3.0f;
        }
    }

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
