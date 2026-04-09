#pragma once

#include "entity.h"
#include <glm/glm.hpp>

namespace schizo::scene {

// ============================================================================
// Directional Light Component
// ============================================================================

/**
 * @class DirectionalLightComponent
 * @brief Component for directional light (sun-like light from far away)
 * Represents light coming from a specific direction at infinite distance
 */
class DirectionalLightComponent : public Component {
public:
    DirectionalLightComponent(
        const glm::vec3& color = glm::vec3(1.0f),
        float intensity = 1.0f,
        bool cast_shadow = true
    ) : color_(color), intensity_(intensity), cast_shadow_(cast_shadow) {}
    
    virtual ~DirectionalLightComponent() = default;
    
    // Deleted copy, allow move
    DirectionalLightComponent(const DirectionalLightComponent&) = delete;
    DirectionalLightComponent& operator=(const DirectionalLightComponent&) = delete;
    
    DirectionalLightComponent(DirectionalLightComponent&&) = default;
    DirectionalLightComponent& operator=(DirectionalLightComponent&&) = default;
    
    /**
     * @brief Get light color
     */
    const glm::vec3& GetColor() const { return color_; }
    
    /**
     * @brief Set light color
     */
    void SetColor(const glm::vec3& color) { color_ = color; }
    
    /**
     * @brief Get light intensity
     */
    float GetIntensity() const { return intensity_; }
    
    /**
     * @brief Set light intensity
     */
    void SetIntensity(float intensity) { intensity_ = std::max(0.0f, intensity); }
    
    /**
     * @brief Get shadow casting state
     */
    bool GetCastShadow() const { return cast_shadow_; }
    
    /**
     * @brief Set shadow casting
     */
    void SetCastShadow(bool cast_shadow) { cast_shadow_ = cast_shadow; }
    
    /**
     * @brief Get light direction (from entity's forward vector)
     */
    glm::vec3 GetDirection() const;
    
    /**
     * @brief Get shadow map distance/bias
     */
    float GetShadowBias() const { return shadow_bias_; }
    
    /**
     * @brief Set shadow map distance/bias
     */
    void SetShadowBias(float bias) { shadow_bias_ = bias; }
    
protected:
    glm::vec3 color_ = glm::vec3(1.0f, 1.0f, 1.0f);
    float intensity_ = 1.0f;
    bool cast_shadow_ = true;
    float shadow_bias_ = 0.005f;
};

// ============================================================================
// Global/Ambient Light Component
// ============================================================================

/**
 * @class GlobalLightComponent
 * @brief Component for global/ambient light
 * Represents uniform ambient lighting that affects all surfaces equally
 */
class GlobalLightComponent : public Component {
public:
    GlobalLightComponent(
        const glm::vec3& color = glm::vec3(0.3f),
        float intensity = 1.0f
    ) : color_(color), intensity_(intensity) {}
    
    virtual ~GlobalLightComponent() = default;
    
    // Deleted copy, allow move
    GlobalLightComponent(const GlobalLightComponent&) = delete;
    GlobalLightComponent& operator=(const GlobalLightComponent&) = delete;
    
    GlobalLightComponent(GlobalLightComponent&&) = default;
    GlobalLightComponent& operator=(GlobalLightComponent&&) = default;
    
    /**
     * @brief Get light color
     */
    const glm::vec3& GetColor() const { return color_; }
    
    /**
     * @brief Set light color
     */
    void SetColor(const glm::vec3& color) { color_ = color; }
    
    /**
     * @brief Get light intensity
     */
    float GetIntensity() const { return intensity_; }
    
    /**
     * @brief Set light intensity
     */
    void SetIntensity(float intensity) { intensity_ = std::max(0.0f, intensity); }
    
protected:
    glm::vec3 color_ = glm::vec3(0.3f, 0.3f, 0.3f);
    float intensity_ = 1.0f;
};

} // namespace schizo::scene
