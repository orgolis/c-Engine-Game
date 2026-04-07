#include "pbr.h"
#include "render_device.h"
#include <spdlog/spdlog.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace schizo::renderer {

// ============================================================================
// PBRPresets Implementation
// ============================================================================

std::unique_ptr<PBRMaterial> PBRPresets::Gold(RenderDevice* device) {
    auto material = PBRMaterial::Create(device, "Gold");
    if (material) {
        material->SetAlbedo(glm::vec3(1.0f, 0.766f, 0.336f));
        material->SetMetallic(1.0f);
        material->SetRoughness(0.2f);
        spdlog::debug("Created Gold material");
    }
    return material;
}

std::unique_ptr<PBRMaterial> PBRPresets::Copper(RenderDevice* device) {
    auto material = PBRMaterial::Create(device, "Copper");
    if (material) {
        material->SetAlbedo(glm::vec3(0.955f, 0.637f, 0.538f));
        material->SetMetallic(1.0f);
        material->SetRoughness(0.3f);
        spdlog::debug("Created Copper material");
    }
    return material;
}

std::unique_ptr<PBRMaterial> PBRPresets::Steel(RenderDevice* device) {
    auto material = PBRMaterial::Create(device, "Steel");
    if (material) {
        material->SetAlbedo(glm::vec3(0.77f, 0.78f, 0.78f));
        material->SetMetallic(1.0f);
        material->SetRoughness(0.25f);
        spdlog::debug("Created Steel material");
    }
    return material;
}

std::unique_ptr<PBRMaterial> PBRPresets::Aluminum(RenderDevice* device) {
    auto material = PBRMaterial::Create(device, "Aluminum");
    if (material) {
        material->SetAlbedo(glm::vec3(0.913f, 0.921f, 0.924f));
        material->SetMetallic(1.0f);
        material->SetRoughness(0.35f);
        spdlog::debug("Created Aluminum material");
    }
    return material;
}

std::unique_ptr<PBRMaterial> PBRPresets::Brass(RenderDevice* device) {
    auto material = PBRMaterial::Create(device, "Brass");
    if (material) {
        material->SetAlbedo(glm::vec3(0.913f, 0.847f, 0.598f));
        material->SetMetallic(1.0f);
        material->SetRoughness(0.28f);
        spdlog::debug("Created Brass material");
    }
    return material;
}

std::unique_ptr<PBRMaterial> PBRPresets::Plastic(RenderDevice* device, const glm::vec3& color) {
    auto material = PBRMaterial::Create(device, "Plastic");
    if (material) {
        material->SetAlbedo(color);
        material->SetMetallic(0.0f);
        material->SetRoughness(0.6f);
        spdlog::debug("Created Plastic material");
    }
    return material;
}

std::unique_ptr<PBRMaterial> PBRPresets::Fabric(RenderDevice* device, const glm::vec3& color) {
    auto material = PBRMaterial::Create(device, "Fabric");
    if (material) {
        material->SetAlbedo(color);
        material->SetMetallic(0.0f);
        material->SetRoughness(0.8f);
        spdlog::debug("Created Fabric material");
    }
    return material;
}

std::unique_ptr<PBRMaterial> PBRPresets::Ceramic(RenderDevice* device, const glm::vec3& color) {
    auto material = PBRMaterial::Create(device, "Ceramic");
    if (material) {
        material->SetAlbedo(color);
        material->SetMetallic(0.0f);
        material->SetRoughness(0.2f);
        spdlog::debug("Created Ceramic material");
    }
    return material;
}

std::unique_ptr<PBRMaterial> PBRPresets::Rubber(RenderDevice* device, const glm::vec3& color) {
    auto material = PBRMaterial::Create(device, "Rubber");
    if (material) {
        material->SetAlbedo(color);
        material->SetMetallic(0.0f);
        material->SetRoughness(0.9f);
        spdlog::debug("Created Rubber material");
    }
    return material;
}

