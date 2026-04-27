/**
 * @file material.h
 * @brief Vulkan Material System - shader + uniforms + texture bindings
 * 
 * High-level material abstraction combining shaders, textures, and uniform buffers
 * with descriptor set management via Phase 1 descriptor allocator.
 */

#pragma once

#include "buffer.h"
#include "image.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <array>
#include <unordered_map>
#include <string>
#include <memory>

namespace vks {

// Forward declarations
class VulkanShader;

/**
 * @struct MaterialData
 * @brief Standard PBR material uniform data
 */
struct MaterialData {
    glm::vec4 albedo_color = {1.0f, 1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.8f;
    float ambient_occlusion = 1.0f;
    float _pad0 = 0.0f;
    glm::vec3 emission = {0.0f, 0.0f, 0.0f};
    float _pad1 = 0.0f;
};

/**
 * @enum TextureSlot
 * @brief Standard texture bindings in descriptor set
 */
enum class TextureSlot : uint32_t {
    ALBEDO = 0,
    NORMAL = 1,
    METALLIC = 2,
    ROUGHNESS = 3,
    AO = 4,
    EMISSION = 5,
    HEIGHT = 6,
    SLOT_COUNT = 7,
};

/**
 * @class Material
 * @brief GPU material with shader, textures, and uniforms
 * 
 * Manages:
 * - Shader module references
 * - Per-material uniform buffer (material parameters)
 * - Descriptor set with texture bindings
 * - Automatic layout management
 */
class Material {
public:
    Material() = default;
    ~Material() { destroy(); }
    
    /**
     * @brief Create material with shader
     * @param device Vulkan device
     * @param shader Shader to use (must be valid for material lifetime)
     * @param descriptor_layout The descriptor set layout for material bindings
     * @return Created material
     */
    static Material create(VkDevice device, 
                          const VulkanShader& shader,
                          VkDescriptorSetLayout descriptor_layout);
    
    /**
     * @brief Set material color and PBR parameters
     * @param data Material data (albedo, metallic, roughness, AO, emission)
     */
    void set_data(const MaterialData& data);
    
    /**
     * @brief Set texture for a slot
     * @param slot Which texture slot (albedo, normal, etc.)
     * @param texture Texture to bind
     */
    void set_texture(TextureSlot slot, const VulkanImage& texture);
    
    /**
     * @brief Get uniform buffer (for direct updates if needed)
     */
    const VulkanBuffer& get_uniform_buffer() const { return uniform_buffer; }
    VulkanBuffer& get_uniform_buffer() { return uniform_buffer; }
    
    /**
     * @brief Get descriptor set for shader binding
     */
    VkDescriptorSet get_descriptor_set() const { return descriptor_set; }
    
    /**
     * @brief Bind material for rendering
     * @param cmd Command buffer
     * @param pipeline_layout Pipeline layout containing descriptor set layout
     * @param set_index Descriptor set index (usually 0)
     */
    void bind(VkCommandBuffer cmd, VkPipelineLayout pipeline_layout, uint32_t set_index = 0) const;
    
    /**
     * @brief Get shader
     */
    const VulkanShader* get_shader() const { return shader; }
    
    /**
     * @brief Check if material is valid
     */
    bool is_valid() const { return device != VK_NULL_HANDLE && descriptor_set != VK_NULL_HANDLE; }
    
    // Move semantics
    Material(Material&& other) noexcept;
    Material& operator=(Material&& other) noexcept;
    
    // Delete copies
    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;

private:
    VkDevice device = VK_NULL_HANDLE;
    const VulkanShader* shader = nullptr;
    
    // Per-material uniform buffer
    VulkanBuffer uniform_buffer;
    
    // Descriptor set for textures + sampler
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    
    // Cached texture pointers (for debugging/inspection)
    std::array<const VulkanImage*, static_cast<size_t>(TextureSlot::SLOT_COUNT)> textures = {};
    
    void destroy();
    void update_descriptor_set();
};

/**
 * @class MaterialLibrary
 * @brief Simple material cache for reusing materials
 */
class MaterialLibrary {
public:
    MaterialLibrary(VkDevice device) : device(device) {}
    
    /**
     * @brief Get or create material
     * @param name Material name (e.g., "stone", "metal_brushed")
     * @param shader Shader to use
     * @param layout Descriptor set layout
     */
    Material& get_or_create(const std::string& name,
                           const VulkanShader& shader,
                           VkDescriptorSetLayout layout);
    
    /**
     * @brief Check if material exists
     */
    bool has(const std::string& name) const {
        return materials.find(name) != materials.end();
    }
    
    /**
     * @brief Get material by name
     */
    Material* get(const std::string& name) {
        auto it = materials.find(name);
        return it != materials.end() ? it->second.get() : nullptr;
    }

private:
    VkDevice device = VK_NULL_HANDLE;
    std::unordered_map<std::string, std::unique_ptr<Material>> materials;
};

}  // namespace vks
