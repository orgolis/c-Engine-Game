#include "collision.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>

namespace schizo::physics {

// ============================================================================
// CollisionShape Implementation
// ============================================================================

glm::mat4 CollisionShape::GetLocalTransform() const {
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), local_offset_);
    glm::mat4 rotation = glm::mat4_cast(local_rotation_);
    return translation * rotation;
}

// ============================================================================
// SphereShape Implementation
// ============================================================================
// (All implementations are inline in header)

// ============================================================================
// BoxShape Implementation
// ============================================================================
// (all implementations are inline in header)

// ============================================================================
// CapsuleShape Implementation
// ============================================================================
// (all implementations are inline in header)

// ============================================================================
// CylinderShape Implementation
// ============================================================================
// (all implementations are inline in header)

// ============================================================================
// ConeShape Implementation
// ============================================================================
// (all implementations are inline in header)

// ============================================================================
// PlaneShape Implementation
// ============================================================================
// (all implementations are inline in header)

// ============================================================================
// Collision Detection Helper Functions
// ============================================================================
// NOTE: Collision detection is handled in PhysicsWorld.
// These functions are disabled due to GLM API compatibility issues with 
// quaternion-vector rotation operations.

} // namespace schizo::physics

