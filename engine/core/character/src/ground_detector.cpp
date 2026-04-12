#include "ground_detector.h"
#include <spdlog/spdlog.h>
#include <glm/geometric.hpp>

namespace engine::character {

GroundDetector::GroundDetector(entt::entity entity)
    : entity_(entity) {
}

void GroundDetector::Update() {
    was_on_ground_ = is_on_ground_;
    last_hit_ = PerformRaycast();
    is_on_ground_ = last_hit_.hit;
}

GroundHit GroundDetector::PerformRaycast() {
    GroundHit result;
    
    // TODO: Implement actual physics raycast
    // This would require access to the physics engine
    // For now, return a stub implementation
    
    // In a real implementation:
    // 1. Get character position from Transform component
    // 2. Raycast downward from position using physics engine
    // 3. Find closest hit below the character
    // 4. Return GroundHit with results
    
    result.hit = false;
    result.distance = GROUND_CHECK_DISTANCE + 1.0f;
    result.normal = glm::vec3(0, 1, 0);
    result.point = glm::vec3(0, 0, 0);
    
    return result;
}

bool GroundDetector::IsWalkableSlope(const glm::vec3& normal) const {
    // Check if slope is not too steep
    // If the upward component is too small, it's too steep to walk on
    float max_slope_angle = glm::radians(45.0f);  // 45 degree max slope
    float min_upward_component = glm::cos(max_slope_angle);
    
    return normal.y >= min_upward_component;
}

} // namespace engine::character
