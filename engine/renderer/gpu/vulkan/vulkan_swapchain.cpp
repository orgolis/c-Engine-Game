#include "vulkan_swapchain.h"
#include "logging/logger.h"
#include <algorithm>

namespace gws::renderer::gpu {

VulkanSwapchain::VulkanSwapchain(VkDevice device,
                                 VkPhysicalDevice physical_device,
                                 VkSurfaceKHR surface,
                                 VkQueue graphics_queue,
                                 VkQueue present_queue,
                                 uint32_t graphics_queue_family,
                                 uint32_t present_queue_family)
    : device(device),
      physical_device(physical_device),
      surface(surface),
      graphics_queue(graphics_queue),
      present_queue(present_queue),
      graphics_queue_family(graphics_queue_family),
      present_queue_family(present_queue_family) {
}

VulkanSwapchain::~VulkanSwapchain() {
    cleanup();
}

void VulkanSwapchain::initialize(uint32_t width, uint32_t height) {
    this->width = width;
    this->height = height;
    
    // Get surface capabilities
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface,
                                             &capabilities);
    
    // Get surface formats
    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface,
                                        &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface,
                                        &format_count, formats.data());
    
    // Get present modes
    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface,
                                             &present_mode_count, nullptr);
    std::vector<VkPresentModeKHR> present_modes(present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface,
                                             &present_mode_count,
                                             present_modes.data());
    
    // Choose best options
    VkSurfaceFormatKHR surface_format = choose_surface_format(formats);
    VkPresentModeKHR present_mode = choose_present_mode(present_modes);
    extent = choose_swap_extent(capabilities);
    
    image_format = surface_format.format;
    
    // Create swapchain
    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }
    
    VkSwapchainCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = surface;
    create_info.minImageCount = image_count;
    create_info.imageFormat = surface_format.format;
    create_info.imageColorSpace = surface_format.colorSpace;
    create_info.imageExtent = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    
    uint32_t queue_family_indices[] = {graphics_queue_family, present_queue_family};
    
    if (graphics_queue_family != present_queue_family) {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = queue_family_indices;
    } else {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    
    create_info.preTransform = capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = present_mode;
    create_info.clipped = VK_TRUE;
    create_info.oldSwapchain = VK_NULL_HANDLE;
    
    vkCreateSwapchainKHR(device, &create_info, nullptr, &swapchain);
    
    // Get swapchain images
    vkGetSwapchainImagesKHR(device, swapchain, &image_count, nullptr);
    images.resize(image_count);
    vkGetSwapchainImagesKHR(device, swapchain, &image_count, images.data());
    
    // Create image views
    image_views.resize(images.size());
    for (size_t i = 0; i < images.size(); i++) {
        VkImageViewCreateInfo view_create_info{};
        view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_create_info.image = images[i];
        view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_create_info.format = image_format;
        view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_create_info.subresourceRange.baseMipLevel = 0;
        view_create_info.subresourceRange.levelCount = 1;
        view_create_info.subresourceRange.baseArrayLayer = 0;
        view_create_info.subresourceRange.layerCount = 1;
        
        vkCreateImageView(device, &view_create_info, nullptr, &image_views[i]);
    }
    
    GWS_LOG_INFO("✅ Swapchain created: {}x{} ({} images)",
                extent.width, extent.height, images.size());
}

void VulkanSwapchain::cleanup() {
    if (device == VK_NULL_HANDLE) {
        return;
    }
    
    // Destroy image views
    for (auto view : image_views) {
        vkDestroyImageView(device, view, nullptr);
    }
    image_views.clear();
    
    // Destroy swapchain
    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
    
    images.clear();
}

uint32_t VulkanSwapchain::acquire_next_image(VkSemaphore signal_semaphore) {
    uint32_t image_index = 0;
    
    VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                           signal_semaphore, VK_NULL_HANDLE,
                                           &image_index);
    
    if (result != VK_SUCCESS) {
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            // Swapchain is out of date, needs recreation
            GWS_LOG_WARN("Swapchain out of date, needs recreation");
        } else {
            GWS_LOG_ERROR("Failed to acquire next image: {}", static_cast<int>(result));
        }
        return ~0u;
    }
    
    current_frame = image_index;
    return image_index;
}

void VulkanSwapchain::present_image(uint32_t image_index, VkSemaphore wait_semaphore) {
    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = wait_semaphore ? 1 : 0;
    present_info.pWaitSemaphores = &wait_semaphore;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain;
    present_info.pImageIndices = &image_index;
    
    VkResult result = vkQueuePresentKHR(present_queue, &present_info);
    
    if (result != VK_SUCCESS) {
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            GWS_LOG_WARN("Swapchain out of date during present");
        } else {
            GWS_LOG_ERROR("Failed to present: {}", static_cast<int>(result));
        }
    }
}

void VulkanSwapchain::recreate(uint32_t new_width, uint32_t new_height) {
    if (new_width == 0 || new_height == 0) {
        return;
    }
    
    width = new_width;
    height = new_height;
    
    cleanup();
    initialize(new_width, new_height);
}

VkSurfaceFormatKHR VulkanSwapchain::choose_surface_format(
    const std::vector<VkSurfaceFormatKHR>& available_formats) {
    
    // Prefer SRGB format
    for (const auto& format : available_formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    
    // Fallback to first available
    return available_formats.empty() ? VkSurfaceFormatKHR{} : available_formats[0];
}

VkPresentModeKHR VulkanSwapchain::choose_present_mode(
    const std::vector<VkPresentModeKHR>& available_modes) {
    
    // Prefer mailbox for triple buffering
    for (const auto& mode : available_modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    
    // Fallback to FIFO (always available)
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanSwapchain::choose_swap_extent(
    const VkSurfaceCapabilitiesKHR& capabilities) {
    
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }
    
    VkExtent2D actual_extent = {width, height};
    
    actual_extent.width = std::max(capabilities.minImageExtent.width,
                                   std::min(capabilities.maxImageExtent.width,
                                           actual_extent.width));
    actual_extent.height = std::max(capabilities.minImageExtent.height,
                                    std::min(capabilities.maxImageExtent.height,
                                            actual_extent.height));
    
    return actual_extent;
}

}  // namespace gws::renderer::gpu
