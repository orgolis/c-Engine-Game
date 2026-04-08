#pragma once

#include <string>
#include <memory>
#include <cstdint>

namespace schizo::assets {
class MeshAsset;
}

namespace schizo::scene {

/**
 * @struct MeshComponent
 * @brief Component that references a mesh asset for rendering
 */
struct MeshComponent {
    /**
     * @brief Path to the mesh asset (relative to assets/models)
     */
    std::string mesh_path;
    
    /**
     * @brief Cached pointer to the loaded mesh asset
     */
    mutable std::shared_ptr<schizo::assets::MeshAsset> mesh_asset;
    
    /**
     * @brief Whether to use the asset mesh or fallback to default cube
     */
    bool use_asset_mesh = false;
    
    MeshComponent() = default;
    explicit MeshComponent(const std::string& path) 
        : mesh_path(path), use_asset_mesh(!path.empty()) {}
    
    /**
     * @brief Load/get the mesh asset
     */
    std::shared_ptr<schizo::assets::MeshAsset> GetMeshAsset() const;
    
    /**
     * @brief Set mesh from path
     */
    void SetMesh(const std::string& path) {
        mesh_path = path;
        mesh_asset = nullptr;  // Invalidate cache
        use_asset_mesh = !path.empty();
    }
    
    /**
     * @brief Invalidate cached mesh (e.g., after reload)
     */
    void InvalidateCache() {
        mesh_asset = nullptr;
    }
};

} // namespace schizo::scene
