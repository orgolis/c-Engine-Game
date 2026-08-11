/**
 * @file image.cpp
 * @brief Vulkan Image implementation
 */

#include "image.h"
#include <stdexcept>
#include <spdlog/spdlog.h>
#include <cstdint>

namespace vks {

// ============================================================================
// VulkanImage Implementation
// ============================================================================

VulkanImage VulkanImage::create(VkDevice device, VkPhysicalDevice pdev,
                               uint32_t width, uint32_t height,
                               ImageFormat format, ImageUsage usage) {
    VulkanImage img;
    img.device = device;
    img.pdev = pdev;
    img.width = width;
    img.height = height;
    img.vk_format = format_to_vk(format);
    img.usage = usage;
    
    if (width == 0 || height == 0) {
        throw std::runtime_error("VulkanImage: invalid dimensions");
    }
    
    // Determine image usage flags
    VkImageUsageFlags image_usage = 0;
    VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    if (usage == ImageUsage::TEXTURE) {
        image_usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    } else if (usage == ImageUsage::ATTACHMENT) {
        if (format == ImageFormat::DEPTH32F || format == ImageFormat::DEPTH24_STENCIL8) {
            image_usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        } else {
            image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }
    } else if (usage == ImageUsage::STORAGE) {
        image_usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    
    // Create image
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = img.vk_format;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = 1;  // Single mip level for now (Phase 5: mip generation)
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = image_usage;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = initial_layout;
    
    if (vkCreateImage(device, &image_info, nullptr, &img.image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VkImage");
    }
    
    // Allocate memory
    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(device, img.image, &mem_req);
    
    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_req.size;
    alloc_info.memoryTypeIndex = img.find_memory_type(mem_req.memoryTypeBits, 
                                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &alloc_info, nullptr, &img.memory) != VK_SUCCESS) {
        vkDestroyImage(device, img.image, nullptr);
        throw std::runtime_error("Failed to allocate image memory");
    }
    
    vkBindImageMemory(device, img.image, img.memory, 0);
    
    // Create image view
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = img.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = img.vk_format;
    view_info.subresourceRange.aspectMask = format_to_aspect(format);
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device, &view_info, nullptr, &img.view) != VK_SUCCESS) {
        vkDestroyImage(device, img.image, nullptr);
        vkFreeMemory(device, img.memory, nullptr);
        throw std::runtime_error("Failed to create image view");
    }
    
    // Create sampler for textures
    if (usage == ImageUsage::TEXTURE) {
        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.anisotropyEnable = VK_TRUE;
        sampler_info.maxAnisotropy = 16.0f;
        sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        sampler_info.unnormalizedCoordinates = VK_FALSE;
        sampler_info.compareEnable = VK_FALSE;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_info.mipLodBias = 0.0f;
        sampler_info.minLod = 0.0f;
        sampler_info.maxLod = 0.0f;
        
        if (vkCreateSampler(device, &sampler_info, nullptr, &img.sampler) != VK_SUCCESS) {
            vkDestroyImageView(device, img.view, nullptr);
            vkDestroyImage(device, img.image, nullptr);
            vkFreeMemory(device, img.memory, nullptr);
            throw std::runtime_error("Failed to create sampler");
        }
    }
    
    img.current_layout = initial_layout;
    
    spdlog::debug("✓ VulkanImage created: {}x{} format={}", width, height, static_cast<int>(format));
    return img;
}

VulkanImage VulkanImage::load_from_file(VkDevice device, VkPhysicalDevice pdev,
                                       const std::string& path, ImageUsage usage) {
    // Placeholder: actual implementation requires stb_image
    // For now, create a simple white 1x1 texture
    spdlog::warn("VulkanImage::load_from_file: not yet implemented for {}", path);
    return create(device, pdev, 1, 1, ImageFormat::RGBA8_SRGB, usage);
}

VulkanImage::VulkanImage(VulkanImage&& other) noexcept
    : device(other.device), pdev(other.pdev), image(other.image), view(other.view),
      sampler(other.sampler), memory(other.memory), width(other.width), height(other.height),
      vk_format(other.vk_format), current_layout(other.current_layout), usage(other.usage) {
    other.device = VK_NULL_HANDLE;
    other.pdev = VK_NULL_HANDLE;
    other.image = VK_NULL_HANDLE;
    other.view = VK_NULL_HANDLE;
    other.sampler = VK_NULL_HANDLE;
    other.memory = VK_NULL_HANDLE;
}

VulkanImage& VulkanImage::operator=(VulkanImage&& other) noexcept {
    destroy();
    device = other.device;
    pdev = other.pdev;
    image = other.image;
    view = other.view;
    sampler = other.sampler;
    memory = other.memory;
    width = other.width;
    height = other.height;
    vk_format = other.vk_format;
    current_layout = other.current_layout;
    usage = other.usage;
    
    other.device = VK_NULL_HANDLE;
    other.pdev = VK_NULL_HANDLE;
    other.image = VK_NULL_HANDLE;
    other.view = VK_NULL_HANDLE;
    other.sampler = VK_NULL_HANDLE;
    other.memory = VK_NULL_HANDLE;
    
    return *this;
}

void VulkanImage::destroy() {
    if (device == VK_NULL_HANDLE) return;
    
    if (sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, sampler, nullptr);
        sampler = VK_NULL_HANDLE;
    }
    
    if (view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, view, nullptr);
        view = VK_NULL_HANDLE;
    }
    
    if (image != VK_NULL_HANDLE) {
        vkDestroyImage(device, image, nullptr);
        image = VK_NULL_HANDLE;
    }
    
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
    
    device = VK_NULL_HANDLE;
}

VkFormat VulkanImage::format_to_vk(ImageFormat fmt) {
    switch (fmt) {
    case ImageFormat::RGBA8_SRGB:       return VK_FORMAT_R8G8B8A8_SRGB;
    case ImageFormat::RGBA8_UNORM:      return VK_FORMAT_R8G8B8A8_UNORM;
    case ImageFormat::RGBA16F:          return VK_FORMAT_R16G16B16A16_SFLOAT;
    case ImageFormat::RGBA32F:          return VK_FORMAT_R32G32B32A32_SFLOAT;
    case ImageFormat::DEPTH32F:         return VK_FORMAT_D32_SFLOAT;
    case ImageFormat::DEPTH24_STENCIL8: return VK_FORMAT_D24_UNORM_S8_UINT;
    default: return VK_FORMAT_UNDEFINED;
    }
}

VkImageAspectFlags VulkanImage::format_to_aspect(ImageFormat fmt) {
    switch (fmt) {
    case ImageFormat::DEPTH32F:
    case ImageFormat::DEPTH24_STENCIL8:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    default:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

uint32_t VulkanImage::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(pdev, &mem_props);
    
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) && (mem_props.memoryTypes[i].propertyFlags & properties)) {
            return i;
        }
    }
    
    throw std::runtime_error("Failed to find suitable memory type");
}

}  // namespace vks
