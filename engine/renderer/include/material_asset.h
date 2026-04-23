#pragma once

#include <memory>
#include <string>
#include <map>
#include <vector>
#include <glm/glm.hpp>

namespace schizo::renderer {
    class Material;
    class PBRMaterial;
    class Texture2D;
    class RenderDevice;
}

namespace schizo::asset {

/**
 * @struct MaterialAssetData
 * @brief Serializable material data for saving/loading
 */
struct MaterialAssetData {
    // Material metadata
    std::string name;
    std::string type = "PBR";  // PBR, Unlit, Custom
    
    // PBR Parameters
    glm::vec3 albedo = glm::vec3(0.8f);
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ambient_occlusion = 1.0f;
    glm::vec3 emissive = glm::vec3(0.0f);
    float emissive_intensity = 1.0f;
    
    // Texture asset paths
    std::string albedo_texture_path;
    std::string normal_texture_path;
    std::string metallic_texture_path;
    std::string roughness_texture_path;
    std::string ao_texture_path;
    std::string emissive_texture_path;
    
    // Texture slot enable flags
    bool use_albedo_texture = false;
    bool use_normal_texture = false;
    bool use_metallic_texture = false;
    bool use_roughness_texture = false;
    bool use_ao_texture = false;
    bool use_emissive_texture = false;
    
    // Rendering flags
    bool double_sided = false;
    bool cast_shadows = true;
    bool receive_shadows = true;
    
    // Serialize to/from JSON
    std::string ToJson() const;
    static MaterialAssetData FromJson(const std::string& json);
};

/**
 * @class MaterialAsset
 * @brief Asset wrapper for materials with serialization
 */
class MaterialAsset {
public:
    /**
     * Create a new material asset
     */
    static std::shared_ptr<MaterialAsset> Create(const std::string& name);
    
    /**
     * Load material from file
     */
    static std::shared_ptr<MaterialAsset> Load(const std::string& asset_path);
    
    /**
     * Save material to file
     */
    bool Save(const std::string& asset_path) const;
    
    /**
     * Get asset data
     */
    const MaterialAssetData& GetData() const { return data_; }
    
    /**
     * Set asset data
     */
    void SetData(const MaterialAssetData& data) { data_ = data; }
    
    /**
     * Create runtime material from this asset
     */
    std::shared_ptr<schizo::renderer::Material> CreateRuntime(
        schizo::renderer::RenderDevice* device) const;
    
    /**
     * Get asset name
     */
    const std::string& GetName() const { return data_.name; }
    
    /**
     * Get asset path
     */
    const std::string& GetPath() const { return path_; }
    
private:
    MaterialAsset() = default;
    
    MaterialAssetData data_;
    std::string path_;
};

/**
 * @class MaterialLibrary
 * @brief Manages loading and caching of material assets
 */
class MaterialLibrary {
public:
    /**
     * Initialize library with asset folder path
     */
    void Initialize(const std::string& asset_folder);
    
    /**
     * Load a material asset by name
     */
    std::shared_ptr<MaterialAsset> GetMaterial(const std::string& name);
    
    /**
     * Save a material asset
     */
    bool SaveMaterial(const std::shared_ptr<MaterialAsset>& material);
    
    /**
     * Get all available material names
     */
    std::vector<std::string> GetAvailableMaterials() const;
    
    /**
     * Create default materials
     */
    void CreateDefaultMaterials();
    
    /**
     * Clear all cached materials
     */
    void Clear();
    
private:
    std::map<std::string, std::shared_ptr<MaterialAsset>> materials_;
    std::string asset_folder_;
};

} // namespace schizo::asset
