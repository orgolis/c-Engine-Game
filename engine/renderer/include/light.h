#pragma once

#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include "texture.h"

namespace schizo::renderer {

// Forward declarations
class RenderDevice;
class Framebuffer;

/**
 * @enum LightType
 * @brief Types of light sources
 */
enum class LightType : uint8_t {
    Directional,  // Sun light, infinite distance
    Point,        // Omni-directional point light
    Spot,         // Cone-shaped directional light
};

/**
 * @enum ShadowQuality
 * @brief Shadow map quality and technique
 */
enum class ShadowQuality : uint8_t {
    None,          // No shadows
    Low,           // 512x512 shadow map
    Medium,        // 1024x1024 shadow map
    High,          // 2048x2048 shadow map
    Ultra,         // 4096x4096 shadow map
};

/**
 * @class Light
 * @brief Advanced light source with shadow support and physical properties
 */
struct Light {
public:
    /**
     * Create a light of the specified type
     */
    static std::unique_ptr<Light> Create(LightType type, const std::string& name = "Light");
    
    virtual ~Light() = default;
    
    Light(const Light&) = delete;
    Light& operator=(const Light&) = delete;
    Light(Light&&) = default;
    Light& operator=(Light&&) = default;
    
    // Basic properties
    virtual void SetName(const std::string& name) = 0;
    virtual std::string GetName() const = 0;
    
    virtual LightType GetType() const = 0;
    
    virtual void SetEnabled(bool enabled) = 0;
    virtual bool IsEnabled() const = 0;
    
    // Transform properties
    virtual void SetPosition(const glm::vec3& position) = 0;
    virtual glm::vec3 GetPosition() const = 0;
    
    virtual void SetDirection(const glm::vec3& direction) = 0;
    virtual glm::vec3 GetDirection() const = 0;
    
    virtual void SetRotation(const glm::quat& rotation) = 0;
    virtual glm::quat GetRotation() const = 0;
    
    // Color and intensity
    virtual void SetColor(const glm::vec3& color) = 0;
    virtual glm::vec3 GetColor() const = 0;
    
    virtual void SetIntensity(float intensity) = 0;
    virtual float GetIntensity() const = 0;
    
    // Light properties specific to type
    /**
     * Set range for point and spot lights
     */
    virtual void SetRange(float range) = 0;
    virtual float GetRange() const = 0;
    
    /**
     * Set attenuation for point and spot lights
     * Attenuation uses inverse square law by default
     */
    virtual void SetAttenuation(float linear, float quadratic) = 0;
    virtual glm::vec2 GetAttenuation() const = 0;
    
    /**
     * Set inner and outer cone angles for spot lights (in degrees)
     */
    virtual void SetSpotAngles(float inner_angle, float outer_angle) = 0;
    virtual glm::vec2 GetSpotAngles() const = 0;
    
    /**
     * Set falloff exponent for spot light
     */
    virtual void SetSpotFalloff(float falloff) = 0;
    virtual float GetSpotFalloff() const = 0;
    
    // Shadow properties
    /**
     * Enable/disable shadow casting
     */
    virtual void SetCastShadow(bool cast_shadow) = 0;
    virtual bool GetCastShadow() const = 0;
    
    /**
     * Set shadow quality
     */
    virtual void SetShadowQuality(ShadowQuality quality) = 0;
    virtual ShadowQuality GetShadowQuality() const = 0;
    
    /**
     * Set shadow map near/far planes
     */
    virtual void SetShadowPlanes(float near_plane, float far_plane) = 0;
    virtual glm::vec2 GetShadowPlanes() const = 0;
    
    /**
     * Set shadow bias to reduce shadow acne
     */
    virtual void SetShadowBias(float bias, float normal_bias = 0.0f) = 0;
    virtual glm::vec2 GetShadowBias() const = 0;
    
    /**
     * Set shadow map filters
     */
    virtual void SetShadowFilter(float filter_radius) = 0;
    virtual float GetShadowFilter() const = 0;
    
    /**
     * Get the shadow map framebuffer (if shadows enabled)
     */
    virtual std::shared_ptr<Framebuffer> GetShadowFramebuffer() const = 0;
    
    /**
     * Get the shadow map texture (if shadows enabled)
     */
    virtual std::shared_ptr<Texture2D> GetShadowMap() const = 0;
    
    // View/projection matrices for rendering
    /**
     * Get view matrix for shadow map rendering
     */
    virtual glm::mat4 GetViewMatrix() const = 0;
    
    /**
     * Get projection matrix for shadow map rendering
     */
    virtual glm::mat4 GetProjectionMatrix() const = 0;
    
    /**
     * Get view-projection matrix
     */
    virtual glm::mat4 GetViewProjectionMatrix() const = 0;
    
    // Physical properties
    /**
     * Set light temperature (in Kelvin) for realistic color
     * Common values: 6500K = daylight, 3000K = warm indoor lighting
     */
    virtual void SetTemperature(float kelvin) = 0;
    virtual float GetTemperature() const = 0;
    
    /**
     * Calculate color from temperature
     */
    static glm::vec3 TemperatureToColor(float kelvin);
    
    // Advanced features
    /**
     * Set light cookie texture (masks light beam)
     */
    virtual void SetCookieTexture(std::shared_ptr<Texture2D> texture) = 0;
    virtual std::shared_ptr<Texture2D> GetCookieTexture() const = 0;
    
    /**
     * Set volumetric light intensity (god rays)
     */
    virtual void SetVolumetricIntensity(float intensity) = 0;
    virtual float GetVolumetricIntensity() const = 0;
    
    /**
     * For cascade shadow mapping
     */
    virtual void SetCascadeCount(uint32_t count) = 0;
    virtual uint32_t GetCascadeCount() const = 0;

protected:
    Light() = default;
};

} // namespace schizo::renderer

