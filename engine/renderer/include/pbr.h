#pragma once

#include <memory>
#include <string>
#include <glm/glm.hpp>
#include "material.h"
#include "texture.h"

namespace schizo::renderer {

// Forward declarations
class RenderDevice;

/**
 * @class PBRPresets
 * @brief Factory for common PBR material presets
 */
class PBRPresets {
public:
    /**
     * Create a gold material
     */
    static std::unique_ptr<PBRMaterial> Gold(RenderDevice* device);
    
    /**
     * Create a copper material
     */
    static std::unique_ptr<PBRMaterial> Copper(RenderDevice* device);
    
    /**
     * Create a steel material
     */
    static std::unique_ptr<PBRMaterial> Steel(RenderDevice* device);
    
    /**
     * Create an aluminum material
     */
    static std::unique_ptr<PBRMaterial> Aluminum(RenderDevice* device);
    
    /**
     * Create a brass material
     */
    static std::unique_ptr<PBRMaterial> Brass(RenderDevice* device);
    
    /**
     * Create a plastic material (rough dielectric)
     */
    static std::unique_ptr<PBRMaterial> Plastic(RenderDevice* device, const glm::vec3& color);
    
    /**
     * Create a matte fabric material
     */
    static std::unique_ptr<PBRMaterial> Fabric(RenderDevice* device, const glm::vec3& color);
    
    /**
     * Create a ceramic/porcelain material
     */
    static std::unique_ptr<PBRMaterial> Ceramic(RenderDevice* device, const glm::vec3& color);
    
    /**
     * Create rubber material
     */
    static std::unique_ptr<PBRMaterial> Rubber(RenderDevice* device, const glm::vec3& color);
    
    /**
     * Create glass material (transparent dielectric)
     */
    static std::unique_ptr<PBRMaterial> Glass(RenderDevice* device);
    
    /**
     * Create skin material (with subsurface scattering)
     */
    static std::unique_ptr<PBRMaterial> Skin(RenderDevice* device, const glm::vec3& tone);
    
    /**
     * Create wood material
     */
    static std::unique_ptr<PBRMaterial> Wood(RenderDevice* device, const glm::vec3& color);
    
    /**
     * Create a Lambertian diffuse surface
     */
    static std::unique_ptr<PBRMaterial> DiffuseLambert(RenderDevice* device, const glm::vec3& color);
    
    /**
     * Create a mirror/polished metal material
     */
    static std::unique_ptr<PBRMaterial> Mirror(RenderDevice* device);
};

/**
 * @struct PBRTextureSet
 * @brief Set of textures for a PBR material
 */
struct PBRTextureSet {
    std::shared_ptr<Texture2D> albedo;           // Base color
    std::shared_ptr<Texture2D> normal;           // Surface normals (tangent space)
    std::shared_ptr<Texture2D> metallic;         // Metallic intensity (or occlusion channel)
    std::shared_ptr<Texture2D> roughness;        // Surface roughness
    std::shared_ptr<Texture2D> ao;               // Ambient occlusion
    std::shared_ptr<Texture2D> emissive;         // Self-emitted light
    std::shared_ptr<Texture2D> height;           // Displacement map
    std::shared_ptr<Texture2D> cavity;           // Cavity/crevice occlusion
};

/**
 * @class MaterialLibrary
 * @brief Material management and caching system
 */
class MaterialLibrary {
public:
    /**
     * Create a new material library
     */
    static std::unique_ptr<MaterialLibrary> Create(RenderDevice* device);
    
    virtual ~MaterialLibrary() = default;
    
    MaterialLibrary(const MaterialLibrary&) = delete;
    MaterialLibrary& operator=(const MaterialLibrary&) = delete;
    MaterialLibrary(MaterialLibrary&&) = default;
    MaterialLibrary& operator=(MaterialLibrary&&) = default;
    
    /**
     * Register a material by name
     */
    virtual void RegisterMaterial(const std::string& name, std::unique_ptr<Material> material) = 0;
    
    /**
     * Get a material by name
     */
    virtual std::shared_ptr<Material> GetMaterial(const std::string& name) = 0;
    
    /**
     * Get a PBR material by name
     */
    virtual std::shared_ptr<PBRMaterial> GetPBRMaterial(const std::string& name) = 0;
    
    /**
     * Check if material exists
     */
    virtual bool HasMaterial(const std::string& name) const = 0;
    
    /**
     * Remove a material from the library
     */
    virtual void RemoveMaterial(const std::string& name) = 0;
    
    /**
     * Create and register a PBR material from texture set
     */
    virtual std::shared_ptr<PBRMaterial> CreatePBRFromTextures(const std::string& name, 
                                                               const PBRTextureSet& textures) = 0;
    
    /**
     * Create and register a parametric PBR material
     */
    virtual std::shared_ptr<PBRMaterial> CreateParametricPBR(const std::string& name,
                                                             const glm::vec3& albedo,
                                                             float metallic,
                                                             float roughness) = 0;
    
