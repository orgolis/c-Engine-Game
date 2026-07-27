#pragma once

// ============================================================================
// Inverse Kinematics — two-bone analytic solver + foot IK. (Animation Stage 5.)
//
// Runs as a POST-PASS after a pose is applied to the skeleton: the state
// machine produces the animated pose, then IK adjusts limbs to reach targets
// (foot planted on uneven ground, hand on a ledge/weapon grip). Analytic
// (law-of-cosines) two-bone IK is exact and O(1) — no iteration.
// ============================================================================

#include "skeleton.h"
#include <glm/glm.hpp>
#include <functional>

namespace schizo::renderer {

/// Solve a two-bone chain (a = hip/shoulder, b = knee/elbow, c = ankle/wrist)
/// so `c` reaches `target_world`, bending the joint toward `pole_world` (a hint
/// point defining the bend plane — e.g. in front of the knee). Writes new LOCAL
/// rotations to bones a and b and refreshes world transforms. `weight` in
/// [0,1] blends between the animated pose (0) and the fully-solved pose (1).
///
/// The skeleton's world transforms must be current on entry (call
/// UpdateWorldTransforms or ApplyPoseToSkeleton first).
void SolveTwoBoneIK(Skeleton& skel,
                    uint16_t a, uint16_t b, uint16_t c,
                    const glm::vec3& target_world,
                    const glm::vec3& pole_world,
                    float weight = 1.0f);

/// Ground query used by foot IK. Trace a ray from `origin` along `dir` for up to
/// `max_dist`; on hit, fill `hit_pos`/`hit_normal` and return true. The editor
/// wires this to PhysicsWorld::raycast; a headless test can supply a flat plane.
using GroundQueryFn =
    std::function<bool(const glm::vec3& origin, const glm::vec3& dir, float max_dist,
                       glm::vec3& hit_pos, glm::vec3& hit_normal)>;

/// One leg's three-bone chain (bone ids).
struct LegChain {
    uint16_t hip   = 0;
    uint16_t knee  = 0;
    uint16_t ankle = 0;
    /// Local +axis of the ankle bone that points down toward the sole, used to
    /// offset the IK target so the sole (not the ankle joint) sits on the
    /// ground. Length = ankle-to-sole distance in world units.
    float    foot_height = 0.1f;
};

struct FootIkParams {
    float trace_up   = 0.5f;   // start the ray this far ABOVE the animated foot
    float trace_down = 0.8f;   // and cast this far below it
    float max_pelvis_drop = 0.6f;  // clamp how far the pelvis can sink
    float weight     = 1.0f;   // overall IK blend
    bool  align_to_normal = true;  // rotate the foot to the ground normal
};

/// Foot IK for a biped: raycast under each foot, drop the pelvis (bone
/// `pelvis`) so the lower foot can reach, then two-bone-IK each leg onto its
/// ground point and (optionally) align the ankle to the surface normal.
/// Returns the pelvis vertical offset that was applied (<= 0).
float SolveFootIK(Skeleton& skel,
                  uint16_t pelvis,
                  const LegChain& left,
                  const LegChain& right,
                  const GroundQueryFn& ground,
                  const FootIkParams& params = {});

} // namespace schizo::renderer
