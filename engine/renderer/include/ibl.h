#pragma once

#include <memory>
#include <string>
#include <glm/glm.hpp>
#include "texture.h"

namespace schizo::renderer {

// Forward declarations
class RenderDevice;

/**
 * @enum IBLQuality
 * @brief Quality of IBL specular convolution
 */
enum class IBLQuality : uint8_t {
    Low,      // 16x samples per roughness level
    Medium,   // 32x samples per roughness level
    High,     // 64x samples per roughness level
    Ultra,    // 128x samples per roughness level
};

/**
 * @class EnvironmentMap
 * @brief Manages HDR environment maps for IBL
 */
class EnvironmentMap {
public:
    /**
     * Load environment map from equirectangular HDR image
     */
    static std::unique_ptr<EnvironmentMap> LoadEquirectangular(RenderDevice* device,
                                                               const std::string& filepath,
                                                               IBLQuality quality = IBLQuality::Medium);
    
    /**
     * Load environment map from cubemap
     */
    static std::unique_ptr<EnvironmentMap> LoadCubemap(RenderDevice* device,
                                                       const std::string& positive_x,
                                                       const std::string& negative_x,
                                                       const std::string& positive_y,
                                                       const std::string& negative_y,
                                                       const std::string& positive_z,
                                                       const std::string& negative_z,
                                                       IBLQuality quality = IBLQuality::Medium);
    
    /**
     * Create a simple gradient environment map
     */
    static std::unique_ptr<EnvironmentMap> CreateGradient(RenderDevice* device,
                                                         const glm::vec3& top_color,
                                                         const glm::vec3& bottom_color,
                                                         IBLQuality quality = IBLQuality::Medium);
    
    virtual ~EnvironmentMap() = default;
    
    EnvironmentMap(const EnvironmentMap&) = delete;
    EnvironmentMap& operator=(const EnvironmentMap&) = delete;
    EnvironmentMap(EnvironmentMap&&) = default;
    EnvironmentMap& operator=(EnvironmentMap&&) = default;
    
    /**
     * Get the environment cubemap
     */
    virtual std::shared_ptr<TextureCube> GetEnvironmentCubemap() const = 0;
    
    /**
     * Get the irradiance map (diffuse component)
     * Pre-integrated diffuse lighting for quick lookup
     */
    virtual std::shared_ptr<TextureCube> GetIrradianceMap() const = 0;
    
    /**
     * Get the specular prefiltered map (specular component)
     * Specular lighting pre-convolved at different roughness levels
     */
    virtual std::shared_ptr<TextureCube> GetSpecularMap() const = 0;
    
    /**
     * Get the BRDF lookup texture
     * Pre-integrated Fresnel and roughness response
     */
    virtual std::shared_ptr<Texture2D> GetBRDFLUT() const = 0;
    
    /**
     * Get ambient light from environment
     */
    virtual glm::vec3 GetAmbientlight() const = 0;
    
    /**
     * Set intensity multiplier for IBL
     */
    virtual void SetIntensity(float intensity) = 0;
    virtual float GetIntensity() const = 0;
    
    /**
     * Set rotation for environment map
     */
    virtual void SetRotation(float yaw) = 0;
    virtual float GetRotation() const = 0;

protected:
    EnvironmentMap() = default;
};

/**
 * @class LightProbe
 * @brief Captures incoming lighting at a point in space
 */
class LightProbe {
public:
    /**
     * Create a light probe at position
     */
    static std::unique_ptr<LightProbe> Create(RenderDevice* device,
                                             const glm::vec3& position,
                                             uint32_t resolution = 32);
    
    virtual ~LightProbe() = default;
    
    LightProbe(const LightProbe&) = delete;
    LightProbe& operator=(const LightProbe&) = delete;
    LightProbe(LightProbe&&) = default;
    LightProbe& operator=(LightProbe&&) = default;
    