    /**
     * Get all material names
     */
    virtual std::vector<std::string> GetMaterialNames() const = 0;
    
    /**
     * Clear all materials
     */
    virtual void Clear() = 0;

protected:
    MaterialLibrary() = default;
};

/**
 * @class PBRMaterialBuilder
 * @brief Fluent builder for creating PBR materials
 */
class PBRMaterialBuilder {
public:
    /**
     * Create a new builder
     */
    explicit PBRMaterialBuilder(RenderDevice* device);
    
    /**
     * Set material name
     */
    PBRMaterialBuilder& SetName(const std::string& name);
    
    /**
     * Set base color/albedo
     */
    PBRMaterialBuilder& SetAlbedo(const glm::vec3& color);
    PBRMaterialBuilder& SetAlbedoTexture(std::shared_ptr<Texture2D> texture);
    
    /**
     * Set metallic value and texture
     */
    PBRMaterialBuilder& SetMetallic(float value);
    PBRMaterialBuilder& SetMetallicTexture(std::shared_ptr<Texture2D> texture);
    
    /**
     * Set roughness value and texture
     */
    PBRMaterialBuilder& SetRoughness(float value);
    PBRMaterialBuilder& SetRoughnessTexture(std::shared_ptr<Texture2D> texture);
    
    /**
     * Set normal map
     */
    PBRMaterialBuilder& SetNormalMap(std::shared_ptr<Texture2D> texture, float strength = 1.0f);
    
    /**
     * Set ambient occlusion
     */
    PBRMaterialBuilder& SetAmbientOcclusion(std::shared_ptr<Texture2D> texture, float strength = 1.0f);
    
    /**
     * Set emissive color and intensity
     */
    PBRMaterialBuilder& SetEmissive(const glm::vec3& color, float intensity = 1.0f);
    PBRMaterialBuilder& SetEmissiveTexture(std::shared_ptr<Texture2D> texture);
    
    /**
     * Set height/displacement map
     */
    PBRMaterialBuilder& SetHeightMap(std::shared_ptr<Texture2D> texture);
    
    /**
     * Set cavity map
     */
    PBRMaterialBuilder& SetCavityMap(std::shared_ptr<Texture2D> texture);
    
    /**
     * Enable/disable metallic-roughness workflow
     */
    PBRMaterialBuilder& SetWorkflow(bool metallic_roughness);
    
    /**
     * Build the final material
     */
    std::unique_ptr<PBRMaterial> Build();

private:
    RenderDevice* device_;
    std::string name_;
    glm::vec3 albedo_ = glm::vec3(0.9f);
    float metallic_ = 0.0f;
    float roughness_ = 0.5f;
    float ao_strength_ = 1.0f;
    float normal_strength_ = 1.0f;
    glm::vec3 emissive_ = glm::vec3(0.0f);
    float emissive_intensity_ = 0.0f;
    bool metallic_roughness_workflow_ = true;
    
    std::shared_ptr<Texture2D> albedo_texture_;
    std::shared_ptr<Texture2D> metallic_texture_;
    std::shared_ptr<Texture2D> roughness_texture_;
    std::shared_ptr<Texture2D> normal_texture_;
    std::shared_ptr<Texture2D> ao_texture_;
    std::shared_ptr<Texture2D> emissive_texture_;
    std::shared_ptr<Texture2D> height_texture_;
    std::shared_ptr<Texture2D> cavity_texture_;
};

/**
 * @class PBRMaterialUtils
 * @brief Utility functions for PBR material operations
 */
class PBRMaterialUtils {
public:
    /**
     * Convert linear color to sRGB
     */
    static glm::vec3 LinearToSRGB(const glm::vec3& linear);
    
    /**
     * Convert sRGB color to linear
     */
    static glm::vec3 SRGBToLinear(const glm::vec3& srgb);
    
    /**
     * Calculate Fresnel (F0) from refractive index
     */
    static glm::vec3 CalculateF0(float refractive_index);
    
    /**
     * Clamp roughness to valid range (prevents division by zero)
     */
    static float ClampRoughness(float roughness);
    
    /**
     * Estimate roughness from specular value
     */
    static float SpecularToRoughness(float specular);
    
    /**
     * Convert dielectric specular to metallic/roughness values
     */
    static void ConvertSpecularGloss(float specular, float gloss,
                                     glm::vec3& out_albedo,
                                     float& out_metallic,
                                     float& out_roughness);
    
    /**
     * Check if material is metallic
     */
    static bool IsMetallic(float metallic_value);
    
    /**
     * Get real-world material parameters
     */
    static void GetMaterialParams(const std::string& material_name,
                                  glm::vec3& out_albedo,
                                  float& out_metallic,
                                  float& out_roughness);
};

} // namespace schizo::renderer

#endif // PBR_H
