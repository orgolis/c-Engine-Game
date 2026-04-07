#include "material.h"
#include "render_device.h"
#include <spdlog/spdlog.h>
#include <glm/gtc/type_ptr.hpp>

namespace schizo::renderer {

// ============================================================================
// OpenGL Material Implementation
// ============================================================================

class OpenGLMaterial : public Material {
public:
    explicit OpenGLMaterial(std::shared_ptr<ShaderProgram> shader, const std::string& name = "Material")
        : shader_(shader), name_(name), material_type_(MaterialType::Lit) {
        if (!shader_) {
            spdlog::warn("Material created with null shader");
        }
    }
    
    ~OpenGLMaterial() override = default;
    
    MaterialType GetType() const override {
        return material_type_;
    }
    
    std::shared_ptr<ShaderProgram> GetShader() const override {
        return shader_;
    }
    
    std::string GetName() const override {
        return name_;
    }
    
    void SetName(const std::string& name) override {
        name_ = name;
    }
    
    void Bind(RenderDevice* device) const override {
        if (!device || !shader_) {
            spdlog::warn("Material::Bind - Invalid device or shader");
            return;
        }
        
        shader_->Use(device);
        
        // Bind all textures
        for (const auto& [name, texture_info] : texture_bindings_) {
            if (texture_info.first) {
                texture_info.first->Bind(device, texture_info.second);
            }
        }
        
        // Upload uniforms
        for (const auto& [name, value] : uniform_cache_) {
            shader_->SetUniform(device, name, value);
        }
        
        for (const auto& [name, value] : int_uniform_cache_) {
            shader_->SetUniform(device, name, value);
        }
        
        for (const auto& [name, value] : mat4_cache_) {
            shader_->SetUniform(device, name, value);
        }
        
        for (const auto& [name, value] : mat3_cache_) {
            shader_->SetUniform(device, name, value);
        }
    }
    
    void Unbind(RenderDevice* device) const override {
        (void)device;  // Typically don't need to do anything here
    }
    
    // Uniform setters
    void SetUniform1f(const std::string& name, float value) override {
        uniform_cache_[name] = glm::vec4(value, 0, 0, 0);
    }
    
    void SetUniform2f(const std::string& name, float x, float y) override {
        uniform_cache_[name] = glm::vec4(x, y, 0, 0);
    }
    
    void SetUniform3f(const std::string& name, float x, float y, float z) override {
        uniform_cache_[name] = glm::vec4(x, y, z, 0);
    }
    
    void SetUniform4f(const std::string& name, float x, float y, float z, float w) override {
        uniform_cache_[name] = glm::vec4(x, y, z, w);
    }
    
    void SetUniform1i(const std::string& name, int value) override {
        int_uniform_cache_[name] = glm::ivec4(value);
    }
    
    void SetUniform2i(const std::string& name, int x, int y) override {
        int_uniform_cache_[name] = glm::ivec4(x, y, 0, 0);
    }
    
    void SetUniform3i(const std::string& name, int x, int y, int z) override {
        int_uniform_cache_[name] = glm::ivec4(x, y, z, 0);
    }
    
    void SetUniform4i(const std::string& name, int x, int y, int z, int w) override {
        int_uniform_cache_[name] = glm::ivec4(x, y, z, w);
    }
    
    void SetUniformVec2(const std::string& name, const glm::vec2& value) override {
        uniform_cache_[name] = glm::vec4(value.x, value.y, 0, 0);
    }
    
    void SetUniformVec3(const std::string& name, const glm::vec3& value) override {
        uniform_cache_[name] = glm::vec4(value.x, value.y, value.z, 0);
    }
    
    void SetUniformVec4(const std::string& name, const glm::vec4& value) override {
        uniform_cache_[name] = value;
    }
    
    void SetUniformMat4(const std::string& name, const glm::mat4& value) override {
        mat4_cache_[name] = value;
    }
    
    void SetUniformMat3(const std::string& name, const glm::mat3& value) override {
        mat3_cache_[name] = value;
    }
    
    void SetTexture(const std::string& name, std::shared_ptr<Texture> texture, uint32_t unit = 0) override {
        if (!texture) {
            spdlog::warn("Material::SetTexture - Null texture for uniform '{}'", name);
            return;
        }
        
        texture_bindings_[name] = {texture, unit};
        spdlog::debug("Material::SetTexture - Bound texture to '{}'", name);
    }
    
