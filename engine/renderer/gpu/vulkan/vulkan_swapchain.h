#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <cstdint>

namespace gws::renderer::gpu {

// ============================================================================
// Vulkan Swapchain Management
// ============================================================================

class VulkanSwapchain {
public:
    VulkanSwapchain(VkDevice device,
                   VkPhysicalDevice physical_device,
                   VkSurfaceKHR surface,
                   VkQueue graphics_queue,
                   VkQueue present_queue,
                   uint32_t graphics_queue_family,
                   uint32_t present_queue_family);
    
    ~VulkanSwapchain();
    
    /// Initialize swapchain for rendering
    void initialize(uint32_t width, uint32_t height);
    
    /// Cleanup swapchain resources
    void cleanup();
    
    /// Acquire next image for rendering
    uint32_t acquire_next_image(VkSemaphore signal_semaphore);
    
    /// Present rendered image to display
    void present_image(uint32_t image_index, VkSemaphore wait_semaphore);
    
    /// Recreate swapchain (called on window resize)
    void recreate(uint32_t width, uint32_t height);
    
    /// Get swapchain properties
    uint32_t get_width() const { return width; }
    uint32_t get_height() const { return height; }
    uint32_t get_image_count() const { return images.size(); }
    VkFormat get_format() const { return image_format; }
    VkImage get_image(uint32_t index) const { return images[index]; }
    VkImageView get_image_view(uint32_t index) const { return image_views[index]; }
    VkSwapchainKHR get_vk_swapchain() const { return swapchain; }
    
private:
    /// Helper to choose swap surface format
    VkSurfaceFormatKHR choose_surface_format(
        const std::vector<VkSurfaceFormatKHR>& available_formats);
    
    /// Helper to choose swap present mode
    VkPresentModeKHR choose_present_mode(
        const std::vector<VkPresentModeKHR>& available_modes);
    
    /// Helper to choose swap extent
    VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities);
    
    // Vulkan resources
    VkDevice device;
    VkPhysicalDevice physical_device;
    VkSurfaceKHR surface;
    VkQueue graphics_queue;
    VkQueue present_queue;
    uint32_t graphics_queue_family;
    uint32_t present_queue_family;
    
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> images;
    std::vector<VkImageView> image_views;
    
    VkFormat image_format = VK_FORMAT_B8G8R8A8_SRGB;
    VkExtent2D extent = {0, 0};
    
    uint32_t width = 0;
    uint32_t height = 0;
    
    /// Frame index for triple buffering
    uint32_t current_frame = 0;
};

}  // namespace gws::renderer::gpu
