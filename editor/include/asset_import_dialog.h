#pragma once

#include <string>
#include <memory>
#include <glm/glm.hpp>

// Forward declarations
namespace schizo::scene {
    class Scene;
    class Entity;
}

namespace schizo::editor {

/**
 * @struct AssetImportSettings
 * @brief Settings for importing assets
 */
struct AssetImportSettings {
    // General settings
    float scale = 1.0f;
    bool generate_normals = true;
    bool generate_tangents = true;
    
    // Mesh-specific settings
    bool import_animations = true;
    bool import_materials = true;
    bool optimize_mesh = false;
    
    // Material settings
    bool import_textures = true;
    
    // Scene settings
    bool create_entity = true;
    bool auto_place_in_scene = true;
    glm::vec3 spawn_position = glm::vec3(0.0f);
};

/**
 * @class AssetImportDialog
 * @brief Dialog for configuring asset import settings before adding to scene
 */
class AssetImportDialog {
public:
    AssetImportDialog();
    ~AssetImportDialog();
    
    /**
     * @brief Show the import dialog for an asset
     */
    void ShowImportDialog(const std::string& asset_path, const std::string& asset_type);
    
    /**
     * @brief Render the import dialog UI
     * @return true if import was confirmed, false if cancelled
     */
    bool RenderDialog();
    
    /**
     * @brief Get current import settings
     */
    const AssetImportSettings& GetSettings() const { return settings_; }
    
    /**
     * @brief Check if dialog is open
     */
    bool IsOpen() const { return is_open_; }
    
    /**
     * @brief Close the dialog
     */
    void Close() { is_open_ = false; }
    
private:
    AssetImportSettings settings_;
    std::string current_asset_path_;
    std::string current_asset_type_;
    bool is_open_ = false;
    bool confirmed_ = false;
};

} // namespace schizo::editor