    std::shared_ptr<Texture> GetTexture(const std::string& name) const override {
        auto it = texture_bindings_.find(name);
        if (it != texture_bindings_.end()) {
            return it->second.first;
        }
        return nullptr;
    }
    
    void SetTextureSlot(TextureSlot slot, std::shared_ptr<Texture2D> texture) override {
        if (!texture) {
            spdlog::warn("Material::SetTextureSlot - Null texture for slot {}", static_cast<int>(slot));
            return;
        }
        
        texture_slots_[static_cast<uint8_t>(slot)] = texture;
        
        // Map to standard uniform names based on slot
        const char* slot_names[] = {
            "uAlbedo", "uNormal", "uMetallic", "uRoughness",
            "uAO", "uEmissive", "uHeight", "uCavity"
        };
        
        if (static_cast<uint8_t>(slot) < 8) {
            SetTexture(slot_names[static_cast<uint8_t>(slot)], texture, static_cast<uint32_t>(slot));
        }
    }
    
    std::shared_ptr<Texture2D> GetTextureSlot(TextureSlot slot) const override {
        auto idx = static_cast<uint8_t>(slot);
        if (idx < 8) {
            return texture_slots_[idx];
        }
        return nullptr;
    }

protected:
    std::shared_ptr<ShaderProgram> shader_;
    std::string name_;
    MaterialType material_type_;
    
    // Uniform caches
    std::unordered_map<std::string, glm::vec4> uniform_cache_;
    std::unordered_map<std::string, glm::ivec4> int_uniform_cache_;
    std::unordered_map<std::string, glm::mat4> mat4_cache_;
    std::unordered_map<std::string, glm::mat3> mat3_cache_;
    
    // Texture bindings: uniform name -> (texture, unit)
    std::unordered_map<std::string, std::pair<std::shared_ptr<Texture>, uint32_t>> texture_bindings_;
    
    // Texture slots for PBR workflow
    std::array<std::shared_ptr<Texture2D>, 8> texture_slots_ = {};
};

// ============================================================================
// OpenGL PBR Material Implementation
// ============================================================================

class OpenGLPBRMaterial : public PBRMaterial {
public:
    explicit OpenGLPBRMaterial(RenderDevice* device, const std::string& name = "PBRMaterial")
        : device_(device), name_(name), 
          albedo_(1.0f), metallic_(0.0f), roughness_(0.5f), 
          ao_strength_(1.0f), normal_strength_(1.0f),
          emissive_(0.0f), emissive_intensity_(0.0f),
          use_metallic_roughness_(true),
          material_type_(MaterialType::StandardPBR) {
        
        // Create PBR shader
        shader_ = ShaderProgram::CreatePBRShader(device);
        if (!shader_) {
            spdlog::warn("OpenGLPBRMaterial - Failed to create PBR shader, using basic shader");
            shader_ = ShaderProgram::CreateBasicShader(device);
        }
        
        // Enable all texture slots by default
        texture_slots_enabled_.fill(false);
        texture_slots_enabled_[static_cast<uint8_t>(TextureSlot::Albedo)] = true;
    }
    
    ~OpenGLPBRMaterial() override = default;
    
    MaterialType GetType() const override {
        return material_type_;
    }
    
    std::shared_ptr<ShaderProgram> GetShader() const override {
        return shader_;
    }
    
    std::string GetName() const override {
        return name_;
    }
    
    void SetName(const std::string& name) override {
        name_ = name;
    }
    
    void Bind(RenderDevice* device) const override {
        if (!device || !shader_) {
            spdlog::warn("PBRMaterial::Bind - Invalid device or shader");
            return;
        }
        
        shader_->Use(device);
        
        // Set PBR parameters
        shader_->SetUniform(device, "uAlbedo", albedo_);
        shader_->SetUniform(device, "uMetallic", metallic_);
        shader_->SetUniform(device, "uRoughness", roughness_);
        shader_->SetUniform(device, "uAOStrength", ao_strength_);
        shader_->SetUniform(device, "uNormalStrength", normal_strength_);
        shader_->SetUniform(device, "uEmissive", emissive_);
        shader_->SetUniform(device, "uEmissiveIntensity", emissive_intensity_);
        
        // Bind textures
        for (uint8_t i = 0; i < 8; ++i) {
            if (texture_slots_[i] && texture_slots_enabled_[i]) {
                texture_slots_[i]->Bind(device, i);
            }
        }
        
        // Upload additional uniforms
        for (const auto& [name, value] : uniform_cache_) {
            shader_->SetUniform(device, name, value);
        }
    }
    