std::unique_ptr<PBRMaterial> PBRPresets::Glass(RenderDevice* device) {
    auto material = PBRMaterial::Create(device, "Glass");
    if (material) {
        material->SetAlbedo(glm::vec3(1.0f));
        material->SetMetallic(0.0f);
        material->SetRoughness(0.05f);
        spdlog::debug("Created Glass material");
    }
    return material;
}

std::unique_ptr<PBRMaterial> PBRPresets::Skin(RenderDevice* device, const glm::vec3& tone) {
    auto material = PBRMaterial::Create(device, "Skin");
    if (material) {
        // Skin is generally not metallic and has moderate roughness
        // Tone varies from pale to dark brown
        material->SetAlbedo(tone);
        material->SetMetallic(0.0f);
        material->SetRoughness(0.35f);
        material->SetAOStrength(0.8f);
        spdlog::debug("Created Skin material");
    }
    return material;
}

std::unique_ptr<PBRMaterial> PBRPresets::Wood(RenderDevice* device, const glm::vec3& color) {
    auto material = PBRMaterial::Create(device, "Wood");
    if (material) {
        material->SetAlbedo(color);
        material->SetMetallic(0.0f);
        material->SetRoughness(0.5f);
        spdlog::debug("Created Wood material");
    }
    return material;
}

std::unique_ptr<PBRMaterial> PBRPresets::DiffuseLambert(RenderDevice* device, const glm::vec3& color) {
    auto material = PBRMaterial::Create(device, "DiffuseLambert");
    if (material) {
        material->SetAlbedo(color);
        material->SetMetallic(0.0f);
        material->SetRoughness(1.0f);  // Fully rough = diffuse Lambert
        material->SetAOStrength(1.0f);
        spdlog::debug("Created Lambertian Diffuse material");
    }
    return material;
}

std::unique_ptr<PBRMaterial> PBRPresets::Mirror(RenderDevice* device) {
    auto material = PBRMaterial::Create(device, "Mirror");
    if (material) {
        material->SetAlbedo(glm::vec3(1.0f));
        material->SetMetallic(1.0f);
        material->SetRoughness(0.0f);  // Perfectly smooth mirror
        spdlog::debug("Created Mirror material");
    }
    return material;
}

// ============================================================================
// MaterialLibrary Implementation
// ============================================================================

class OpenGLMaterialLibrary : public MaterialLibrary {
public:
    explicit OpenGLMaterialLibrary(RenderDevice* device) : device_(device) {}
    
    ~OpenGLMaterialLibrary() override = default;
    
    void RegisterMaterial(const std::string& name, std::unique_ptr<Material> material) override {
        if (!material) {
            spdlog::warn("MaterialLibrary::RegisterMaterial - Null material for name '{}'", name);
            return;
        }
        
        if (material_map_.find(name) != material_map_.end()) {
            spdlog::warn("MaterialLibrary::RegisterMaterial - Overwriting existing material '{}'", name);
        }
        
        material_map_[name] = std::move(material);
        spdlog::debug("MaterialLibrary::RegisterMaterial - Registered material '{}'", name);
    }
    
    std::shared_ptr<Material> GetMaterial(const std::string& name) override {
        auto it = material_map_.find(name);
        if (it == material_map_.end()) {
            spdlog::warn("MaterialLibrary::GetMaterial - Material '{}' not found", name);
            return nullptr;
        }
        
        // Return shared_ptr to the material for reference counting
        return std::shared_ptr<Material>(it->second.get(), [](Material*) {});
    }
    
