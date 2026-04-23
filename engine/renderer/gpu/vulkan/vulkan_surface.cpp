#include "vulkan_surface.h"
#include "gws/core/logging/logger.h"

#ifdef _WIN32
#include <vulkan/vulkan_win32.h>
#endif

namespace gws::renderer::gpu {

#ifdef _WIN32

bool Win32VulkanSurface::create_surface(VkInstance instance, void* window_handle,
                                        VkSurfaceKHR* surface) {
    if (!instance || !window_handle || !surface) {
        GWS_LOG_ERROR("Invalid parameters for surface creation");
        return false;
    }
    
    HWND hwnd = static_cast<HWND>(window_handle);
    HINSTANCE hinstance = GetModuleHandle(nullptr);
    
    VkWin32SurfaceCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    create_info.hinstance = hinstance;
    create_info.hwnd = hwnd;
    
    if (vkCreateWin32SurfaceKHR(instance, &create_info, nullptr, surface) != VK_SUCCESS) {
        GWS_LOG_ERROR("Failed to create Win32 Vulkan surface");
        return false;
    }
    
    GWS_LOG_INFO("✅ Win32 Vulkan surface created");
    return true;
}

void Win32VulkanSurface::destroy_surface(VkInstance instance, VkSurfaceKHR surface) {
    if (instance && surface) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        GWS_LOG_DEBUG("Win32 Vulkan surface destroyed");
    }
}

#endif  // _WIN32

#ifdef __linux__

bool WaylandVulkanSurface::create_surface(VkInstance instance, void* window_handle,
                                         VkSurfaceKHR* surface) {
    // TODO: Implement Wayland surface creation
    GWS_LOG_WARN("Wayland surface creation not yet implemented");
    return false;
}

void WaylandVulkanSurface::destroy_surface(VkInstance instance, VkSurfaceKHR surface) {
    if (instance && surface) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
    }
}

bool X11VulkanSurface::create_surface(VkInstance instance, void* window_handle,
                                     VkSurfaceKHR* surface) {
    // TODO: Implement X11 surface creation
    GWS_LOG_WARN("X11 surface creation not yet implemented");
    return false;
}

void X11VulkanSurface::destroy_surface(VkInstance instance, VkSurfaceKHR surface) {
    if (instance && surface) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
    }
}

#endif  // __linux__

}  // namespace gws::renderer::gpu