    void Unbind(RenderDevice* device) const override {
        (void)device;
    }
    
    void SetUniform1f(const std::string& name, float value) override {
        uniform_cache_[name] = glm::vec4(value, 0, 0, 0);
    }
    
    void SetUniform2f(const std::string& name, float x, float y) override {
        uniform_cache_[name] = glm::vec4(x, y, 0, 0);
    }
    
    void SetUniform3f(const std::string& name, float x, float y, float z) override {
        uniform_cache_[name] = glm::vec4(x, y, z, 0);
    }
    
    void SetUniform4f(const std::string& name, float x, float y, float z, float w) override {
        uniform_cache_[name] = glm::vec4(x, y, z, w);
    }
    
    void SetUniform1i(const std::string& name, int value) override {
        // PBR materials typically don't use integer uniforms
    }
    
    void SetUniform2i(const std::string& name, int x, int y) override {
        // PBR materials typically don't use integer uniforms
    }
    
    void SetUniform3i(const std::string& name, int x, int y, int z) override {
        // PBR materials typically don't use integer uniforms
    }
    
    void SetUniform4i(const std::string& name, int x, int y, int z, int w) override {
        // PBR materials typically don't use integer uniforms
    }
    
    void SetUniformVec2(const std::string& name, const glm::vec2& value) override {
        uniform_cache_[name] = glm::vec4(value.x, value.y, 0, 0);
    }
    
    void SetUniformVec3(const std::string& name, const glm::vec3& value) override {
        uniform_cache_[name] = glm::vec4(value.x, value.y, value.z, 0);
    }
    
    void SetUniformVec4(const std::string& name, const glm::vec4& value) override {
        uniform_cache_[name] = value;
    }
    
    void SetUniformMat4(const std::string& name, const glm::mat4& value) override {
        // Not typically used in PBR materials
    }
    
    void SetUniformMat3(const std::string& name, const glm::mat3& value) override {
        // Not typically used in PBR materials
    }
    
    void SetTexture(const std::string& name, std::shared_ptr<Texture> texture, uint32_t unit = 0) override {
        if (!texture) {
            spdlog::warn("PBRMaterial::SetTexture - Null texture for uniform '{}'", name);
            return;
        }
        
        texture_bindings_[name] = {texture, unit};
    }
    
    std::shared_ptr<Texture> GetTexture(const std::string& name) const override {
        auto it = texture_bindings_.find(name);
        if (it != texture_bindings_.end()) {
            return it->second.first;
        }
        return nullptr;
    }
    
    void SetTextureSlot(TextureSlot slot, std::shared_ptr<Texture2D> texture) override {
        if (!texture) {
            spdlog::warn("PBRMaterial::SetTextureSlot - Null texture for slot {}", static_cast<int>(slot));
            return;
        }
        
        auto idx = static_cast<uint8_t>(slot);
        if (idx < 8) {
            texture_slots_[idx] = texture;
            texture_slots_enabled_[idx] = true;
        }
    }
    
    std::shared_ptr<Texture2D> GetTextureSlot(TextureSlot slot) const override {
        auto idx = static_cast<uint8_t>(slot);
        if (idx < 8) {
            return texture_slots_[idx];
        }
        return nullptr;
    }
    
    void SetAlbedo(const glm::vec3& color) override {
        albedo_ = color;
    }
    
    glm::vec3 GetAlbedo() const override {
        return albedo_;
    }
    
    void SetMetallic(float value) override {
        metallic_ = glm::clamp(value, 0.0f, 1.0f);
    }
    
    float GetMetallic() const override {
        return metallic_;
    }
    
    void SetRoughness(float value) override {
        roughness_ = glm::clamp(value, 0.0f, 1.0f);
    }
    
    float GetRoughness() const override {
        return roughness_;
    }
    