    std::shared_ptr<PBRMaterial> GetPBRMaterial(const std::string& name) override {
        auto material = GetMaterial(name);
        if (!material) {
            return nullptr;
        }
        
        // Try to cast to PBRMaterial
        void* ptr = material.get();
        auto pbr = static_cast<PBRMaterial*>(ptr);
        
        if (pbr && pbr->GetType() == MaterialType::StandardPBR) {
            return std::shared_ptr<PBRMaterial>(pbr, [](PBRMaterial*) {});
        }
        
        spdlog::warn("MaterialLibrary::GetPBRMaterial - Material '{}' is not a PBR material", name);
        return nullptr;
    }
    
    bool HasMaterial(const std::string& name) const override {
        return material_map_.find(name) != material_map_.end();
    }
    
    void RemoveMaterial(const std::string& name) override {
        auto it = material_map_.find(name);
        if (it != material_map_.end()) {
            material_map_.erase(it);
            spdlog::debug("MaterialLibrary::RemoveMaterial - Removed material '{}'", name);
        } else {
            spdlog::warn("MaterialLibrary::RemoveMaterial - Material '{}' not found", name);
        }
    }
    
    std::shared_ptr<PBRMaterial> CreatePBRFromTextures(const std::string& name, 
                                                      const PBRTextureSet& textures) override {
        auto material = PBRMaterial::Create(device_, name);
        if (!material) {
            return nullptr;
        }
        
        if (textures.albedo) {
            material->SetTextureSlot(TextureSlot::Albedo, textures.albedo);
        }
        if (textures.normal) {
            material->SetTextureSlot(TextureSlot::Normal, textures.normal);
        }
        if (textures.metallic) {
            material->SetTextureSlot(TextureSlot::Metallic, textures.metallic);
        }
        if (textures.roughness) {
            material->SetTextureSlot(TextureSlot::Roughness, textures.roughness);
        }
        if (textures.ao) {
            material->SetTextureSlot(TextureSlot::AmbientOcclusion, textures.ao);
        }
        if (textures.emissive) {
            material->SetTextureSlot(TextureSlot::Emissive, textures.emissive);
        }
        if (textures.height) {
            material->SetTextureSlot(TextureSlot::Height, textures.height);
        }
        if (textures.cavity) {
            material->SetTextureSlot(TextureSlot::CavityMap, textures.cavity);
        }
        
        // Don't store in map, return directly
        spdlog::debug("MaterialLibrary::CreatePBRFromTextures - Created PBR material '{}'", name);
        return material;
    }
    
    std::shared_ptr<PBRMaterial> CreateParametricPBR(const std::string& name,
                                                     const glm::vec3& albedo,
                                                     float metallic,
                                                     float roughness) override {
        auto material = Material::CreateParamPBR(device_, albedo, metallic, roughness);
        if (!material) {
            return nullptr;
        }
        
        // Store in library for future access
        material_map_[name] = std::move(material);
        
        spdlog::debug("MaterialLibrary::CreateParametricPBR - Created parametric PBR material '{}'", name);
        return nullptr;  // Material was moved
    }
    
    std::vector<std::string> GetMaterialNames() const override {
        std::vector<std::string> names;
        for (const auto& [name, _] : material_map_) {
            names.push_back(name);
        }
        return names;
    }
    
    void Clear() override {
        material_map_.clear();
        spdlog::debug("MaterialLibrary::Clear - Cleared all materials");
    }

private:
    RenderDevice* device_;
    std::unordered_map<std::string, std::unique_ptr<Material>> material_map_;
};

std::unique_ptr<MaterialLibrary> MaterialLibrary::Create(RenderDevice* device) {
    if (!device) {
        spdlog::error("MaterialLibrary::Create - No render device");
        return nullptr;
    }
    
    return std::make_unique<OpenGLMaterialLibrary>(device);
}

// ============================================================================
// PBRMaterialBuilder Implementation
// ============================================================================

PBRMaterialBuilder::PBRMaterialBuilder(RenderDevice* device)
    : device_(device), name_("PBRMaterial") {}

PBRMaterialBuilder& PBRMaterialBuilder::SetName(const std::string& name) {
    name_ = name;
    return *this;
}

