#pragma once

#include "entity.h"
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>

namespace schizo::scene {

// ============================================================================
// Light Type & Quality Enums
// ============================================================================

/**
 * @enum LightType
 * @brief Types of light sources
 */
enum class LightType : uint8_t {
    Directional,  // Sun light, infinite distance
    Point,        // Omni-directional point light
    Spot,         // Cone-shaped directional light
    Area,         // Rectangular area light (LTC-shaded)
    MAX_TYPES
};

/**
 * @enum ShadowQuality
 * @brief Shadow map quality
 */
enum class ShadowQuality : uint8_t {
    None,          // No shadows
    Low,           // 512x512
    Medium,        // 1024x1024
    High,          // 2048x2048
    Ultra,         // 4096x4096
};

// ============================================================================
// Light Component
// ============================================================================

/**
 * @class LightComponent
 * @brief Component that attaches a light source to an entity
 * 
 * Simplified light system integrated directly into the editor without
 * depending on the full renderer module.
 */
class LightComponent : public Component {
public:
    /**
     * Create a light component with the specified type
     */
    LightComponent(LightType type = LightType::Point, const std::string& name = "Light");
    
    virtual ~LightComponent() = default;
    
    // Deleted copy, allow move
    LightComponent(const LightComponent&) = delete;
    LightComponent& operator=(const LightComponent&) = delete;
    
    LightComponent(LightComponent&&) = default;
    LightComponent& operator=(LightComponent&&) = default;
    
    // ========== Component Lifecycle ==========
    
    virtual void OnAttach() override;
    virtual void OnDetach() override;
    virtual void OnUpdate(float delta_time) override;
    
    // ========== Basic Properties ==========
    
    /**
     * Get light type
     */
    LightType GetType() const { return light_type_; }
    
    /**
     * Is light enabled
     */
    bool IsEnabled() const { return enabled_; }
    
    /**
     * Enable/disable light
     */
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    
    // ========== Color & Intensity ==========
    
    /**
     * Get light color
     */
    glm::vec3 GetColor() const { return color_; }
    
    /**
     * Set light color
     */
    void SetColor(const glm::vec3& color) { color_ = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f)); }
    
    /**
     * Set light color with individual components
     */
    void SetColor(float r, float g, float b) { SetColor(glm::vec3(r, g, b)); }
    
    /**
     * Get light intensity
     */
    float GetIntensity() const { return intensity_; }
    
    /**
     * Set light intensity (brightness)
     */
    void SetIntensity(float intensity) { intensity_ = std::max(0.0f, intensity); }
    
    /**
     * Get light temperature (in Kelvin)
     */
    float GetTemperature() const { return temperature_; }
    
    /**
     * Set light temperature (in Kelvin)
     */
    void SetTemperature(float kelvin) { 
        temperature_ = glm::clamp(kelvin, 1000.0f, 10000.0f); 
        UpdateColorFromTemperature();
    }
    
    // ========== Light-Specific Properties ==========
    
    /**
     * Get light range (for point and spot lights)
     */
    float GetRange() const { return range_; }
    
    /**
     * Set light range (for point and spot lights)
     */
    void SetRange(float range) { range_ = std::max(0.1f, range); }
    
    /**
     * Get spot light angles (inner, outer in degrees)
     */
    glm::vec2 GetSpotAngles() const { return spot_angles_; }
    
    /**
     * Set spot light angles (inner, outer in degrees)
     */
    void SetSpotAngles(float inner_angle, float outer_angle) {
        spot_angles_.x = glm::clamp(inner_angle, 0.0f, 90.0f);
        spot_angles_.y = glm::clamp(outer_angle, spot_angles_.x, 90.0f);
    }
    
    /**
     * Get spot light falloff exponent
     */
    float GetSpotFalloff() const { return spot_falloff_; }
    
    /**
     * Set spot light falloff exponent
     */
    void SetSpotFalloff(float falloff) { spot_falloff_ = std::max(0.1f, falloff); }
    
    /**
     * Get light attenuation (linear, quadratic)
     */
    glm::vec2 GetAttenuation() const { return attenuation_; }
    
    /**
     * Set light attenuation (linear, quadratic)
     */
    void SetAttenuation(float linear, float quadratic) {
        attenuation_.x = std::max(0.0f, linear);
        attenuation_.y = std::max(0.0f, quadratic);
    }
    
    // ========== Shadow Properties ==========
    
    /**
     * Does light cast shadows
     */
    bool GetCastShadow() const { return cast_shadow_; }
    
    /**
     * Enable/disable shadow casting
     */
    void SetCastShadow(bool cast_shadow) { cast_shadow_ = cast_shadow; }
    
    /**
     * Get shadow quality
     */
    ShadowQuality GetShadowQuality() const { return shadow_quality_; }
    
    /**
     * Set shadow quality
     */
    void SetShadowQuality(ShadowQuality quality) { shadow_quality_ = quality; }
    
    /**
     * Get shadow bias (bias, normal_bias)
     */
    glm::vec2 GetShadowBias() const { return shadow_bias_; }
    
    /**
     * Set shadow bias (bias, normal_bias)
     */
    void SetShadowBias(float bias, float normal_bias = 0.0f) {
        shadow_bias_.x = std::max(0.0f, bias);
        shadow_bias_.y = normal_bias;
    }
    
    /**
     * Get shadow filter radius
     */
    float GetShadowFilterRadius() const { return shadow_filter_radius_; }
    
    /**
     * Set shadow filter radius
     */
    void SetShadowFilterRadius(float radius) { shadow_filter_radius_ = std::max(0.5f, radius); }
    
    /**
     * Get shadow near/far planes
     */
    glm::vec2 GetShadowPlanes() const { return shadow_planes_; }
    
    /**
     * Set shadow near/far planes
     */
    void SetShadowPlanes(float near_plane, float far_plane) {
        shadow_planes_.x = std::max(0.01f, near_plane);
        shadow_planes_.y = std::max(shadow_planes_.x + 1.0f, far_plane);
    }
    
    /**
     * Get cascade count (for directional lights)
     */
    uint32_t GetCascadeCount() const { return cascade_count_; }
    
    /**
     * Set cascade count (for directional lights)
     */
    void SetCascadeCount(uint32_t count) { cascade_count_ = glm::clamp(count, 1u, 4u); }
    
    // ========== Advanced Features ==========
    
    /**
     * Get volumetric light intensity
     */
    float GetVolumetricIntensity() const { return volumetric_intensity_; }
    
    /**
     * Set volumetric light intensity
     */
    void SetVolumetricIntensity(float intensity) {
        volumetric_intensity_ = glm::clamp(intensity, 0.0f, 1.0f);
    }

    // ========== Area Light Properties ==========

    /// Rectangle size (width, height) in world units, for LightType::Area.
    glm::vec2 GetAreaSize() const { return area_size_; }
    void      SetAreaSize(glm::vec2 s) {
        area_size_ = glm::max(s, glm::vec2(0.01f));
    }

    /// Whether the area light emits from both faces (else only its +normal side).
    bool IsTwoSided() const { return two_sided_; }
    void SetTwoSided(bool b) { two_sided_ = b; }

    // ========== Cookie / Gobo (spot lights) ==========

    /// Texture projected through a spot light (a gobo mask). Empty = no cookie.
    const std::string& GetCookiePath() const { return cookie_path_; }
    void               SetCookiePath(const std::string& p) { cookie_path_ = p; }

    // ========== Transform Accessors ==========
    
    /**
     * Get light world position
     */
    glm::vec3 GetPosition() const;
    
    /**
     * Get light forward direction
     */
    glm::vec3 GetDirection() const;
    
    /**
     * Get light world rotation
     */
    glm::quat GetRotation() const;

