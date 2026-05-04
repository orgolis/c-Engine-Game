#pragma once

#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include <cstdint>

// Forward declarations
namespace schizo::assets {
    class MeshAsset;
}

namespace schizo::scene {
    class Scene;
}

namespace schizo::editor {

/**
 * @struct AssetMetadata
 * @brief Metadata about an asset file
 */
struct AssetMetadata {
    std::string path;
    std::string filename;
    std::string extension;
    size_t file_size = 0;
    std::string asset_type;  // "Mesh", "Texture", "Material", etc.
    
    // For meshes
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
    uint32_t mesh_count = 0;
    
    // Preview data
    uint32_t preview_texture = 0;
    bool preview_ready = false;
};

/**
 * @struct AssetBrowserState
 * @brief State for asset browser UI
 */
struct AssetBrowserState {
    std::string current_directory = "assets/models";
    std::vector<AssetMetadata> discovered_assets;
    int selected_asset_index = -1;
    
    // Filter settings
    bool show_meshes = true;
    bool show_textures = true;
    bool show_materials = true;
    
    // Preview settings
    float preview_size = 100.0f;
    bool show_filter_bar = true;
    
    // Import dialog state
    bool show_import_dialog = false;
    std::string import_file_path;
    std::string import_target_path = "assets/models";
    char import_filename_buffer[256] = {};
};

/**
 * @class AssetBrowserPanel
 * @brief Advanced asset browser panel for the editor
 * 
 * Features:
 * - Directory browsing
 * - Asset filtering (meshes, textures, materials)
 * - Mesh preview with vertex/index info
 * - Texture preview with size
 * - Drag-and-drop asset assignment
 * - Asset metadata display
 */
class AssetBrowserPanel {
public:
    AssetBrowserPanel();
    ~AssetBrowserPanel();
    
    /**
     * @brief Render the asset browser UI
     * @param scene Current scene for asset assignment
     * @param open Optional pointer to visibility flag (close button)
     */
    void Render(const std::shared_ptr<schizo::scene::Scene>& scene, bool* open = nullptr);
    
    /**
     * @brief Get the state of the asset browser
     */
    AssetBrowserState& GetState() { return state_; }
    
    /**
     * @brief Refresh asset list from directory
     */
    void RefreshAssets();
    
    /**
     * @brief Set the current directory to browse
     */
    void SetDirectory(const std::string& path);
    
    /**
     * @brief Get currently selected asset
     */
    const AssetMetadata* GetSelectedAsset() const;
    
    /**
     * @brief Get asset by index
     */
    const AssetMetadata* GetAssetByIndex(size_t index) const;
    
private:
    AssetBrowserState state_;
    
    // Helper methods
    void RenderToolbar();
    void RenderAssetGrid();
    void RenderAssetDetails();
    void RenderAssetPreview(const AssetMetadata& asset);
    void RenderImportDialog();
    void DiscoverAssets(const std::string& directory);
    void LoadAssetMetadata(const std::string& path, AssetMetadata& metadata);
    void LoadTexturePreview(const std::string& path, AssetMetadata& metadata);
    void GeneratePreviewTexture(AssetMetadata& asset);
    void ImportFile(const std::string& file_path, const std::string& target_path);
    std::string ShowOpenFileDialog();
};

} // namespace schizo::editor