PBRMaterialBuilder& PBRMaterialBuilder::SetAlbedo(const glm::vec3& color) {
    albedo_ = color;
    return *this;
}

PBRMaterialBuilder& PBRMaterialBuilder::SetAlbedoTexture(std::shared_ptr<Texture2D> texture) {
    albedo_texture_ = texture;
    return *this;
}

PBRMaterialBuilder& PBRMaterialBuilder::SetMetallic(float value) {
    metallic_ = glm::clamp(value, 0.0f, 1.0f);
    return *this;
}

PBRMaterialBuilder& PBRMaterialBuilder::SetMetallicTexture(std::shared_ptr<Texture2D> texture) {
    metallic_texture_ = texture;
    return *this;
}

PBRMaterialBuilder& PBRMaterialBuilder::SetRoughness(float value) {
    roughness_ = glm::clamp(value, 0.0f, 1.0f);
    return *this;
}

PBRMaterialBuilder& PBRMaterialBuilder::SetRoughnessTexture(std::shared_ptr<Texture2D> texture) {
    roughness_texture_ = texture;
    return *this;
}

PBRMaterialBuilder& PBRMaterialBuilder::SetNormalMap(std::shared_ptr<Texture2D> texture, float strength) {
    normal_texture_ = texture;
    normal_strength_ = glm::clamp(strength, 0.0f, 2.0f);
    return *this;
}

PBRMaterialBuilder& PBRMaterialBuilder::SetAmbientOcclusion(std::shared_ptr<Texture2D> texture, float strength) {
    ao_texture_ = texture;
    ao_strength_ = glm::clamp(strength, 0.0f, 1.0f);
    return *this;
}

PBRMaterialBuilder& PBRMaterialBuilder::SetEmissive(const glm::vec3& color, float intensity) {
    emissive_ = color;
    emissive_intensity_ = glm::max(0.0f, intensity);
    return *this;
}

PBRMaterialBuilder& PBRMaterialBuilder::SetEmissiveTexture(std::shared_ptr<Texture2D> texture) {
    emissive_texture_ = texture;
    return *this;
}

PBRMaterialBuilder& PBRMaterialBuilder::SetHeightMap(std::shared_ptr<Texture2D> texture) {
    height_texture_ = texture;
    return *this;
}

PBRMaterialBuilder& PBRMaterialBuilder::SetCavityMap(std::shared_ptr<Texture2D> texture) {
    cavity_texture_ = texture;
    return *this;
}

PBRMaterialBuilder& PBRMaterialBuilder::SetWorkflow(bool metallic_roughness) {
    metallic_roughness_workflow_ = metallic_roughness;
    return *this;
}

std::unique_ptr<PBRMaterial> PBRMaterialBuilder::Build() {
    if (!device_) {
        spdlog::error("PBRMaterialBuilder::Build - No render device");
        return nullptr;
    }
    
    auto material = PBRMaterial::Create(device_, name_);
    if (!material) {
        return nullptr;
    }
    
    // Set base parameters
    material->SetAlbedo(albedo_);
    material->SetMetallic(metallic_);
    material->SetRoughness(roughness_);
    material->SetAOStrength(ao_strength_);
    material->SetNormalStrength(normal_strength_);
    material->SetEmissive(emissive_, emissive_intensity_);
    material->SetMetallicRoughnessWorkflow(metallic_roughness_workflow_);
    
    // Set textures
    if (albedo_texture_) {
        material->SetTextureSlot(TextureSlot::Albedo, albedo_texture_);
    }
    if (metallic_texture_) {
        material->SetTextureSlot(TextureSlot::Metallic, metallic_texture_);
    }
    if (roughness_texture_) {
        material->SetTextureSlot(TextureSlot::Roughness, roughness_texture_);
    }
    if (normal_texture_) {
        material->SetTextureSlot(TextureSlot::Normal, normal_texture_);
    }
    if (ao_texture_) {
        material->SetTextureSlot(TextureSlot::AmbientOcclusion, ao_texture_);
    }
    if (emissive_texture_) {
        material->SetTextureSlot(TextureSlot::Emissive, emissive_texture_);
    }
    if (height_texture_) {
        material->SetTextureSlot(TextureSlot::Height, height_texture_);
    }
    if (cavity_texture_) {
        material->SetTextureSlot(TextureSlot::CavityMap, cavity_texture_);
    }
    
    spdlog::debug("PBRMaterialBuilder::Build - Built material '{}'", name_);
    return material;
}

