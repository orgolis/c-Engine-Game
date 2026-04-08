#pragma once

#include "mesh_asset.h"
#include <map>
#include <memory>
#include <string>

namespace schizo::assets {

/**
 * @class AssetManager
 * @brief Manages loading, caching, and access to mesh assets
 */
class AssetManager {
public:
    static AssetManager& GetInstance() {
        static AssetManager instance;
        return instance;
    }
    
    // Deleted copy
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;
    
    /**
     * @brief Load a mesh asset from file
     * @param filepath Path to the mesh file (relative to assets folder)
     * @return Pointer to loaded MeshAsset, nullptr if failed
     */
    std::shared_ptr<MeshAsset> LoadMesh(const std::string& filepath);
    
    /**
     * @brief Get a cached mesh asset
     * @param filepath Path to the mesh file
     * @return Pointer to cached MeshAsset, nullptr if not loaded
     */
    std::shared_ptr<MeshAsset> GetMesh(const std::string& filepath) const;
    
    /**
     * @brief Unload a mesh asset
     * @param filepath Path to the mesh file
     */
    void UnloadMesh(const std::string& filepath);
    
    /**
     * @brief Clear all loaded meshes
     */
    void Clear();
    
    /**
     * @brief Get list of loaded mesh paths
     */
    std::vector<std::string> GetLoadedMeshes() const;
    
    /**
     * @brief Set the base asset directory path
     */
    void SetAssetDirectory(const std::string& path) {
        asset_directory_ = path;
    }
    
    /**
     * @brief Get the base asset directory path
     */
    const std::string& GetAssetDirectory() const {
        return asset_directory_;
    }

private:
    AssetManager();
    ~AssetManager() = default;
    
    /**
     * @brief Load glTF/glB mesh file
     */
    std::shared_ptr<MeshAsset> LoadGltfMesh(const std::string& filepath);
    
    /**
     * @brief Load OBJ mesh file
     */
    std::shared_ptr<MeshAsset> LoadObjMesh(const std::string& filepath);
    
    std::map<std::string, std::shared_ptr<MeshAsset>> loaded_meshes_;
    std::string asset_directory_ = "assets/models";
};

} // namespace schizo::assets
