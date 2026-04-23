/**
 * @file vulkan_g_buffer.cpp
 * @brief Vulkan G-Buffer implementation
 */

#include "vulkan_g_buffer.h"
#include "vulkan_device.h"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace gws::renderer::gpu {

VulkanGBuffer::~VulkanGBuffer() {
    cleanup();
}

std::unique_ptr<VulkanGBuffer> VulkanGBuffer::create(VulkanDevice* device, 
                                                     const GBufferConfig& config) {
    auto gbuffer = std::make_unique<VulkanGBuffer>();
    gbuffer->device_ = device;
    gbuffer->config_ = config;
    
    VkDevice vk_device = device->get_device();
    VkPhysicalDevice physical_device = device->get_physical_device();
    
    try {
        // Create position image (RGBA16F)
        VkImageCreateInfo pos_info{};
        pos_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        pos_info.imageType = VK_IMAGE_TYPE_2D;
        pos_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        pos_info.extent = {config.width, config.height, 1};
        pos_info.mipLevels = 1;
        pos_info.arrayLayers = 1;
        pos_info.samples = VK_SAMPLE_COUNT_1_BIT;
        pos_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        pos_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        pos_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateImage(vk_device, &pos_info, nullptr, &gbuffer->position_image_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create position image");
        }
        
        // Allocate memory for position image
        VkMemoryRequirements pos_mem_reqs;
        vkGetImageMemoryRequirements(vk_device, gbuffer->position_image_, &pos_mem_reqs);
        
        VkMemoryAllocateInfo pos_alloc{};
        pos_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        pos_alloc.allocationSize = pos_mem_reqs.size;
        pos_alloc.memoryTypeIndex = device->find_memory_type(
            pos_mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        if (vkAllocateMemory(vk_device, &pos_alloc, nullptr, &gbuffer->position_memory_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate memory for position image");
        }
        
        vkBindImageMemory(vk_device, gbuffer->position_image_, gbuffer->position_memory_, 0);
        
        // Create image view for position
        VkImageViewCreateInfo pos_view_info{};
        pos_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        pos_view_info.image = gbuffer->position_image_;
        pos_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        pos_view_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        pos_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        pos_view_info.subresourceRange.baseMipLevel = 0;
        pos_view_info.subresourceRange.levelCount = 1;
        pos_view_info.subresourceRange.baseArrayLayer = 0;
        pos_view_info.subresourceRange.layerCount = 1;
        
        if (vkCreateImageView(vk_device, &pos_view_info, nullptr, &gbuffer->position_view_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create position image view");
        }
        
        // Create normal image (RGBA16F)
        VkImageCreateInfo norm_info = pos_info;
        if (vkCreateImage(vk_device, &norm_info, nullptr, &gbuffer->normal_image_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create normal image");
        }
        
        VkMemoryRequirements norm_mem_reqs;
        vkGetImageMemoryRequirements(vk_device, gbuffer->normal_image_, &norm_mem_reqs);
        
        VkMemoryAllocateInfo norm_alloc{};
        norm_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        norm_alloc.allocationSize = norm_mem_reqs.size;
        norm_alloc.memoryTypeIndex = device->find_memory_type(
            norm_mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        if (vkAllocateMemory(vk_device, &norm_alloc, nullptr, &gbuffer->normal_memory_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate memory for normal image");
        }
        
        vkBindImageMemory(vk_device, gbuffer->normal_image_, gbuffer->normal_memory_, 0);
        
        VkImageViewCreateInfo norm_view_info = pos_view_info;
        norm_view_info.image = gbuffer->normal_image_;
        
        if (vkCreateImageView(vk_device, &norm_view_info, nullptr, &gbuffer->normal_view_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create normal image view");
        }
        
        // Create albedo image (RGBA8)
        VkImageCreateInfo alb_info = pos_info;
        alb_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        
        if (vkCreateImage(vk_device, &alb_info, nullptr, &gbuffer->albedo_image_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create albedo image");
        }
        
        VkMemoryRequirements alb_mem_reqs;
        vkGetImageMemoryRequirements(vk_device, gbuffer->albedo_image_, &alb_mem_reqs);
        
        VkMemoryAllocateInfo alb_alloc{};
        alb_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alb_alloc.allocationSize = alb_mem_reqs.size;
        alb_alloc.memoryTypeIndex = device->find_memory_type(
            alb_mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        if (vkAllocateMemory(vk_device, &alb_alloc, nullptr, &gbuffer->albedo_memory_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate memory for albedo image");
        }
        
        vkBindImageMemory(vk_device, gbuffer->albedo_image_, gbuffer->albedo_memory_, 0);
        
        VkImageViewCreateInfo alb_view_info = pos_view_info;
        alb_view_info.image = gbuffer->albedo_image_;
        alb_view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        
        if (vkCreateImageView(vk_device, &alb_view_info, nullptr, &gbuffer->albedo_view_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create albedo image view");
        }
        
        // Create material image (RGBA8)
        VkImageCreateInfo mat_info = alb_info;
        
        if (vkCreateImage(vk_device, &mat_info, nullptr, &gbuffer->material_image_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create material image");
        }
        
        VkMemoryRequirements mat_mem_reqs;
        vkGetImageMemoryRequirements(vk_device, gbuffer->material_image_, &mat_mem_reqs);
        
        VkMemoryAllocateInfo mat_alloc{};
        mat_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mat_alloc.allocationSize = mat_mem_reqs.size;
        mat_alloc.memoryTypeIndex = device->find_memory_type(
            mat_mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        if (vkAllocateMemory(vk_device, &mat_alloc, nullptr, &gbuffer->material_memory_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate memory for material image");
        }
        
        vkBindImageMemory(vk_device, gbuffer->material_image_, gbuffer->material_memory_, 0);
        
        VkImageViewCreateInfo mat_view_info = alb_view_info;
        mat_view_info.image = gbuffer->material_image_;
        
        if (vkCreateImageView(vk_device, &mat_view_info, nullptr, &gbuffer->material_view_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create material image view");
        }
        
        // Create depth image
        VkImageCreateInfo depth_info = pos_info;
        depth_info.format = VK_FORMAT_D32_SFLOAT;
        depth_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        
        if (vkCreateImage(vk_device, &depth_info, nullptr, &gbuffer->depth_image_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create depth image");
        }
        
        VkMemoryRequirements depth_mem_reqs;
        vkGetImageMemoryRequirements(vk_device, gbuffer->depth_image_, &depth_mem_reqs);
        
        VkMemoryAllocateInfo depth_alloc{};
        depth_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        depth_alloc.allocationSize = depth_mem_reqs.size;
        depth_alloc.memoryTypeIndex = device->find_memory_type(
            depth_mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        if (vkAllocateMemory(vk_device, &depth_alloc, nullptr, &gbuffer->depth_memory_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate memory for depth image");
        }
        
        vkBindImageMemory(vk_device, gbuffer->depth_image_, gbuffer->depth_memory_, 0);
        
        VkImageViewCreateInfo depth_view_info = pos_view_info;
        depth_view_info.image = gbuffer->depth_image_;
        depth_view_info.format = VK_FORMAT_D32_SFLOAT;
        depth_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        
        if (vkCreateImageView(vk_device, &depth_view_info, nullptr, &gbuffer->depth_view_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create depth image view");
        }
        
        // Create render pass
        gbuffer->create_render_pass();
        
        // Create framebuffer
        gbuffer->create_framebuffer();
        
        spdlog::info("VulkanGBuffer created: {}x{}", config.width, config.height);
        return gbuffer;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to create VulkanGBuffer: {}", e.what());
        gbuffer->cleanup();
        return nullptr;
    }
}

void VulkanGBuffer::create_render_pass() {
    VkDevice vk_device = device_->get_device();
    
    // Attachment descriptions
    std::vector<VkAttachmentDescription> attachments(5);
    
    // Position attachment
    attachments[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    // Normal attachment
    attachments[1] = attachments[0];
    
    // Albedo attachment
    attachments[2] = attachments[0];
    attachments[2].format = VK_FORMAT_R8G8B8A8_UNORM;
    
    // Material attachment
    attachments[3] = attachments[2];
    
    // Depth attachment
    attachments[4].format = VK_FORMAT_D32_SFLOAT;
    attachments[4].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[4].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[4].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[4].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[4].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[4].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[4].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    
    // Color attachment references
    std::vector<VkAttachmentReference> color_refs(4);
    for (int i = 0; i < 4; ++i) {
        color_refs[i].attachment = i;
        color_refs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    
    // Depth attachment reference
    VkAttachmentReference depth_ref{};
    depth_ref.attachment = 4;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    // Subpass description
    VkAttachmentReference color_attachment_refs[4];
    for (int i = 0; i < 4; ++i) {
        color_attachment_refs[i].attachment = i;
        color_attachment_refs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 4;
    subpass.pColorAttachments = color_attachment_refs;
    subpass.pDepthStencilAttachment = &depth_ref;
    
    // Subpass dependency
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    
    // Create render pass
    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = static_cast<uint32_t>(attachments.size());
    render_pass_info.pAttachments = attachments.data();
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;
    
    if (vkCreateRenderPass(vk_device, &render_pass_info, nullptr, &render_pass_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create G-Buffer render pass");
    }
}

void VulkanGBuffer::create_framebuffer() {
    VkDevice vk_device = device_->get_device();
    
    std::vector<VkImageView> attachments = {
        position_view_,
        normal_view_,
        albedo_view_,
        material_view_,
        depth_view_
    };
    
    VkFramebufferCreateInfo framebuffer_info{};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = render_pass_;
    framebuffer_info.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebuffer_info.pAttachments = attachments.data();
    framebuffer_info.width = config_.width;
    framebuffer_info.height = config_.height;
    framebuffer_info.layers = 1;
    
    if (vkCreateFramebuffer(vk_device, &framebuffer_info, nullptr, &framebuffer_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create G-Buffer framebuffer");
    }
}

void VulkanGBuffer::begin_geometry_pass(VkCommandBuffer cmd) {
    VkRenderPassBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin_info.renderPass = render_pass_;
    begin_info.framebuffer = framebuffer_;
    begin_info.renderArea.offset = {0, 0};
    begin_info.renderArea.extent = {config_.width, config_.height};
    
    std::vector<VkClearValue> clear_values(5);
    clear_values[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clear_values[1].color = {{0.0f, 0.0f, 1.0f, 0.0f}};  // Normal default (0, 0, 1)
    clear_values[2].color = {{1.0f, 1.0f, 1.0f, 1.0f}};  // White albedo
    clear_values[3].color = {{0.0f, 1.0f, 0.0f, 0.0f}};  // Default material
    clear_values[4].depthStencil = {1.0f, 0};
    
    begin_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
    begin_info.pClearValues = clear_values.data();
    
    vkCmdBeginRenderPass(cmd, &begin_info, VK_SUBPASS_INLINE);
}

void VulkanGBuffer::end_geometry_pass(VkCommandBuffer cmd) {
    vkCmdEndRenderPass(cmd);
}

void VulkanGBuffer::begin_lighting_pass(VkCommandBuffer cmd) {
    // Lighting pass reads from G-Buffer, no need to begin another render pass here
    // The caller will handle the lighting render pass
}

void VulkanGBuffer::end_lighting_pass(VkCommandBuffer cmd) {
    // Lighting pass cleanup handled by caller
}

void VulkanGBuffer::clear(VkCommandBuffer cmd, const glm::vec4& clear_color) {
    // Clear values are set in begin_geometry_pass
}

void VulkanGBuffer::resize(VulkanDevice* device, uint32_t width, uint32_t height) {
    if (width == config_.width && height == config_.height) {
        return;
    }
    
    cleanup();
    config_.width = width;
    config_.height = height;
    
    auto new_gbuffer = create(device, config_);
    if (new_gbuffer) {
        *this = std::move(*new_gbuffer);
    }
}

void VulkanGBuffer::cleanup() {
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
    
    // Destroy image views
    if (position_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vk_device, position_view_, nullptr);
        position_view_ = VK_NULL_HANDLE;
    }
    if (normal_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vk_device, normal_view_, nullptr);
        normal_view_ = VK_NULL_HANDLE;
    }
    if (albedo_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vk_device, albedo_view_, nullptr);
        albedo_view_ = VK_NULL_HANDLE;
    }
    if (material_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vk_device, material_view_, nullptr);
        material_view_ = VK_NULL_HANDLE;
    }
    if (depth_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vk_device, depth_view_, nullptr);
        depth_view_ = VK_NULL_HANDLE;
    }
    
    // Destroy images
    if (position_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(vk_device, position_image_, nullptr);
        position_image_ = VK_NULL_HANDLE;
    }
    if (normal_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(vk_device, normal_image_, nullptr);
        normal_image_ = VK_NULL_HANDLE;
    }
    if (albedo_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(vk_device, albedo_image_, nullptr);
        albedo_image_ = VK_NULL_HANDLE;
    }
    if (material_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(vk_device, material_image_, nullptr);
        material_image_ = VK_NULL_HANDLE;
    }
    if (depth_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(vk_device, depth_image_, nullptr);
        depth_image_ = VK_NULL_HANDLE;
    }
    
    // Free memory
    if (position_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, position_memory_, nullptr);
        position_memory_ = VK_NULL_HANDLE;
    }
    if (normal_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, normal_memory_, nullptr);
        normal_memory_ = VK_NULL_HANDLE;
    }
    if (albedo_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, albedo_memory_, nullptr);
        albedo_memory_ = VK_NULL_HANDLE;
    }
    if (material_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, material_memory_, nullptr);
        material_memory_ = VK_NULL_HANDLE;
    }
    if (depth_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, depth_memory_, nullptr);
        depth_memory_ = VK_NULL_HANDLE;
    }
}

VkFormat VulkanGBuffer::format_to_vk(GBufferFormat fmt) {
    switch (fmt) {
        case GBufferFormat::RGBA8:    return VK_FORMAT_R8G8B8A8_UNORM;
        case GBufferFormat::RGBA16F:  return VK_FORMAT_R16G16B16A16_SFLOAT;
        case GBufferFormat::RGBA32F:  return VK_FORMAT_R32G32B32A32_SFLOAT;
        case GBufferFormat::RGB16F:   return VK_FORMAT_R16G16B16_SFLOAT;
        case GBufferFormat::RG16F:    return VK_FORMAT_R16G16_SFLOAT;
        case GBufferFormat::R32F:     return VK_FORMAT_R32_SFLOAT;
        case GBufferFormat::R8:       return VK_FORMAT_R8_UNORM;
        default:                       return VK_FORMAT_R16G16B16A16_SFLOAT;
    }
}

} // namespace gws::renderer::gpu
