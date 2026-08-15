#pragma once
// ============================================================================
// level_tools — arrays, scatter and splines for level design (4.7).
//
// Snapping (already done) makes placement exact. This is the rest of the item:
// the operations that place MANY things, because doing it one object at a time
// is where level building actually costs its time. A fence of forty posts
// placed by hand is forty chances to be one unit out, and the forty-first is
// the one someone notices.
//
// The maths lives here, apart from the editor, because it is what can be
// quietly wrong. An array whose spacing drifts, a scatter that clumps, a spline
// that does not pass through its own control points -- none of those look
// broken in a screenshot, and all of them are arithmetic.
//
// Deterministic by construction. Scatter takes an explicit seed and the same
// seed gives the same layout, so a level that looked right yesterday looks the
// same today. A scatter that reshuffles on every reload is one nobody can
// build on.
// ============================================================================

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <vector>

namespace schizo::editor {

// ---- placements -------------------------------------------------------------
/// One instance an operation wants created. Rotation and scale are carried so a
/// scatter can vary them; an array normally leaves them alone.
struct Placement {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

// ---- linear / grid array -----------------------------------------------------
struct ArrayParams {
    int       count_x = 4;
    int       count_y = 1;
    int       count_z = 1;
    glm::vec3 step{2.0f, 0.0f, 0.0f};      // offset between neighbours
    bool      center = false;              // centre the result on the source
};

/// Positions for an array starting at `origin`.
///
/// The FIRST placement is the source's own position, so an array of 4 adds
/// three new objects rather than four stacked on the original. Getting that
/// wrong leaves a duplicate exactly on top of the source, which is invisible
/// until something behaves oddly.
std::vector<Placement> build_array(const glm::vec3& origin, const ArrayParams& p);

// ---- scatter ------------------------------------------------------------------
struct ScatterParams {
    int       count        = 12;
    float     radius       = 10.0f;
    float     min_distance = 0.0f;   // 0 = allow overlap
    bool      random_yaw   = true;
    float     scale_min    = 1.0f;
    float     scale_max    = 1.0f;
    uint32_t  seed         = 1337u;
    int       max_attempts = 32;     // per instance, before giving up
};

/// Scatter within a disc on the XZ plane around `center`.
///
/// Rejection-sampled against `min_distance`, and it can legitimately return
/// FEWER than `count`: asking for fifty trees three metres apart inside a
/// five-metre circle is not satisfiable, and silently overlapping them is worse
/// than honestly placing what fits.
///
/// Uses its own small PRNG rather than std::rand, which is process-global: a
/// scatter whose result depends on what else called rand() first is not
/// reproducible even with a fixed seed.
std::vector<Placement> build_scatter(const glm::vec3& center, const ScatterParams& p);

// ---- spline --------------------------------------------------------------------
/// Catmull-Rom through its control points.
///
/// Chosen over Bezier deliberately: it INTERPOLATES its control points, so the
/// curve passes through every point the user placed. A Bezier only approximates
/// them, and "the path does not go where I put it" is the first complaint about
/// any spline tool that uses one.
class Spline {
public:
    void add_point(const glm::vec3& p) { points_.push_back(p); }
    void clear() { points_.clear(); }
    std::vector<glm::vec3>&       points()       { return points_; }
    const std::vector<glm::vec3>& points() const { return points_; }
    size_t size() const { return points_.size(); }

    /// Position at `t` in [0,1] across the whole spline. Clamped at both ends.
    glm::vec3 evaluate(float t) const;

    /// Approximate arc length, sampled. Used for even spacing, which is what
    /// "place a fence along this path" actually needs -- spacing evenly in `t`
    /// bunches instances where the curve is tight.
    float length(int samples = 64) const;

    /// `count` placements spaced by ARC LENGTH, optionally facing along the
    /// curve. Even spacing in t would crowd corners and stretch straights,
    /// which is exactly where it is most visible.
    std::vector<Placement> distribute(int count, bool align_to_tangent) const;

private:
    std::vector<glm::vec3> points_;
};

}  // namespace schizo::editor
