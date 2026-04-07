#pragma once

#include <glm/glm.hpp>
#include <array>
#include <vector>

namespace schizo::renderer {

// Forward declaration
class Camera;

// ============================================================================
// Frustum Culling
// ============================================================================

/**
 * @enum FrustumPlane
 * @brief Frustum plane indices
 */
enum class FrustumPlane : uint8_t {
    Near = 0,
    Far = 1,
    Left = 2,
    Right = 3,
    Top = 4,
    Bottom = 5,
};

/**
 * @struct Plane
 * @brief An oriented plane (normal + distance)
 */
struct Plane {
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
    float distance = 0.0f;
    
    Plane() = default;
    Plane(const glm::vec3& n, float d) : normal(n), distance(d) {}
    
    /**
     * Calculate signed distance from point to plane
     */
    float GetDistance(const glm::vec3& point) const {
        return glm::dot(normal, point) + distance;
    }
};

/**
 * @class Frustum
 * @brief View frustum for culling (6 planes: near, far, left, right, top, bottom)
 * 
 * Used for frustum culling to quickly reject geometry outside the view frustum
 * without submitting to GPU.
 */
class Frustum {
public:
    Frustum() = default;
    ~Frustum() = default;
    
    /**
     * Extract frustum planes from view-projection matrix
     * @param view_projection The combined view * projection matrix
     */
    void ExtractFromViewProjection(const glm::mat4& view_projection);
    
    /**
     * Extract frustum planes from camera
     * @param camera Camera object
     */
    void ExtractFromCamera(const Camera& camera);
    
    /**
     * Check if point is inside frustum
     * @param point World position
     * @return True if point is inside
     */
    bool ContainsPoint(const glm::vec3& point) const;
    
    /**
     * Check if sphere intersects frustum
     * @param center Sphere center
     * @param radius Sphere radius
     * @return True if sphere intersects
     */
    bool IntersectsSphere(const glm::vec3& center, float radius) const;
    
    /**
     * Check if AABB intersects frustum
     * @param min AABB minimum corner
     * @param max AABB maximum corner
     * @return True if AABB intersects
     */
    bool IntersectsAABB(const glm::vec3& min, const glm::vec3& max) const;
    
    /**
     * Get a specific frustum plane
     * @param plane Plane index
     * @return Reference to plane
     */
    const Plane& GetPlane(FrustumPlane plane) const {
        return planes_[static_cast<size_t>(plane)];
    }
    
    /**
     * Get all 6 planes
     */
    const std::array<Plane, 6>& GetPlanes() const { return planes_; }
    
    /**
     * Get frustum corner positions (8 corners)
     * @return Array of 8 corner positions
     */
    const std::array<glm::vec3, 8>& GetCorners() const { return corners_; }
    
private:
    std::array<Plane, 6> planes_;
    std::array<glm::vec3, 8> corners_;
    
    /**
     * Calculate frustum corner positions from view projection
     */
    void CalculateCorners(const glm::mat4& view_projection);
};

} // namespace schizo::renderer
