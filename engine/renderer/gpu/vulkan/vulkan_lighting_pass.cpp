/**
 * @file vulkan_lighting_pass.cpp
 * @brief Vulkan lighting pass implementation
 */

#include "vulkan_lighting_pass.h"
#include "vulkan_device.h"
#include "vulkan_g_buffer.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <algorithm>

namespace gws::renderer::gpu {

VulkanLightingPass::~VulkanLightingPass() {
    cleanup();
}

std::unique_ptr<VulkanLightingPass> VulkanLightingPass::create(VulkanDevice* device,
                                                               const LightingConfig& config,
                                                               VulkanGBuffer* gbuffer) {
    auto pass = std::make_unique<VulkanLightingPass>();
    pass->device_ = device;
    pass->gbuffer_ = gbuffer;
    pass->config_ = config;
    
    try {
        pass->create_light_buffer();
        pass->create_descriptor_sets();
        pass->create_output_image(gbuffer->get_width(), gbuffer->get_height());
        pass->create_pipeline();
        
        spdlog::info("VulkanLightingPass created with {} max lights", config.max_lights);
        return pass;
    } catch (const std::exception& e) {
        spdlog::error("Failed to create VulkanLightingPass: {}", e.what());
        pass->cleanup();
        return nullptr;
    }
}

void VulkanLightingPass::create_light_buffer() {
    VkDevice vk_device = device_->get_device();
    VkPhysicalDevice physical_device = device_->get_physical_device();
    
    VkDeviceSize buffer_size = sizeof(Light) * config_.max_lights;
    
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(vk_device, &buffer_info, nullptr, &light_buffer_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create light buffer");
    }
    
    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(vk_device, light_buffer_, &mem_reqs);
    
    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = device_->find_memory_type(
        mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(vk_device, &alloc_info, nullptr, &light_buffer_memory_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate light buffer memory");
    }
    
    vkBindBufferMemory(vk_device, light_buffer_, light_buffer_memory_, 0);
}

void VulkanLightingPass::create_descriptor_sets() {
    VkDevice vk_device = device_->get_device();
    
    // Create descriptor set layout
    std::vector<VkDescriptorSetLayoutBinding> bindings(6);
    
    // G-Buffer position sampler
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // G-Buffer normal sampler
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // G-Buffer albedo sampler
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // G-Buffer material sampler
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // G-Buffer depth sampler
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Light buffer
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
    layout_info.pBindings = bindings.data();
    
    if (vkCreateDescriptorSetLayout(vk_device, &layout_info, nullptr, &descriptor_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout");
    }
    
    // Create descriptor pool
    std::vector<VkDescriptorPoolSize> pool_sizes;
    pool_sizes.push_back({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5});
    pool_sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1});
    
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes = pool_sizes.data();
    pool_info.maxSets = 1;
    
    if (vkCreateDescriptorPool(vk_device, &pool_info, nullptr, &descriptor_pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool");
    }
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = descriptor_pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &descriptor_layout_;
    
    if (vkAllocateDescriptorSets(vk_device, &alloc_info, &descriptor_set_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor set");
    }
}

void VulkanLightingPass::create_output_image(uint32_t width, uint32_t height) {
    VkDevice vk_device = device_->get_device();
    VkPhysicalDevice physical_device = device_->get_physical_device();
    
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateImage(vk_device, &image_info, nullptr, &output_image_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create output image");
    }
    
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(vk_device, output_image_, &mem_reqs);
    
    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = device_->find_memory_type(
        mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(vk_device, &alloc_info, nullptr, &output_memory_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate output image memory");
    }
    
    vkBindImageMemory(vk_device, output_image_, output_memory_, 0);
    
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = output_image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(vk_device, &view_info, nullptr, &output_view_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create output image view");
    }
}

void VulkanLightingPass::create_pipeline() {
    // Placeholder for pipeline creation
    // Will be implemented when shader compilation system is complete
    spdlog::debug("Lighting pipeline creation placeholder");
}

void VulkanLightingPass::add_light(const Light& light) {
    if (light_count_ < config_.max_lights) {
        lights_.push_back(light);
        light_count_++;
    } else {
        spdlog::warn("Light limit reached, cannot add more lights");
    }
}

void VulkanLightingPass::update_light(uint32_t index, const Light& light) {
    if (index < light_count_) {
        lights_[index] = light;
    }
}

void VulkanLightingPass::remove_light(uint32_t index) {
    if (index < light_count_) {
        lights_.erase(lights_.begin() + index);
        light_count_--;
    }
}

void VulkanLightingPass::clear_lights() {
    lights_.clear();
    light_count_ = 0;
}

const Light& VulkanLightingPass::get_light(uint32_t index) const {
    if (index >= light_count_) {
        throw std::out_of_range("Light index out of range");
    }
    return lights_[index];
}

void VulkanLightingPass::begin_pass(VkCommandBuffer cmd, uint32_t width, uint32_t height) {
    // Transition output image to color attachment
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = output_image_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE, VK_PIPELINE_STAGE_COLOR_OUTPUT_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VulkanLightingPass::end_pass(VkCommandBuffer cmd) {
    // Transition output image to shader read
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = output_image_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VulkanLightingPass::render(VkCommandBuffer cmd) {
    if (pipeline_ == VK_NULL_HANDLE) {
        spdlog::warn("Lighting pass pipeline not initialized");
        return;
    }
    
    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    
    // Bind descriptor set
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_,
                           0, 1, &descriptor_set_, 0, nullptr);
    
    // Draw full-screen quad
    vkCmdDraw(cmd, 6, 1, 0, 0);
}

void VulkanLightingPass::set_ambient_light(float intensity) {
    config_.global_ambient = intensity;
}

void VulkanLightingPass::resize(uint32_t width, uint32_t height) {
    // Will be implemented when output image resizing is needed
}

void VulkanLightingPass::cleanup() {
    if (!device_) return;
    
    VkDevice vk_device = device_->get_device();
    
    if (framebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(vk_device, framebuffer_, nullptr);
        framebuffer_ = VK_NULL_HANDLE;
    }
    
    if (render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vk_device, render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }
    
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(vk_device, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    
    if (pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vk_device, pipeline_layout_, nullptr);
        pipeline_layout_ = VK_NULL_HANDLE;
    }
    
    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vk_device, descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
    }
    
    if (descriptor_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vk_device, descriptor_layout_, nullptr);
        descriptor_layout_ = VK_NULL_HANDLE;
    }
    
    if (output_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vk_device, output_view_, nullptr);
        output_view_ = VK_NULL_HANDLE;
    }
    
    if (output_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(vk_device, output_image_, nullptr);
        output_image_ = VK_NULL_HANDLE;
    }
    
    if (output_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, output_memory_, nullptr);
        output_memory_ = VK_NULL_HANDLE;
    }
    
    if (light_buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(vk_device, light_buffer_, nullptr);
        light_buffer_ = VK_NULL_HANDLE;
    }
    
    if (light_buffer_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, light_buffer_memory_, nullptr);
        light_buffer_memory_ = VK_NULL_HANDLE;
    }
}

} // namespace gws::renderer::gpu
