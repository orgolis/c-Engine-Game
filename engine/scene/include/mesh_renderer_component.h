#pragma once

#include "entity.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <cstdint>

namespace schizo::scene {

// ============================================================================
// Mesh Renderer Component
// ============================================================================

/**
 * @enum MeshType
 * @brief Predefined mesh types for simple visualization
 */
enum class MeshType {
    Cube,
    Sphere,
    Cylinder,
    Plane,
    Pyramid,
    Capsule
};

/**
 * @enum AlphaMode
 * @brief How the renderer should treat per-fragment alpha.
 *
 *  - Opaque: alpha is ignored; rendered in the G-Buffer pass.
 *  - Cutout: G-Buffer pass `discard`s fragments below alpha_cutoff_. Hard edges.
 *  - Blend:  rendered in the forward transparent pass after lighting, with
 *            depth test on / depth write off and source-over alpha blending.
 *            Real translucency (glass, water, decals, particle billboards).
 */
enum class AlphaMode : uint8_t {
    Opaque = 0,
    Cutout = 1,
    Blend  = 2,
};

/**
 * @class MeshRendererComponent
 * @brief Component that renders a mesh with color
 */
class MeshRendererComponent : public Component {
public:
    MeshRendererComponent(MeshType type = MeshType::Cube, const glm::vec4& color = glm::vec4(1.0f))
        : mesh_type_(type), color_(color) {}
    
    virtual ~MeshRendererComponent() = default;
    
    // Deleted copy, allow move
    MeshRendererComponent(const MeshRendererComponent&) = delete;
    MeshRendererComponent& operator=(const MeshRendererComponent&) = delete;
    
    MeshRendererComponent(MeshRendererComponent&&) = default;
    MeshRendererComponent& operator=(MeshRendererComponent&&) = default;
    
    /**
     * @brief Get mesh type
     */
    MeshType GetMeshType() const { return mesh_type_; }
    
    /**
     * @brief Set mesh type
     */
    void SetMeshType(MeshType type) { mesh_type_ = type; }
    
    /**
     * @brief Get mesh color
     */
    const glm::vec4& GetColor() const { return color_; }
    
    /**
     * @brief Set mesh color
     */
    void SetColor(const glm::vec4& color) { color_ = color; }
    
    /**
     * @brief Set mesh color with individual components
     */
    void SetColor(float r, float g, float b, float a = 1.0f) {
        color_ = glm::vec4(r, g, b, a);
    }

    AlphaMode GetAlphaMode() const { return alpha_mode_; }
    void      SetAlphaMode(AlphaMode m) { alpha_mode_ = m; }

    // Compatibility wrappers — older call sites and the serializer used to
    // treat transparency as a single bool. Keep them so they still work,
    // mapped onto the new enum.
    bool IsTransparent() const { return alpha_mode_ != AlphaMode::Opaque; }
    void SetTransparent(bool t) {
        if (!t) alpha_mode_ = AlphaMode::Opaque;
        else if (alpha_mode_ == AlphaMode::Opaque) alpha_mode_ = AlphaMode::Cutout;
    }

    float GetAlphaCutoff() const { return alpha_cutoff_; }
    void  SetAlphaCutoff(float c) { alpha_cutoff_ = glm::clamp(c, 0.0f, 1.0f); }

    // ---- PBR factors --------------------------------------------------
    // These feed straight into MaterialUniforms.{metallic,roughness,occlusion,emissive}
    // via EntityMaterialCache::get_or_create. Defaults match what the
    // editor used to hardcode (slightly dielectric, fairly rough, fully lit).

    float GetMetallic() const { return metallic_; }
    void  SetMetallic(float m) { metallic_ = glm::clamp(m, 0.0f, 1.0f); }

    float GetRoughness() const { return roughness_; }
    void  SetRoughness(float r) { roughness_ = glm::clamp(r, 0.04f, 1.0f); }

    float GetOcclusion() const { return occlusion_; }
    void  SetOcclusion(float o) { occlusion_ = glm::clamp(o, 0.0f, 1.0f); }

    const glm::vec3& GetEmissive() const { return emissive_; }
    void  SetEmissive(const glm::vec3& e) { emissive_ = glm::max(e, glm::vec3(0.0f)); }

    /// HDR multiplier on the emissive colour. The bloom threshold is ~1.0,
    /// so an emissive colour at [0,1] barely glows — push this to 2–10 to
    /// make a surface read as a real, blooming light source ("self glow").
    float GetEmissiveIntensity() const { return emissive_intensity_; }
    void  SetEmissiveIntensity(float i) { emissive_intensity_ = glm::max(i, 0.0f); }

    /// Emissive colour pre-multiplied by intensity — what the material/
    /// G-Buffer actually receives.
    glm::vec3 GetEmissiveLinear() const { return emissive_ * emissive_intensity_; }

    // ---- Albedo (base colour) texture --------------------------------
    // Optional path to a base-colour texture for this primitive. When set, the
    // editor's EntityMaterialCache loads it through the TextureManager (mipped,
    // sRGB, deduplicated, hot-reloadable) and binds it as the material's
    // base-colour slot; `color_` then multiplies the sampled texel. Empty = the
    // flat `color_` only (shared white default).
    const std::string& GetAlbedoTexturePath() const { return albedo_texture_path_; }
    void SetAlbedoTexturePath(const std::string& p) { albedo_texture_path_ = p; }
    bool HasAlbedoTexture() const { return !albedo_texture_path_.empty(); }

protected:
    MeshType  mesh_type_      = MeshType::Cube;
    glm::vec4 color_          = glm::vec4(1.0f);  // White by default
    AlphaMode alpha_mode_     = AlphaMode::Opaque;
    float     alpha_cutoff_   = 0.5f;            // Standard glTF default (Cutout mode)

    // Default to fully dielectric, fully rough — i.e. truly non-reflective.
    // Previously this defaulted to metallic=0.05 / roughness=0.7 which left
    // a faint stochastic SSR + IBL contribution on every surface that
    // looked like noise/sparkles. Place a real material on the entity
    // (or override these values) when a reflective look is wanted.
    float     metallic_       = 0.0f;
    float     roughness_      = 1.0f;
    float     occlusion_      = 1.0f;
    glm::vec3 emissive_       = glm::vec3(0.0f);
    float     emissive_intensity_ = 1.0f;
    std::string albedo_texture_path_;   // optional base-colour texture
};

} // namespace schizo::scene
