#pragma once

// ============================================================================
// FloatingOrigin — large-world coordinate precision. (World-streaming pillar.)
// Far from the origin, 32-bit float positions lose precision (jitter). This
// keeps a DOUBLE-precision accumulated world origin and, when the focus (camera/
// player) drifts past a threshold, rebases: it returns a shift to apply to ALL
// local positions (camera + entities) so the focus is back near local origin,
// while the absolute world position (origin + local) is preserved exactly.
// ============================================================================

#include <glm/glm.hpp>
#include <cmath>

namespace schizo::world {

class FloatingOrigin {
public:
    explicit FloatingOrigin(double threshold = 1024.0, double snap = 64.0)
        : threshold_(threshold), snap_(snap) {}

    void set_threshold(double t) { threshold_ = t; }
    void set_snap(double s) { snap_ = s; }

    glm::dvec3 origin() const { return origin_; }

    /// Absolute (double-precision) world position of a local point.
    glm::dvec3 to_world(const glm::vec3& local) const {
        return origin_ + glm::dvec3(local);
    }

    /// Given the focus's LOCAL position (relative to the current origin), decide
    /// whether to rebase. If |focus_local| (XZ) exceeds the threshold, snap a
    /// shift to the grid quantum, accumulate it into the origin, and set
    /// `out_shift` to the delta the caller must ADD to every local position
    /// (camera + all entities). Returns true iff a rebase occurred.
    bool update(const glm::vec3& focus_local, glm::vec3& out_shift) {
        out_shift = glm::vec3(0.0f);
        const double dx = focus_local.x, dz = focus_local.z;
        if (std::sqrt(dx * dx + dz * dz) <= threshold_) return false;

        // Snap the shift to `snap_` so the streaming grid stays cell-aligned.
        const double sx = std::round(dx / snap_) * snap_;
        const double sz = std::round(dz / snap_) * snap_;
        origin_.x += sx;
        origin_.z += sz;
        out_shift = glm::vec3(static_cast<float>(-sx), 0.0f, static_cast<float>(-sz));
        return true;
    }

private:
    glm::dvec3 origin_{0.0};
    double     threshold_;
    double     snap_;
};

} // namespace schizo::world
