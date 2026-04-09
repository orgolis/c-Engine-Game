#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <string>

// Forward declarations
namespace schizo::scene {
    class Entity;
}

namespace schizo::renderer {
    class Material;
    class PBRMaterial;
}

namespace schizo::editor {

/**
 * @struct MaterialEditorState
 * @brief State for material editing
 */
struct MaterialEditorState {
    // PBR Parameters
    glm::vec3 albedo = glm::vec3(0.8f);
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ambient_occlusion = 1.0f;
    glm::vec3 emissive = glm::vec3(0.0f);
    
    // Texture slots
    std::string albedo_texture;
    std::string normal_texture;
    std::string metallic_texture;
    std::string roughness_texture;
    std::string ao_texture;
    std::string emissive_texture;
    
    // Material preset
    int preset_index = 0;  // Index into preset list
    std::string preset_name;
};

/**
 * @class MaterialEditorPanel
 * @brief Material editor for the inspector
 * 
 * Features:
 * - PBR parameter editing
 * - Texture slot assignment
 * - Material presets
 * - Live preview
 */
class MaterialEditorPanel {
public:
    MaterialEditorPanel();
    ~MaterialEditorPanel();
    
    /**
     * @brief Render material editor for entity
     */
    void Render(const std::shared_ptr<schizo::scene::Entity>& entity);
    
    /**
     * @brief Set the edited material state
     */
    void SetMaterialState(const MaterialEditorState& state);
    
    /**
     * @brief Get current material state
     */
    const MaterialEditorState& GetMaterialState() const { return state_; }
    
private:
    MaterialEditorState state_;
    
    void RenderPBRParameters();
    void RenderTextureSlots();
    void RenderPresets();
    void RenderPreview();
    void ApplyMaterialToEntity(const std::shared_ptr<schizo::scene::Entity>& entity);
};

} // namespace schizo::editor