    void SetAOStrength(float value) override {
        ao_strength_ = glm::clamp(value, 0.0f, 1.0f);
    }
    
    float GetAOStrength() const override {
        return ao_strength_;
    }
    
    void SetNormalStrength(float value) override {
        normal_strength_ = glm::clamp(value, 0.0f, 2.0f);
    }
    
    float GetNormalStrength() const override {
        return normal_strength_;
    }
    
    void SetEmissive(const glm::vec3& color, float intensity = 1.0f) override {
        emissive_ = color;
        emissive_intensity_ = glm::max(0.0f, intensity);
    }
    
    glm::vec3 GetEmissive() const override {
        return emissive_;
    }
    
    float GetEmissiveIntensity() const override {
        return emissive_intensity_;
    }
    
    void SetMetallicRoughnessWorkflow(bool enabled) override {
        use_metallic_roughness_ = enabled;
    }
    
    bool IsMetallicRoughnessWorkflow() const override {
        return use_metallic_roughness_;
    }
    
    void EnableTextureSlot(TextureSlot slot, bool enabled) override {
        auto idx = static_cast<uint8_t>(slot);
        if (idx < 8) {
            texture_slots_enabled_[idx] = enabled;
        }
    }
    
    bool IsTextureSlotEnabled(TextureSlot slot) const override {
        auto idx = static_cast<uint8_t>(slot);
        if (idx < 8) {
            return texture_slots_enabled_[idx];
        }
        return false;
    }
    
    glm::vec3 GetDiffuseColor() const override {
        // In PBR, diffuse is the non-metallic part of albedo
        return albedo_ * (1.0f - metallic_);
    }
    
    glm::vec3 GetF0() const override {
        // Fresnel at zero degrees
        // For dielectrics: 0.04, for metals: albedo
        if (metallic_ > 0.5f) {
            return glm::mix(glm::vec3(0.04f), albedo_, metallic_);
        }
        return glm::vec3(0.04f);
    }

private:
    RenderDevice* device_;
    std::shared_ptr<ShaderProgram> shader_;
    std::string name_;
    MaterialType material_type_;
    
    // PBR parameters
    glm::vec3 albedo_;
    float metallic_;
    float roughness_;
    float ao_strength_;
    float normal_strength_;
    glm::vec3 emissive_;
    float emissive_intensity_;
    bool use_metallic_roughness_;
    
    // Texture slots and their enabled states
    std::array<std::shared_ptr<Texture2D>, 8> texture_slots_ = {};
    std::array<bool, 8> texture_slots_enabled_ = {};
    
    // Additional uniforms cache
    std::unordered_map<std::string, glm::vec4> uniform_cache_;
    
