#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include "shader.h"
#include "texture.h"

namespace schizo::renderer {

// Forward declarations
class RenderDevice;
class Material;

/**
 * @enum MaterialType
 * @brief Categorizes materials by rendering approach
 */
enum class MaterialType : uint8_t {
    Unlit,                  // No lighting, emissive only
    Lit,                    // Simple Blinn-Phong lighting
    StandardPBR,            // Metallic-Roughness PBR
    MetallicRoughness,      // Disney/Unreal style PBR
    SpecularGlossiness,     // Legacy specular-gloss workflow
    Cloth,                  // Cloth material with anisotropy
    Subsurface,             // Translucent materials with SSS
    Anisotropic,            // Brushed metal, hair-like materials
    Custom                  // Custom shader-based material
};

/**
 * @enum TextureSlot
 * @brief Standard PBR material texture slots
 */
enum class TextureSlot : uint8_t {
    Albedo = 0,             // Base color
    Normal = 1,             // Surface normals
    Metallic = 2,           // Metallic intensity
    Roughness = 3,          // Surface roughness
    AmbientOcclusion = 4,   // Ambient occlusion
    Emissive = 5,           // Self-emitted light
    Height = 6,             // Height/displacement map
    CavityMap = 7,          // Cavity/crevice occlusion
    MaxSlots = 8
};

/**
 * @class Material
 * @brief Encapsulates shader program + uniforms + texture bindings
 * 
 * A material represents a combination of:
 * - A compiled shader program
 * - Uniform parameter values (colors, matrices, floats, etc)
 * - Texture bindings to texture units
 * 
 * Materials can be easily reused and modified without recompiling shaders.
 */
class Material {
public:
    /**
     * Create a material from a shader program
     */
    static std::unique_ptr<Material> Create(std::shared_ptr<ShaderProgram> shader);
    
    virtual ~Material() = default;
    
    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;
    Material(Material&&) = default;
    Material& operator=(Material&&) = default;
    
    /**
     * Get the material type
     */
    virtual MaterialType GetType() const = 0;
    
    /**
     * Get the shader program
     */
    virtual std::shared_ptr<ShaderProgram> GetShader() const = 0;
    
    /**
     * Bind material for rendering (uses shader + sets uniforms + binds textures)
     */
    virtual void Bind(RenderDevice* device) const = 0;
    
    /**
     * Unbind material
     */
    virtual void Unbind(RenderDevice* device) const = 0;
    
    /**
     * Get material name for debugging
     */
    virtual std::string GetName() const = 0;
    
    /**
     * Set material name
     */
    virtual void SetName(const std::string& name) = 0;
    
    // Uniform setters
    virtual void SetUniform1f(const std::string& name, float value) = 0;
    virtual void SetUniform2f(const std::string& name, float x, float y) = 0;
    virtual void SetUniform3f(const std::string& name, float x, float y, float z) = 0;
    virtual void SetUniform4f(const std::string& name, float x, float y, float z, float w) = 0;
    
    virtual void SetUniform1i(const std::string& name, int value) = 0;
    virtual void SetUniform2i(const std::string& name, int x, int y) = 0;
    virtual void SetUniform3i(const std::string& name, int x, int y, int z) = 0;
    virtual void SetUniform4i(const std::string& name, int x, int y, int z, int w) = 0;
    
    virtual void SetUniformVec2(const std::string& name, const glm::vec2& value) = 0;
    virtual void SetUniformVec3(const std::string& name, const glm::vec3& value) = 0;
    virtual void SetUniformVec4(const std::string& name, const glm::vec4& value) = 0;
    
    virtual void SetUniformMat4(const std::string& name, const glm::mat4& value) = 0;
    virtual void SetUniformMat3(const std::string& name, const glm::mat3& value) = 0;
    
    // Texture bindings
    /**
     * Bind a texture to a sampler uniform
     * @param name Uniform variable name (e.g., "uTexture")
     * @param texture Texture to bind
     * @param unit Texture unit to bind to
     */
    virtual void SetTexture(const std::string& name, std::shared_ptr<Texture> texture, uint32_t unit = 0) = 0;
    
    /**
     * Get bound texture
     */
    virtual std::shared_ptr<Texture> GetTexture(const std::string& name) const = 0;
    
