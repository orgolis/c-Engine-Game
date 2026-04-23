#include "material_asset.h"
#include "material.h"
#include "render_device.h"
#include "texture.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace schizo::asset {

using json = nlohmann::json;

// ============================================================================
// MaterialAssetData Serialization
// ============================================================================

std::string MaterialAssetData::ToJson() const {
    json j;
    
    j["name"] = name;
    j["type"] = type;
    
    // PBR parameters
    j["albedo"] = {albedo.r, albedo.g, albedo.b};
    j["metallic"] = metallic;
    j["roughness"] = roughness;
    j["ambient_occlusion"] = ambient_occlusion;
    j["emissive"] = {emissive.r, emissive.g, emissive.b};
    j["emissive_intensity"] = emissive_intensity;
    
    // Texture paths
    j["albedo_texture"] = albedo_texture_path;
    j["normal_texture"] = normal_texture_path;
    j["metallic_texture"] = metallic_texture_path;
    j["roughness_texture"] = roughness_texture_path;
    j["ao_texture"] = ao_texture_path;
    j["emissive_texture"] = emissive_texture_path;
    
    // Texture enable flags
    j["use_albedo_texture"] = use_albedo_texture;
    j["use_normal_texture"] = use_normal_texture;
    j["use_metallic_texture"] = use_metallic_texture;
    j["use_roughness_texture"] = use_roughness_texture;
    j["use_ao_texture"] = use_ao_texture;
    j["use_emissive_texture"] = use_emissive_texture;
    
    // Rendering flags
    j["double_sided"] = double_sided;
    j["cast_shadows"] = cast_shadows;
    j["receive_shadows"] = receive_shadows;
    
    return j.dump(2);
}

MaterialAssetData MaterialAssetData::FromJson(const std::string& json_str) {
    MaterialAssetData data;
    
    try {
        auto j = json::parse(json_str);
        
        data.name = j.value("name", "Material");
        data.type = j.value("type", "PBR");
        
        // PBR parameters
        if (j.contains("albedo")) {
            auto& alb = j["albedo"];
            data.albedo = glm::vec3(alb[0], alb[1], alb[2]);
        }
        data.metallic = j.value("metallic", 0.0f);
        data.roughness = j.value("roughness", 0.5f);
        data.ambient_occlusion = j.value("ambient_occlusion", 1.0f);
        
        if (j.contains("emissive")) {
            auto& em = j["emissive"];
            data.emissive = glm::vec3(em[0], em[1], em[2]);
        }
        data.emissive_intensity = j.value("emissive_intensity", 1.0f);
        
        // Texture paths
        data.albedo_texture_path = j.value("albedo_texture", "");
        data.normal_texture_path = j.value("normal_texture", "");
        data.metallic_texture_path = j.value("metallic_texture", "");
        data.roughness_texture_path = j.value("roughness_texture", "");
        data.ao_texture_path = j.value("ao_texture", "");
        data.emissive_texture_path = j.value("emissive_texture", "");
        
        // Texture enable flags
        data.use_albedo_texture = j.value("use_albedo_texture", false);
        data.use_normal_texture = j.value("use_normal_texture", false);
        data.use_metallic_texture = j.value("use_metallic_texture", false);
        data.use_roughness_texture = j.value("use_roughness_texture", false);
        data.use_ao_texture = j.value("use_ao_texture", false);
        data.use_emissive_texture = j.value("use_emissive_texture", false);
        
        // Rendering flags
        data.double_sided = j.value("double_sided", false);
        data.cast_shadows = j.value("cast_shadows", true);
        data.receive_shadows = j.value("receive_shadows", true);
        
    } catch (const std::exception& e) {
        spdlog::error("Error parsing material JSON: {}", e.what());
    }
    
    return data;
}

// ============================================================================
// MaterialAsset Implementation
// ============================================================================

std::shared_ptr<MaterialAsset> MaterialAsset::Create(const std::string& name) {
    auto asset = std::make_shared<MaterialAsset>();
    asset->data_.name = name;
    return asset;
}

