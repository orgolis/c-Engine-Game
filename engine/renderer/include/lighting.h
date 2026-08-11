#pragma once

#include <memory>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <cstdint>
#include "light.h"

namespace schizo::renderer {

// Forward declarations
class RenderDevice;
class EnvironmentMap;

/**
 * @struct LightingConfig
 * @brief Configuration for lighting system
 */
struct LightingConfig {
    uint32_t max_lights = 128;                      // Maximum active lights
    uint32_t max_shadow_casting_lights = 8;         // Max lights with shadows per frame
    float global_ambient = 0.1f;                    // Global ambient light
    glm::vec3 ambient_color = glm::vec3(0.5f);     // Ambient light color
    bool enable_ibl = true;                         // Enable image-based lighting
    bool enable_dynamic_shadows = true;             // Enable shadow updates
    bool use_deferred_shading = false;              // Use deferred shading instead of forward
};

/**
 * @class LightManager
 * @brief Manages all lights in a scene
 */
class LightManager {
public:
    /**
     * Create a light manager
     */
    static std::unique_ptr<LightManager> Create(RenderDevice* device,
                                               const LightingConfig& config);
    
    virtual ~LightManager() = default;
    
    LightManager(const LightManager&) = delete;
    LightManager& operator=(const LightManager&) = delete;
    LightManager(LightManager&&) = default;
    LightManager& operator=(LightManager&&) = default;
    
    /**
     * Create and add a light to the manager
     */
    virtual std::shared_ptr<Light> AddLight(LightType type, const std::string& name = "Light") = 0;
    
    /**
     * Remove a light from the manager
     */
    virtual void RemoveLight(std::shared_ptr<Light> light) = 0;
    
    /**
     * Get light by name
     */
    virtual std::shared_ptr<Light> GetLight(const std::string& name) const = 0;
    
    /**
     * Get all lights
     */
    virtual std::vector<std::shared_ptr<Light>> GetLights() const = 0;
    
    /**
     * Get lights by type
     */
    virtual std::vector<std::shared_ptr<Light>> GetLightsByType(LightType type) const = 0;
    
    /**
     * Get all enabled lights
     */
    virtual std::vector<std::shared_ptr<Light>> GetActiveLights() const = 0;
    
    /**
     * Get shadow-casting lights
     */
    virtual std::vector<std::shared_ptr<Light>> GetShadowCastingLights() const = 0;
    
    /**
     * Get total light count
     */
    virtual uint32_t GetLightCount() const = 0;
    
    /**
     * Set main/primary directional light (sun)
     */
    virtual void SetMainLight(std::shared_ptr<Light> light) = 0;
    virtual std::shared_ptr<Light> GetMainLight() const = 0;
    
    // Ambient and global lighting
    /**
     * Set global ambient light
     */
    virtual void SetAmbientLight(float intensity) = 0;
    virtual float GetAmbientLight() const = 0;
    
    /**
     * Set ambient light color
     */
    virtual void SetAmbientColor(const glm::vec3& color) = 0;
    virtual glm::vec3 GetAmbientColor() const = 0;
    
    /**
     * Set environment map for IBL
     */
    virtual void SetEnvironmentMap(std::shared_ptr<EnvironmentMap> env_map) = 0;
    virtual std::shared_ptr<EnvironmentMap> GetEnvironmentMap() const = 0;
    
    // Configuration
    /**
     * Update lighting configuration
     */
    virtual void SetConfig(const LightingConfig& config) = 0;
    virtual LightingConfig GetConfig() const = 0;
    
    /**
     * Enable/disable shadow updates
     */
    virtual void SetDynamicShadows(bool enabled) = 0;
    virtual bool GetDynamicShadows() const = 0;
    
    // Lighting calculations
    /**
     * Calculate lighting for a point in space from all lights
     * Useful for point light baking and other calculations
     */
    virtual glm::vec3 CalculateLighting(const glm::vec3& position,
                                        const glm::vec3& normal,
                                        const glm::vec3& view_dir) const = 0;
    
    /**
     * Calculate contribution of a single light
     */
    virtual glm::vec3 CalculateLightContribution(std::shared_ptr<Light> light,
                                                 const glm::vec3& position,
                                                 const glm::vec3& normal,
                                                 const glm::vec3& view_dir) const = 0;
    
    /**
     * Get nearest lights that affect a position
     */
    virtual std::vector<std::shared_ptr<Light>> GetNearestLights(const glm::vec3& position,
                                                                 uint32_t max_lights = 4) const = 0;

protected:
    LightManager() = default;
};

/**
 * @class LightingUtils
 * @brief Utility functions for lighting calculations
 */
class LightingUtils {
public:
    /**
     * Calculate Lambertian diffuse
     */
    static glm::vec3 Lambertian(const glm::vec3& light_color,
                               const glm::vec3& normal,
                               const glm::vec3& light_dir,
                               float intensity = 1.0f);
    
    /**
     * Calculate Blinn-Phong specular
     */
    static glm::vec3 BlinnPhong(const glm::vec3& light_color,
                               const glm::vec3& normal,
                               const glm::vec3& light_dir,
                               const glm::vec3& view_dir,
                               float shininess = 32.0f,
                               float intensity = 1.0f);
    
    /**
     * Calculate point light attenuation
     */
    static float PointLightAttenuation(float distance,
                                      float range,
                                      float linear = 0.045f,
                                      float quadratic = 0.0075f);
    
    /**
     * Calculate spot light falloff
     */
    static float SpotLightFalloff(float cos_theta,
                                 float cos_inner,
                                 float cos_outer,
                                 float falloff = 2.0f);
    
    /**
     * Calculate light cone falloff with smooth transition
     */
    static float SmoothCone(float cos_theta,
                           float cos_inner,
                           float cos_outer);
    
    /**
     * Calculate distance attenuation with inverse square law
     */
    static float InverseSquareLaw(float distance,
                                 float falloff_start = 1.0f);
    
    /**
     * Convert point light range to intensity
     */
    static float RangeToRadius(float intensity,
                              float min_brightness = 1.0f / 255.0f);
    
    /**
     * Calculate shadow bias for a light
     */
    static glm::vec2 CalculateShadowBias(LightType light_type,
                                        float normal_offset = 0.01f);
    
    /**
     * Calculate light space matrix for shadow mapping
     */
    static glm::mat4 CalculateLightSpaceMatrix(const glm::vec3& light_pos,
                                              const glm::vec3& light_dir,
                                              float view_distance = 100.0f);
    
    /**
     * Culling: Check if light affects an AABB
     */
    static bool LightAffectsAABB(std::shared_ptr<Light> light,
                                const glm::vec3& aabb_min,
                                const glm::vec3& aabb_max);
    
    /**
     * Culling: Check if light affects a sphere
     */
    static bool LightAffectsSphere(std::shared_ptr<Light> light,
                                  const glm::vec3& sphere_center,
                                  float sphere_radius);

protected:
    LightingUtils() = default;
};

} // namespace schizo::renderer