    /**
     * Set texture by slot (for PBR workflows)
     */
    virtual void SetTextureSlot(TextureSlot slot, std::shared_ptr<Texture2D> texture) = 0;
    
    /**
     * Get texture by slot
     */
    virtual std::shared_ptr<Texture2D> GetTextureSlot(TextureSlot slot) const = 0;
    
    // Preset materials
    /**
     * Create a basic material with a solid color
     */
    static std::unique_ptr<Material> CreateSolidColor(RenderDevice* device, const glm::vec4& color);
    
    /**
     * Create a material with a simple texture
     */
    static std::unique_ptr<Material> CreateTextured(RenderDevice* device, std::shared_ptr<Texture2D> texture);
    
    /**
     * Create a standard PBR material
     * Requires textures: albedo, normal, metallic, roughness, ao
     */
    static std::unique_ptr<Material> CreatePBR(RenderDevice* device,
                                               std::shared_ptr<Texture2D> albedo,
                                               std::shared_ptr<Texture2D> normal,
                                               std::shared_ptr<Texture2D> metallic,
                                               std::shared_ptr<Texture2D> roughness,
                                               std::shared_ptr<Texture2D> ao);
    
    /**
     * Create a material with just albedo and normal
     */
    static std::unique_ptr<Material> CreateSimplePBR(RenderDevice* device,
                                                     std::shared_ptr<Texture2D> albedo,
                                                     std::shared_ptr<Texture2D> normal = nullptr);
    
    /**
     * Create a material from a single base color and parameters
     */
    static std::unique_ptr<Material> CreateParamPBR(RenderDevice* device,
                                                    const glm::vec3& albedo,
                                                    float metallic = 0.0f,
                                                    float roughness = 0.5f);
    
    /**
     * Create an unlit emissive material
     */
    static std::unique_ptr<Material> CreateEmissive(RenderDevice* device,
                                                   const glm::vec3& emissive_color,
                                                   std::shared_ptr<Texture2D> emission_texture = nullptr);

protected:
    Material() = default;
};

/**
 * @class PBRMaterial
 * @brief Advanced PBR material with physical parameters
 */
class PBRMaterial : public Material {
public:
    /**
     * Create a new PBR material
     */
    static std::unique_ptr<PBRMaterial> Create(RenderDevice* device, const std::string& name = "PBR");
    
    virtual ~PBRMaterial() = default;
    
    /**
     * Set base color/albedo
     */
    virtual void SetAlbedo(const glm::vec3& color) = 0;
    virtual glm::vec3 GetAlbedo() const = 0;
    
    /**
     * Set metallic value [0, 1]
     */
    virtual void SetMetallic(float value) = 0;
    virtual float GetMetallic() const = 0;
    
    /**
     * Set roughness value [0, 1]
     */
    virtual void SetRoughness(float value) = 0;
    virtual float GetRoughness() const = 0;
    
    /**
     * Set ambient occlusion strength [0, 1]
     */
    virtual void SetAOStrength(float value) = 0;
    virtual float GetAOStrength() const = 0;
    
    /**
     * Set normal map intensity [0, 1]
     */
    virtual void SetNormalStrength(float value) = 0;
    virtual float GetNormalStrength() const = 0;
    
    /**
     * Set emissive color and intensity
     */
    virtual void SetEmissive(const glm::vec3& color, float intensity = 1.0f) = 0;
    virtual glm::vec3 GetEmissive() const = 0;
    virtual float GetEmissiveIntensity() const = 0;
    
    /**
     * Set whether to use metallic-roughness workflow
     */
    virtual void SetMetallicRoughnessWorkflow(bool enabled) = 0;
    virtual bool IsMetallicRoughnessWorkflow() const = 0;
    
    /**
     * Enable/disable texture slots
     */
    virtual void EnableTextureSlot(TextureSlot slot, bool enabled) = 0;
    virtual bool IsTextureSlotEnabled(TextureSlot slot) const = 0;
    
    /**
     * Get material properties for CPU-side calculations
     */
    virtual glm::vec3 GetDiffuseColor() const = 0;
    virtual glm::vec3 GetF0() const = 0;  // Fresnel at zero degrees

protected:
    PBRMaterial() = default;
};

} // namespace schizo::renderer
