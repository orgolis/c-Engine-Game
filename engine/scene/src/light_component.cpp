#include "light_component.h"
#include "transform.h"
#include "entity.h"
#include <spdlog/spdlog.h>
#include <glm/gtc/constants.hpp>

namespace schizo::scene {

// ============================================================================
// LightComponent Implementation
// ============================================================================

LightComponent::LightComponent(LightType type, const std::string& name)
    : light_type_(type), name_(name) {
}

void LightComponent::OnAttach() {
    if (!entity_) return;
    
    spdlog::debug("LightComponent attached to {}: {} light",
                  entity_->GetName(),
                  light_type_ == LightType::Directional ? "Directional" :
                  light_type_ == LightType::Point ? "Point" : "Spot");
}

void LightComponent::OnDetach() {
    spdlog::debug("LightComponent detached from {}", entity_ ? entity_->GetName() : "unknown");
}

void LightComponent::OnUpdate(float delta_time) {
    // Light properties are synchronized with entity transform automatically
    // via GetPosition() and GetDirection() methods
}

glm::vec3 LightComponent::GetPosition() const {
    if (entity_) {
        return entity_->GetTransform()->GetWorldPosition();
    }
    return glm::vec3(0.0f);
}

glm::vec3 LightComponent::GetDirection() const {
    if (entity_) {
        return entity_->GetTransform()->GetForward();
    }
    return glm::vec3(0.0f, -1.0f, 0.0f);  // Default: down
}

glm::quat LightComponent::GetRotation() const {
    if (entity_) {
        return entity_->GetTransform()->GetWorldRotation();
    }
    return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // Identity
}

void LightComponent::UpdateColorFromTemperature() {
    // Tanner Helland's formula: convert Kelvin temperature to RGB
    float temp = temperature_ / 100.0f;
    
    float r, g, b;
    
    // Red
    if (temp <= 66.0f) {
        r = 255.0f;
    } else {
        r = temp - 60.0f;
        r = 329.698727446f * glm::pow(r, -0.1332047592f);
        r = glm::clamp(r, 0.0f, 255.0f);
    }
    
    // Green
    if (temp <= 66.0f) {
        g = temp;
        g = 99.4743258134f * glm::log(g) - 161.1195681661f;
    } else {
        g = temp - 60.0f;
        g = 288.1221695283f * glm::pow(g, -0.0755148492f);
    }
    g = glm::clamp(g, 0.0f, 255.0f);
    
    // Blue
    if (temp >= 66.0f) {
        b = 255.0f;
    } else if (temp <= 19.0f) {
        b = 0.0f;
    } else {
        b = temp - 10.0f;
        b = 138.5177312231f * glm::log(b) - 305.0447927307f;
        b = glm::clamp(b, 0.0f, 255.0f);
    }
    
    // Normalize to 0-1 range
    color_ = glm::vec3(r / 255.0f, g / 255.0f, b / 255.0f);
}

// ============================================================================
// DirectionalLightComponent Implementation
// ============================================================================

glm::vec3 DirectionalLightComponent::GetDirection() const {
    if (entity_) {
        return entity_->GetTransform()->GetForward();
    }
    return glm::vec3(0.0f, -1.0f, 0.0f);
}

} // namespace schizo::scene