    // Legacy texture bindings
    std::unordered_map<std::string, std::pair<std::shared_ptr<Texture>, uint32_t>> texture_bindings_;
};


// ============================================================================
// Factory Functions
// ============================================================================

std::unique_ptr<Material> Material::Create(std::shared_ptr<ShaderProgram> shader) {
    if (!shader) {
        spdlog::error("Material::Create - Null shader");
        return nullptr;
    }
    
    return std::make_unique<OpenGLMaterial>(shader);
}

std::unique_ptr<Material> Material::CreateSolidColor(RenderDevice* device, const glm::vec4& color) {
    if (!device) {
        spdlog::error("Material::CreateSolidColor - No render device");
        return nullptr;
    }
    
    auto shader = ShaderProgram::CreateSolidShader(device);
    if (!shader) {
        spdlog::error("Material::CreateSolidColor - Failed to create shader");
        return nullptr;
    }
    
    auto material = Material::Create(shader);
    if (material) {
        material->SetUniform4f("uColor", color.r, color.g, color.b, color.a);
        spdlog::info("Material::CreateSolidColor - Created solid color material");
    }
    
    return material;
}

std::unique_ptr<Material> Material::CreateTextured(RenderDevice* device, std::shared_ptr<Texture2D> texture) {
    if (!device) {
        spdlog::error("Material::CreateTextured - No render device");
        return nullptr;
    }
    
    if (!texture) {
        spdlog::error("Material::CreateTextured - Null texture");
        return nullptr;
    }
    
    auto shader = ShaderProgram::CreateBasicShader(device);
    if (!shader) {
        spdlog::error("Material::CreateTextured - Failed to create shader");
        return nullptr;
    }
    
    auto material = Material::Create(shader);
    if (material) {
        material->SetTexture("uTexture", texture, 0);
        spdlog::info("Material::CreateTextured - Created textured material");
    }
    
    return material;
}

std::unique_ptr<Material> Material::CreatePBR(RenderDevice* device,
                                               std::shared_ptr<Texture2D> albedo,
                                               std::shared_ptr<Texture2D> normal,
                                               std::shared_ptr<Texture2D> metallic,
                                               std::shared_ptr<Texture2D> roughness,
                                               std::shared_ptr<Texture2D> ao) {
    if (!device) {
        spdlog::error("Material::CreatePBR - No render device");
        return nullptr;
    }
    
    // Check textures
    if (!albedo || !normal || !metallic || !roughness || !ao) {
        spdlog::error("Material::CreatePBR - Missing required textures");
        return nullptr;
    }
    
    auto material = PBRMaterial::Create(device, "StandardPBR");
    if (material) {
        material->SetTextureSlot(TextureSlot::Albedo, albedo);
        material->SetTextureSlot(TextureSlot::Normal, normal);
        material->SetTextureSlot(TextureSlot::Metallic, metallic);
        material->SetTextureSlot(TextureSlot::Roughness, roughness);
        material->SetTextureSlot(TextureSlot::AmbientOcclusion, ao);
        
        spdlog::info("Material::CreatePBR - Created full PBR material with all textures");
    }
    
    return material;
}

std::unique_ptr<Material> Material::CreateSimplePBR(RenderDevice* device,
                                                    std::shared_ptr<Texture2D> albedo,
                                                    std::shared_ptr<Texture2D> normal) {
    if (!device) {
        spdlog::error("Material::CreateSimplePBR - No render device");
        return nullptr;
    }
    
    if (!albedo) {
        spdlog::error("Material::CreateSimplePBR - Missing albedo texture");
        return nullptr;
    }
    
    auto material = PBRMaterial::Create(device, "SimplePBR");
    if (material) {
        material->SetTextureSlot(TextureSlot::Albedo, albedo);
        if (normal) {
            material->SetTextureSlot(TextureSlot::Normal, normal);
        }
        
        // Set default PBR parameters
        material->SetMetallic(0.0f);
        material->SetRoughness(0.5f);
        
        spdlog::info("Material::CreateSimplePBR - Created simple PBR material");
    }
    
    return material;
}

std::unique_ptr<Material> Material::CreateParamPBR(RenderDevice* device,
                                                   const glm::vec3& albedo,
                                                   float metallic,
                                                   float roughness) {
    if (!device) {
        spdlog::error("Material::CreateParamPBR - No render device");
        return nullptr;
    }
    
    auto material = PBRMaterial::Create(device, "ParamPBR");
    if (material) {
        material->SetAlbedo(albedo);
        material->SetMetallic(metallic);
        material->SetRoughness(roughness);
        
        spdlog::info("Material::CreateParamPBR - Created parametric PBR material");
    }
    
    return material;
}

std::unique_ptr<Material> Material::CreateEmissive(RenderDevice* device,
                                                  const glm::vec3& emissive_color,
                                                  std::shared_ptr<Texture2D> emission_texture) {
    if (!device) {
        spdlog::error("Material::CreateEmissive - No render device");
        return nullptr;
    }
    
    auto material = PBRMaterial::Create(device, "Emissive");
    if (material) {
        material->SetEmissive(emissive_color, 1.0f);
        material->SetAlbedo(glm::vec3(0.0f));  // No albedo, fully emissive
        material->SetMetallic(0.0f);
        material->SetRoughness(1.0f);
        
        if (emission_texture) {
            material->SetTextureSlot(TextureSlot::Emissive, emission_texture);
        }
        
        spdlog::info("Material::CreateEmissive - Created emissive material");
    }
    
    return material;
}

// ============================================================================
// PBRMaterial Factory Functions
// ============================================================================

std::unique_ptr<PBRMaterial> PBRMaterial::Create(RenderDevice* device, const std::string& name) {
    if (!device) {
        spdlog::error("PBRMaterial::Create - No render device");
        return nullptr;
    }
    
    return std::make_unique<OpenGLPBRMaterial>(device, name);
}

} // namespace schizo::renderer
