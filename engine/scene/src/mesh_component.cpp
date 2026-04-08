#include "mesh_component.h"
#include "asset_manager.h"

namespace schizo::scene {

std::shared_ptr<schizo::assets::MeshAsset> MeshComponent::GetMeshAsset() const {
    if (!use_asset_mesh || mesh_path.empty()) {
        return nullptr;
    }
    
    // Load from cache or load if not cached
    if (!mesh_asset) {
        mesh_asset = schizo::assets::AssetManager::GetInstance().GetMesh(mesh_path);
        
        // Try loading if not cached
        if (!mesh_asset) {
            mesh_asset = schizo::assets::AssetManager::GetInstance().LoadMesh(mesh_path);
        }
    }
    
    return mesh_asset;
}

} // namespace schizo::scene