    /**
     * Capture lighting at current position
     */
    virtual void Capture(RenderDevice* device) = 0;
    
    /**
     * Get captured cubemap
     */
    virtual std::shared_ptr<TextureCube> GetCubemap() const = 0;
    
    /**
     * Get SH coefficients (spherical harmonics)
     * 9 coefficients for 2nd order SH (9x3 = 27 floats)
     */
    virtual std::vector<glm::vec3> GetSHCoefficients() const = 0;
    
    /**
     * Set position
     */
    virtual void SetPosition(const glm::vec3& position) = 0;
    virtual glm::vec3 GetPosition() const = 0;
    
    /**
     * Enable/disable
     */
    virtual void SetEnabled(bool enabled) = 0;
    virtual bool IsEnabled() const = 0;
    
    /**
     * Set influence radius
     */
    virtual void SetInfluenceRadius(float radius) = 0;
    virtual float GetInfluenceRadius() const = 0;

protected:
    LightProbe() = default;
};

/**
 * @class SkyBox
 * @brief Renders a skybox from an environment map
 */
class SkyBox {
public:
    /**
     * Create a skybox from environment or cubemap
     */
    static std::unique_ptr<SkyBox> Create(RenderDevice* device,
                                         std::shared_ptr<TextureCube> cubemap);
    
    virtual ~SkyBox() = default;
    
    SkyBox(const SkyBox&) = delete;
    SkyBox& operator=(const SkyBox&) = delete;
    SkyBox(SkyBox&&) = default;
    SkyBox& operator=(SkyBox&&) = default;
    
    /**
     * Render the skybox
     */
    virtual void Render(RenderDevice* device) = 0;
    
    /**
     * Set cubemap
     */
    virtual void SetCubemap(std::shared_ptr<TextureCube> cubemap) = 0;
    virtual std::shared_ptr<TextureCube> GetCubemap() const = 0;
    
    /**
     * Set exposure/intensity
     */
    virtual void SetIntensity(float intensity) = 0;
    virtual float GetIntensity() const = 0;
    
    /**
     * Set rotation
     */
    virtual void SetRotation(float yaw) = 0;
    virtual float GetRotation() const = 0;

protected:
    SkyBox() = default;
};

/**
 * @class IBLUtils
 * @brief Utilities for IBL calculations
 */
class IBLUtils {
public:
    /**
     * Calculate spherical harmonics coefficients from cubemap
     * Projects cubemap to 2nd order SH (9 coefficients)
     */
    static std::vector<glm::vec3> ProjectToSH(RenderDevice* device,
                                             std::shared_ptr<TextureCube> cubemap);
    
    /**
     * Evaluate SH at direction
     */
    static glm::vec3 EvaluateSH(const std::vector<glm::vec3>& coeffs,
                               const glm::vec3& direction);
    
    /**
     * Calculate dominant light direction and color from SH
     */
    static std::pair<glm::vec3, glm::vec3> GetDominantLight(const std::vector<glm::vec3>& coeffs);
    
    /**
     * Convolve environment for diffuse (irradiance map)
     * Integrates incoming radiance over hemisphere
     */
    static std::shared_ptr<TextureCube> ConvolveDiffuse(RenderDevice* device,
                                                        std::shared_ptr<TextureCube> environment);
    
    /**
     * Prefilter environment for specular
     * Pre-convolves with cosine-weighted specular lobe at different roughness levels
     */
    static std::shared_ptr<TextureCube> PrefilterSpecular(RenderDevice* device,
                                                         std::shared_ptr<TextureCube> environment,
                                                         IBLQuality quality = IBLQuality::Medium);
    
    /**
     * Generate BRDF 2D lookup texture
     * Stores Fresnel and roughness response for split-sum approximation
     */
    static std::shared_ptr<Texture2D> GenerateBRDFLUT(RenderDevice* device,
                                                      uint32_t resolution = 512);

protected:
    IBLUtils() = default;
};

} // namespace schizo::renderer

