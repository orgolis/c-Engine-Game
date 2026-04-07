#pragma once

#include <memory>
#include <array>
#include <glm/glm.hpp>
#include "texture.h"

namespace schizo::renderer {

// Forward declarations
class RenderDevice;
class Framebuffer;
class Light;

/**
 * @enum ShadowAlgorithm
 * @brief Shadow mapping algorithm
 */
enum class ShadowAlgorithm : uint8_t {
    BasicShadowMap,        // Simple depth comparison
    PCF,                   // Percentage Closer Filtering (2x2, 4x4, 8x8)
    PCSS,                  // Percentage Closer Soft Shadows
    VSM,                   // Variance Shadow Maps
    EVSM,                  // Exponential Variance Shadow Maps
};

/**
 * @struct ShadowMapConfig
 * @brief Configuration for shadow map generation
 */
struct ShadowMapConfig {
    uint32_t resolution = 2048;          // Shadow map resolution
    ShadowAlgorithm algorithm = ShadowAlgorithm::PCF;
    float bias = 0.005f;                 // Depth bias to reduce shadow acne
    float normal_bias = 0.01f;           // Normal offset bias
    float filter_radius = 1.0f;          // For PCF/PCSS
    float light_bleed_reduction = 0.2f;  // For VSM
    bool use_linear_depth = true;        // For better precision
    bool dynamic_cascade = true;         // Auto-adjust cascade levels
};

/**
 * @struct CascadeConfig
 * @brief Cascade shadow map configuration
 */
struct CascadeConfig {
    uint32_t num_cascades = 4;           // Number of cascade levels
    std::array<float, 4> cascade_splits = {0.1f, 0.3f, 0.7f, 1.0f};
    float overlap = 0.1f;                // Overlap between cascades
    bool use_smooth_transitions = true;  // Smooth blending between cascades
};

/**
 * @class ShadowMap
 * @brief Manages shadow map generation and rendering
 */
class ShadowMap {
public:
    /**
     * Create a shadow map with configuration
     */
    static std::unique_ptr<ShadowMap> Create(RenderDevice* device, 
                                            const ShadowMapConfig& config);
    
    virtual ~ShadowMap() = default;
    
    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;
    ShadowMap(ShadowMap&&) = default;
    ShadowMap& operator=(ShadowMap&&) = default;
    
    /**
     * Initialize shadow map framebuffer
     */
    virtual bool Initialize() = 0;
    
    /**
     * Begin rendering to shadow map
     */
    virtual void BeginRender(RenderDevice* device) = 0;
    
    /**
     * End rendering to shadow map (resolve/filter if needed)
     */
    virtual void EndRender(RenderDevice* device) = 0;
    
    /**
     * Get the shadow map texture
     */
    virtual std::shared_ptr<Texture2D> GetShadowTexture() const = 0;
    
    /**
     * Get the framebuffer for rendering
     */
    virtual std::shared_ptr<Framebuffer> GetFramebuffer() const = 0;
    
    /**
     * Update configuration
     */
    virtual void SetConfig(const ShadowMapConfig& config) = 0;
    virtual ShadowMapConfig GetConfig() const = 0;
    
    /**
     * Get resolution
     */
    virtual uint32_t GetResolution() const = 0;

protected:
    ShadowMap() = default;
};

/**
 * @class CascadeShadowMap
 * @brief Manages cascaded shadow mapping for directional lights
 */
class CascadeShadowMap {
public:
    /**
     * Create cascaded shadow map system
     */
    static std::unique_ptr<CascadeShadowMap> Create(RenderDevice* device,
                                                    const ShadowMapConfig& config,
                                                    const CascadeConfig& cascade_config);
    
    virtual ~CascadeShadowMap() = default;
    
    CascadeShadowMap(const CascadeShadowMap&) = delete;
    CascadeShadowMap& operator=(const CascadeShadowMap&) = delete;
    CascadeShadowMap(CascadeShadowMap&&) = default;
    CascadeShadowMap& operator=(CascadeShadowMap&&) = default;
    
    /**
     * Initialize cascades
     */
    virtual bool Initialize() = 0;
    
    /**
     * Begin rendering cascade
     */
    virtual void BeginCascade(RenderDevice* device, uint32_t cascade_level) = 0;
    
    /**
     * End cascade rendering
     */
    virtual void EndCascade(RenderDevice* device) = 0;
    
    /**
     * Get shadow texture for cascade
     */
    virtual std::shared_ptr<Texture2D> GetCascadeTexture(uint32_t cascade_level) const = 0;
    
    /**
     * Get all cascade textures as array texture
     */
    virtual std::shared_ptr<Texture2D> GetCascadeArray() const = 0;
    
    /**
     * Get view-projection matrices for all cascades
     */
    virtual std::vector<glm::mat4> GetCascadeMatrices() const = 0;
    
    /**
     * Get cascade split distances
     */
    virtual std::vector<float> GetCascadeSplits() const = 0;
    
    /**
     * Update cascade configuration
     */
    virtual void SetCascadeConfig(const CascadeConfig& config) = 0;
    virtual CascadeConfig GetCascadeConfig() const = 0;
    
    /**
     * Update camera and recalculate cascades
     */
    virtual void UpdateCascades(const glm::vec3& camera_pos,
                               const glm::vec3& camera_dir,
                               float camera_fov,
                               float near_plane,
                               float far_plane) = 0;

protected:
    CascadeShadowMap() = default;
};

/**
 * @class ShadowMapAtlas
 * @brief Manages multiple shadow maps in a single atlas texture
 */
class ShadowMapAtlas {
public:
    /**
     * Create shadow map atlas
     */
    static std::unique_ptr<ShadowMapAtlas> Create(RenderDevice* device,
                                                 uint32_t atlas_size,
                                                 uint32_t max_lights);
    
    virtual ~ShadowMapAtlas() = default;
    
    ShadowMapAtlas(const ShadowMapAtlas&) = delete;
    ShadowMapAtlas& operator=(const ShadowMapAtlas&) = delete;
    ShadowMapAtlas(ShadowMapAtlas&&) = default;
    ShadowMapAtlas& operator=(ShadowMapAtlas&&) = default;
    
    /**
     * Allocate space for a light's shadow map
     */
    virtual uint32_t AllocateLight(uint32_t resolution) = 0;
    
    /**
     * Release space for a light
     */
    virtual void ReleaseLight(uint32_t light_id) = 0;
    
    /**
     * Begin rendering shadow map for light
     */
    virtual void BeginLight(RenderDevice* device, uint32_t light_id) = 0;
    
    /**
     * End rendering light shadow map
     */
    virtual void EndLight(RenderDevice* device) = 0;
    
    /**
     * Get atlas texture
     */
    virtual std::shared_ptr<Texture2D> GetAtlasTexture() const = 0;
    
    /**
     * Get UV rect for light in atlas
     */
    virtual glm::vec4 GetLightRect(uint32_t light_id) const = 0;

protected:
    ShadowMapAtlas() = default;
};

} // namespace schizo::renderer

#endif // SHADOW_H