// ============================================================================
// PBRMaterialUtils Implementation
// ============================================================================

glm::vec3 PBRMaterialUtils::LinearToSRGB(const glm::vec3& linear) {
    return glm::mix(
        glm::vec3(12.92f) * linear,
        glm::vec3(1.055f) * glm::pow(linear, glm::vec3(1.0f / 2.4f)) - glm::vec3(0.055f),
        glm::greaterThanEqual(linear, glm::vec3(0.0031308f))
    );
}

glm::vec3 PBRMaterialUtils::SRGBToLinear(const glm::vec3& srgb) {
    return glm::mix(
        srgb / glm::vec3(12.92f),
        glm::pow((srgb + glm::vec3(0.055f)) / glm::vec3(1.055f), glm::vec3(2.4f)),
        glm::greaterThanEqual(srgb, glm::vec3(0.04045f))
    );
}

glm::vec3 PBRMaterialUtils::CalculateF0(float refractive_index) {
    float r = refractive_index - 1.0f;
    float g = refractive_index + 1.0f;
    float fresnel = (r * r) / (g * g);
    return glm::vec3(fresnel);
}

float PBRMaterialUtils::ClampRoughness(float roughness) {
    // Clamp to avoid division by zero in roughness calculations
    return glm::clamp(roughness, 0.02f, 1.0f);
}

float PBRMaterialUtils::SpecularToRoughness(float specular) {
    // Convert specular value to roughness
    // Common approximation: roughness = sqrt(2 / (specular + 2))
    return glm::sqrt(2.0f / (specular + 2.0f));
}

void PBRMaterialUtils::ConvertSpecularGloss(float specular, float gloss,
                                           glm::vec3& out_albedo,
                                           float& out_metallic,
                                           float& out_roughness) {
    // Convert legacy specular/gloss to metallic/roughness
    out_metallic = 0.0f;  // Most specular-gloss materials are dielectrics
    out_roughness = 1.0f - gloss;  // Inverse relationship
}

bool PBRMaterialUtils::IsMetallic(float metallic_value) {
    return metallic_value > 0.5f;
}

void PBRMaterialUtils::GetMaterialParams(const std::string& material_name,
                                        glm::vec3& out_albedo,
                                        float& out_metallic,
                                        float& out_roughness) {
    // Real-world material parameters (Unreal/Unity PBR)
    if (material_name == "Gold") {
        out_albedo = glm::vec3(1.0f, 0.766f, 0.336f);
        out_metallic = 1.0f;
        out_roughness = 0.2f;
    } else if (material_name == "Copper") {
        out_albedo = glm::vec3(0.955f, 0.637f, 0.538f);
        out_metallic = 1.0f;
        out_roughness = 0.3f;
    } else if (material_name == "Steel") {
        out_albedo = glm::vec3(0.77f, 0.78f, 0.78f);
        out_metallic = 1.0f;
        out_roughness = 0.25f;
    } else if (material_name == "Aluminum") {
        out_albedo = glm::vec3(0.913f, 0.921f, 0.924f);
        out_metallic = 1.0f;
        out_roughness = 0.35f;
    } else {
        out_albedo = glm::vec3(0.9f);
        out_metallic = 0.0f;
        out_roughness = 0.5f;
    }
}

} // namespace schizo::renderer
