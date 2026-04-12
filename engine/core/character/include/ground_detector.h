#pragma once

#include <glm/glm.hpp>
#include <entt/entt.hpp>

namespace engine::character {

/**
 * @struct GroundHit
 * @brief Result of a ground detection raycast
 */
struct GroundHit {
    bool hit = false;
    glm::vec3 point = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0, 1, 0);
    float distance = 0.0f;
    entt::entity hit_entity = entt::null;
};

/**
 * @class GroundDetector
 * @brief Detects if character is on ground using raycasting
 *
 * This class performs raycasts downward from the character's position
 * to determine:
 * - Is the character on ground?
 * - What is the ground normal?
 * - Where is the ground contact point?
 *
 * This is essential for:
 * - Jump state transitions
 * - Landing damage calculation
 * - Ground friction application
 * - Animation state selection
 */
class GroundDetector {
public:
    static constexpr float GROUND_CHECK_DISTANCE = 0.3f;
    static constexpr float GROUND_CHECK_RADIUS = 0.2f;

    /**
     * Create ground detector for a character
     * @param entity The character entity to detect ground for
     */
    explicit GroundDetector(entt::entity entity);

    /**
     * Update ground detection (perform raycasts)
     */
    void Update();

    /**
     * Check if character is on ground
     * @return true if on solid ground
     */
    bool IsOnGround() const { return last_hit_.hit; }

    /**
     * Get the normal of the ground surface
     * @return Ground normal vector
     */
    glm::vec3 GetGroundNormal() const { return last_hit_.normal; }

    /**
     * Get the contact point on the ground
     * @return World position of ground contact
     */
    glm::vec3 GetGroundContactPoint() const { return last_hit_.point; }

    /**
     * Get the distance to ground
     * @return Distance in meters
     */
    float GetDistanceToGround() const { return last_hit_.distance; }

    /**
     * Get the full ground hit information
     */
    const GroundHit& GetLastHit() const { return last_hit_; }

    /**
     * Check if character just landed (was in air, now on ground)
     */
    bool JustLanded() const { return is_on_ground_ && !was_on_ground_; }

    /**
     * Check if character just left ground (was on ground, now in air)
     */
    bool JustLeftGround() const { return !is_on_ground_ && was_on_ground_; }

private:
    entt::entity entity_;
    GroundHit last_hit_;
    bool is_on_ground_ = false;
    bool was_on_ground_ = false;

    /**
     * Perform raycast from character position downward
     * @return GroundHit with results
     */
    GroundHit PerformRaycast();

    /**
     * Check if a surface slope is walkable
     * @param normal The surface normal
     * @return true if slope is not too steep
     */
    bool IsWalkableSlope(const glm::vec3& normal) const;
};

} // namespace engine::character