std::shared_ptr<MaterialAsset> MaterialAsset::Load(const std::string& asset_path) {
    try {
        std::ifstream file(asset_path);
        if (!file.is_open()) {
            spdlog::warn("Material file not found: {}", asset_path);
            return nullptr;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();
        
        auto asset = std::make_shared<MaterialAsset>();
        asset->path_ = asset_path;
        asset->data_ = MaterialAssetData::FromJson(buffer.str());
        
        spdlog::info("Material loaded: {}", asset_path);
        return asset;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to load material {}: {}", asset_path, e.what());
        return nullptr;
    }
}

bool MaterialAsset::Save(const std::string& asset_path) const {
    try {
        std::ofstream file(asset_path);
        if (!file.is_open()) {
            spdlog::error("Cannot write material file: {}", asset_path);
            return false;
        }
        
        file << data_.ToJson();
        file.close();
        
        spdlog::info("Material saved: {}", asset_path);
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to save material: {}", e.what());
        return false;
    }
}

std::shared_ptr<schizo::renderer::Material> MaterialAsset::CreateRuntime(
    schizo::renderer::RenderDevice* device) const 
{
    if (!device) return nullptr;
    
    // Create PBR material with parameters
    auto material = schizo::renderer::Material::CreateParamPBR(
        device,
        data_.albedo,
        data_.metallic,
        data_.roughness
    );
    
    if (!material) return nullptr;
    
    // Set additional properties
    if (auto pbr = dynamic_cast<schizo::renderer::PBRMaterial*>(material.get())) {
        pbr->SetAOStrength(data_.ambient_occlusion);
        pbr->SetEmissive(data_.emissive, data_.emissive_intensity);
        pbr->SetName(data_.name);
    }
    
    return material;
}

// ============================================================================
// MaterialLibrary Implementation
// ============================================================================

void MaterialLibrary::Initialize(const std::string& asset_folder) {
    asset_folder_ = asset_folder;
    spdlog::info("Material library initialized at: {}", asset_folder);
}

std::shared_ptr<MaterialAsset> MaterialLibrary::GetMaterial(const std::string& name) {
    // Check cache first
    auto it = materials_.find(name);
    if (it != materials_.end()) {
        return it->second;
    }
    
    // Try to load from disk
    auto file_path = asset_folder_ + "/" + name + ".material";
    auto asset = MaterialAsset::Load(file_path);
    
    if (asset) {
        materials_[name] = asset;
    }
    
    return asset;
}

bool MaterialLibrary::SaveMaterial(const std::shared_ptr<MaterialAsset>& material) {
    if (!material) return false;
    
    auto file_path = asset_folder_ + "/" + material->GetName() + ".material";
    bool success = material->Save(file_path);
    
    if (success) {
        materials_[material->GetName()] = material;
    }
    
    return success;
}

std::vector<std::string> MaterialLibrary::GetAvailableMaterials() const {
    std::vector<std::string> names;
    for (const auto& [name, _] : materials_) {
        names.push_back(name);
    }
    return names;
}

void MaterialLibrary::CreateDefaultMaterials() {
    // Create basic preset materials
    struct {
        const char* name;
        glm::vec3 albedo;
        float metallic;
        float roughness;
    } presets[] = {
        {"white",    glm::vec3(1.0f, 1.0f, 1.0f), 0.0f, 0.8f},
        {"gray",     glm::vec3(0.5f, 0.5f, 0.5f), 0.0f, 0.5f},
        {"gold",     glm::vec3(1.0f, 0.85f, 0.0f), 1.0f, 0.2f},
        {"copper",   glm::vec3(0.97f, 0.68f, 0.19f), 1.0f, 0.3f},
        {"steel",    glm::vec3(0.77f, 0.78f, 0.78f), 1.0f, 0.1f},
        {"plastic",  glm::vec3(0.2f, 0.8f, 0.9f), 0.0f, 0.6f},
        {"rubber",   glm::vec3(0.1f, 0.1f, 0.1f), 0.0f, 0.9f},
    };
    
    for (const auto& preset : presets) {
        auto asset = Create(preset.name);
        asset->GetData().albedo = preset.albedo;
        asset->GetData().metallic = preset.metallic;
        asset->GetData().roughness = preset.roughness;
        materials_[preset.name] = asset;
    }
    
    spdlog::info("Created {} default materials", sizeof(presets) / sizeof(presets[0]));
}

void MaterialLibrary::Clear() {
    materials_.clear();
}

} // namespace schizo::asset