private:
    // Light type and basic properties
    LightType light_type_ = LightType::Point;
    bool enabled_ = true;
    std::string name_;
    
    // Color and intensity
    glm::vec3 color_ = glm::vec3(1.0f);
    float intensity_ = 1.0f;
    float temperature_ = 6500.0f;  // Default: daylight
    
    // Light-specific properties
    float range_ = 10.0f;                    // For point and spot lights
    glm::vec2 spot_angles_ = glm::vec2(15.0f, 30.0f);  // Inner, outer in degrees
    float spot_falloff_ = 2.0f;              // Exponent for smooth falloff
    glm::vec2 attenuation_ = glm::vec2(0.09f, 0.032f);  // Linear, quadratic
    
    // Shadow properties
    bool cast_shadow_ = true;
    ShadowQuality shadow_quality_ = ShadowQuality::Medium;
    glm::vec2 shadow_bias_ = glm::vec2(0.005f, 0.0f);
    float shadow_filter_radius_ = 1.0f;
    glm::vec2 shadow_planes_ = glm::vec2(0.1f, 100.0f);
    uint32_t cascade_count_ = 4;  // For directional lights
    
    // Advanced
    float volumetric_intensity_ = 0.0f;

    // Area light (LightType::Area)
    glm::vec2 area_size_ = glm::vec2(2.0f, 2.0f);  // width, height (world units)
    bool      two_sided_ = false;

    // Spot cookie / gobo (texture projected through the cone). Empty = none.
    std::string cookie_path_;
    
    /**
     * Update color from temperature using Tanner Helland algorithm
     */
    void UpdateColorFromTemperature();
};

// ============================================================================
// Directional Light Component (Legacy)
// ============================================================================

/**
 * @class DirectionalLightComponent
 * @brief Component for directional light
 * @deprecated Use LightComponent with LightType::Directional instead
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
     * @brief Get shadow bias
     */
    float GetShadowBias() const { return shadow_bias_; }
    
    /**
     * @brief Set shadow bias
     */
    void SetShadowBias(float bias) { shadow_bias_ = bias; }
    
protected:
    glm::vec3 color_ = glm::vec3(1.0f);
    float intensity_ = 1.0f;
    bool cast_shadow_ = true;
    float shadow_bias_ = 0.005f;
};

// ============================================================================
// Global/Ambient Light Component
// ============================================================================

/**
 * @class GlobalLightComponent
 * @brief Component for global ambient light
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
    glm::vec3 color_ = glm::vec3(0.3f);
    float intensity_ = 1.0f;
};

} // namespace schizo::scene
