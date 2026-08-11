/**
 * @file material.cpp
 * @brief Vulkan Material implementation
 */

#include "material.h"
#include "shader_compiler.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <cstdint>

namespace vks {

// ============================================================================
// Material Implementation
// ============================================================================

Material Material::create(VkDevice device,
                         const VulkanShader& shader,
                         VkDescriptorSetLayout descriptor_layout)
{
    Material mat;
    mat.device = device;
    mat.shader = &shader;
    
    if (device == VK_NULL_HANDLE) {
        throw std::runtime_error("Material::create: invalid device");
    }
    
    // Create uniform buffer for material data
    mat.uniform_buffer = VulkanBuffer::create(
        device, nullptr,  // Physical device would be passed in production
        BufferUsage::UNIFORM,
        sizeof(MaterialData),
        nullptr
    );
    
    if (!mat.uniform_buffer.is_valid()) {
        throw std::runtime_error("Material: failed to create uniform buffer");
    }
    
    // Create descriptor pool (one set per material)
    VkDescriptorPoolSize pool_sizes[2]{};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_sizes[0].descriptorCount = 1;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[1].descriptorCount = static_cast<uint32_t>(TextureSlot::SLOT_COUNT);
    
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = pool_sizes;
    pool_info.maxSets = 1;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    
    if (vkCreateDescriptorPool(device, &pool_info, nullptr, &mat.descriptor_pool) != VK_SUCCESS) {
        throw std::runtime_error("Material: failed to create descriptor pool");
    }
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = mat.descriptor_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &descriptor_layout;
    
    if (vkAllocateDescriptorSets(device, &alloc_info, &mat.descriptor_set) != VK_SUCCESS) {
        throw std::runtime_error("Material: failed to allocate descriptor set");
    }
    
    // Initialize with default white texture for all slots
    // (In production, these would be replaced by actual textures)
    VulkanImage white_texture = VulkanImage::create(device, nullptr, 1, 1,
                                                    ImageFormat::RGBA8_UNORM,
                                                    ImageUsage::TEXTURE);
    
    for (size_t i = 0; i < static_cast<size_t>(TextureSlot::SLOT_COUNT); ++i) {
        mat.textures[i] = &white_texture;  // Will be overwritten by set_texture
    }
    
    // Write uniform buffer descriptor
    VkDescriptorBufferInfo buffer_info{};
    buffer_info.buffer = mat.uniform_buffer.get();
    buffer_info.offset = 0;
    buffer_info.range = sizeof(MaterialData);
    
    VkWriteDescriptorSet write_buffer{};
    write_buffer.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_buffer.dstSet = mat.descriptor_set;
    write_buffer.dstBinding = 0;
    write_buffer.dstArrayElement = 0;
    write_buffer.descriptorCount = 1;
    write_buffer.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write_buffer.pBufferInfo = &buffer_info;
    
    // Write sampler descriptors (placeholder textures)
    std::array<VkDescriptorImageInfo, static_cast<size_t>(TextureSlot::SLOT_COUNT)> image_infos{};
    for (size_t i = 0; i < image_infos.size(); ++i) {
        image_infos[i].sampler = white_texture.get_sampler();
        image_infos[i].imageView = white_texture.get_view();
        image_infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    
    VkWriteDescriptorSet write_images{};
    write_images.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_images.dstSet = mat.descriptor_set;
    write_images.dstBinding = 1;
    write_images.dstArrayElement = 0;
    write_images.descriptorCount = static_cast<uint32_t>(image_infos.size());
    write_images.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write_images.pImageInfo = image_infos.data();
    
    vkUpdateDescriptorSets(device, 1, &write_buffer, 0, nullptr);
    vkUpdateDescriptorSets(device, 1, &write_images, 0, nullptr);
    
    spdlog::debug("✓ Material created");
    return mat;
}

void Material::set_data(const MaterialData& data) {
    if (!uniform_buffer.is_valid()) {
        spdlog::warn("Material::set_data: invalid uniform buffer");
        return;
    }
    
    uniform_buffer.update(&data, sizeof(MaterialData), 0);
}

void Material::set_texture(TextureSlot slot, const VulkanImage& texture) {
    if (texture.get_view() == VK_NULL_HANDLE) {
        spdlog::warn("Material::set_texture: invalid texture");
        return;
    }
    
    uint32_t slot_idx = static_cast<uint32_t>(slot);
    if (slot_idx >= textures.size()) {
        spdlog::warn("Material::set_texture: invalid slot {}", slot_idx);
        return;
    }
    
    textures[slot_idx] = &texture;
    
    // Update descriptor
    VkDescriptorImageInfo image_info{};
    image_info.sampler = texture.get_sampler();
    image_info.imageView = texture.get_view();
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptor_set;
    write.dstBinding = 1;  // Texture binding
    write.dstArrayElement = slot_idx;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image_info;
    
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void Material::bind(VkCommandBuffer cmd, VkPipelineLayout pipeline_layout, uint32_t set_index) const {
    if (descriptor_set == VK_NULL_HANDLE || cmd == VK_NULL_HANDLE) {
        return;
    }
    
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           pipeline_layout, set_index, 1, &descriptor_set, 0, nullptr);
}

Material::Material(Material&& other) noexcept
    : device(other.device), shader(other.shader),
      uniform_buffer(std::move(other.uniform_buffer)),
      descriptor_set(other.descriptor_set),
      descriptor_pool(other.descriptor_pool),
      textures(other.textures) {
    other.device = VK_NULL_HANDLE;
    other.shader = nullptr;
    other.descriptor_set = VK_NULL_HANDLE;
    other.descriptor_pool = VK_NULL_HANDLE;
}

Material& Material::operator=(Material&& other) noexcept {
    destroy();
    device = other.device;
    shader = other.shader;
    uniform_buffer = std::move(other.uniform_buffer);
    descriptor_set = other.descriptor_set;
    descriptor_pool = other.descriptor_pool;
    textures = other.textures;
    
    other.device = VK_NULL_HANDLE;
    other.shader = nullptr;
    other.descriptor_set = VK_NULL_HANDLE;
    other.descriptor_pool = VK_NULL_HANDLE;
    
    return *this;
}

void Material::destroy() {
    if (device == VK_NULL_HANDLE) return;
    
    if (descriptor_set != VK_NULL_HANDLE && descriptor_pool != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device, descriptor_pool, 1, &descriptor_set);
        descriptor_set = VK_NULL_HANDLE;
    }
    
    if (descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        descriptor_pool = VK_NULL_HANDLE;
    }
    
    device = VK_NULL_HANDLE;
    shader = nullptr;
}

// ============================================================================
// MaterialLibrary Implementation
// ============================================================================

Material& MaterialLibrary::get_or_create(const std::string& name,
                                        const VulkanShader& shader,
                                        VkDescriptorSetLayout layout) {
    auto it = materials.find(name);
    if (it != materials.end()) {
        return *it->second;
    }
    
    auto mat = std::make_unique<Material>(Material::create(device, shader, layout));
    materials[name] = std::move(mat);
    
    return *materials[name];
}

}  // namespace vks
