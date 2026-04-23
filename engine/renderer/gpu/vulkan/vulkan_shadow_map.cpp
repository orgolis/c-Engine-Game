/**
 * @file vulkan_shadow_map.cpp
 * @brief Vulkan shadow map implementation
 */

#include "vulkan_shadow_map.h"
#include "vulkan_device.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace gws::renderer::gpu {

VulkanShadowMap::~VulkanShadowMap() {
    cleanup();
}

std::unique_ptr<VulkanShadowMap> VulkanShadowMap::create(VulkanDevice* device, 
                                                         const ShadowMapConfig& config) {
    auto shadow_map = std::make_unique<VulkanShadowMap>();
    shadow_map->device_ = device;
    shadow_map->config_ = config;
    
    try {
        shadow_map->create_shadow_image();
        shadow_map->create_shadow_sampler();
        shadow_map->create_render_pass();
        shadow_map->create_framebuffer();
        
        if (config.type == ShadowMapType::Cascaded2D) {
            shadow_map->create_cascade_matrices();
        }
        
        spdlog::info("VulkanShadowMap created: {}x{}, type={}", 
                    config.width, config.height, static_cast<int>(config.type));
        return shadow_map;
    } catch (const std::exception& e) {
        spdlog::error("Failed to create VulkanShadowMap: {}", e.what());
        shadow_map->cleanup();
        return nullptr;
    }
}

void VulkanShadowMap::create_shadow_image() {
    VkDevice vk_device = device_->get_device();
    VkPhysicalDevice physical_device = device_->get_physical_device();
    
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_D32_SFLOAT;
    image_info.extent = {config_.width, config_.height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = config_.type == ShadowMapType::Cascaded2D ? config_.cascade_count : 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (config_.type == ShadowMapType::Cubemap) {
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.arrayLayers = 6;
        image_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }
    
    if (vkCreateImage(vk_device, &image_info, nullptr, &shadow_image_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow image");
    }
    
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(vk_device, shadow_image_, &mem_reqs);
    
    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = device_->find_memory_type(
        mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(vk_device, &alloc_info, nullptr, &shadow_memory_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate shadow image memory");
    }
    
    vkBindImageMemory(vk_device, shadow_image_, shadow_memory_, 0);
    
    // Create image view
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = shadow_image_;
    view_info.viewType = config_.type == ShadowMapType::Cascaded2D ? 
        VK_IMAGE_VIEW_TYPE_2D_ARRAY : 
        (config_.type == ShadowMapType::Cubemap ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D);
    view_info.format = VK_FORMAT_D32_SFLOAT;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = config_.type == ShadowMapType::Cascaded2D ? 
        config_.cascade_count : (config_.type == ShadowMapType::Cubemap ? 6 : 1);
    
    if (vkCreateImageView(vk_device, &view_info, nullptr, &shadow_view_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow image view");
    }
}

void VulkanShadowMap::create_shadow_sampler() {
    VkDevice vk_device = device_->get_device();
    
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.anisotropyEnable = VK_TRUE;
    sampler_info.maxAnisotropy = 1.0f;
    sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable = config_.use_pcf ? VK_TRUE : VK_FALSE;
    sampler_info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.mipLodBias = 0.0f;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = 1.0f;
    
    if (vkCreateSampler(vk_device, &sampler_info, nullptr, &shadow_sampler_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow sampler");
    }
}

void VulkanShadowMap::create_render_pass() {
    VkDevice vk_device = device_->get_device();
    
    VkAttachmentDescription attachment{};
    attachment.format = VK_FORMAT_D32_SFLOAT;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    
    VkAttachmentReference depth_ref{};
    depth_ref.attachment = 0;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depth_ref;
    
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE;
    dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dependencyFlags = 0;
    
    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;
    
    if (vkCreateRenderPass(vk_device, &render_pass_info, nullptr, &render_pass_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow render pass");
    }
}

void VulkanShadowMap::create_framebuffer() {
    VkDevice vk_device = device_->get_device();
    
    // Create depth view for framebuffer
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = shadow_image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_D32_SFLOAT;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;
    
    VkImageView depth_view;
    if (vkCreateImageView(vk_device, &view_info, nullptr, &depth_view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth view for shadow framebuffer");
    }
    
    VkFramebufferCreateInfo framebuffer_info{};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = render_pass_;
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.pAttachments = &depth_view;
    framebuffer_info.width = config_.width;
    framebuffer_info.height = config_.height;
    framebuffer_info.layers = 1;
    
    if (vkCreateFramebuffer(vk_device, &framebuffer_info, nullptr, &framebuffer_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow framebuffer");
    }
    
    vkDestroyImageView(vk_device, depth_view, nullptr);
}

void VulkanShadowMap::create_cascade_matrices() {
    cascade_view_matrices_.resize(config_.cascade_count);
    cascade_proj_matrices_.resize(config_.cascade_count);
    cascade_splits_.resize(config_.cascade_count);
    
    // Initialize cascade splits (0.0 to 1.0)
    for (uint32_t i = 0; i < config_.cascade_count; ++i) {
        float split = (static_cast<float>(i) + 1.0f) / config_.cascade_count;
        cascade_splits_[i] = split;
    }
}

void VulkanShadowMap::begin_directional_pass(VkCommandBuffer cmd, uint32_t cascade_index) {
    if (cascade_index >= config_.cascade_count) {
        spdlog::warn("Cascade index {} out of range", cascade_index);
        return;
    }
    
    VkRenderPassBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin_info.renderPass = render_pass_;
    begin_info.framebuffer = framebuffer_;
    begin_info.renderArea.offset = {0, 0};
    begin_info.renderArea.extent = {config_.width, config_.height};
    
    VkClearValue clear_value;
    clear_value.depthStencil = {1.0f, 0};
    
    begin_info.clearValueCount = 1;
    begin_info.pClearValues = &clear_value;
    
    vkCmdBeginRenderPass(cmd, &begin_info, VK_SUBPASS_INLINE);
}

void VulkanShadowMap::end_pass(VkCommandBuffer cmd) {
    vkCmdEndRenderPass(cmd);
}

void VulkanShadowMap::begin_cubemap_pass(VkCommandBuffer cmd, uint32_t face_index) {
    begin_directional_pass(cmd, face_index);
}

void VulkanShadowMap::begin_spot_pass(VkCommandBuffer cmd) {
    begin_directional_pass(cmd, 0);
}

glm::mat4 VulkanShadowMap::get_light_vp_matrix(uint32_t cascade_index) const {
    if (cascade_index >= config_.cascade_count) {
        return glm::mat4(1.0f);
    }
    return cascade_proj_matrices_[cascade_index] * cascade_view_matrices_[cascade_index];
}

void VulkanShadowMap::update_cascade_splits(const std::vector<float>& splits) {
    if (splits.size() != config_.cascade_count) {
        spdlog::warn("Cascade splits count mismatch");
        return;
    }
    cascade_splits_ = splits;
}

void VulkanShadowMap::cleanup() {
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
    
    if (shadow_sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(vk_device, shadow_sampler_, nullptr);
        shadow_sampler_ = VK_NULL_HANDLE;
    }
    
    if (shadow_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vk_device, shadow_view_, nullptr);
        shadow_view_ = VK_NULL_HANDLE;
    }
    
    if (shadow_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(vk_device, shadow_image_, nullptr);
        shadow_image_ = VK_NULL_HANDLE;
    }
    
    if (shadow_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, shadow_memory_, nullptr);
        shadow_memory_ = VK_NULL_HANDLE;
    }
}

} // namespace gws::renderer::gpu
