#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace gws::renderer::gpu {

/**
 * @brief Platform-independent Vulkan surface abstraction
 * 
 * Handles creation of Vulkan surfaces from native window handles.
 * Separates windowing concerns from graphics initialization.
 */
class VulkanSurface {
public:
    VulkanSurface() = default;
    virtual ~VulkanSurface() = default;
    
    /**
     * @brief Create a Vulkan surface from a native window handle
     * @param instance Vulkan instance
     * @param window_handle Platform-specific window handle (HWND on Windows, xcb_window_t on Linux, etc.)
     * @param surface Output surface handle
     * @return true if surface creation succeeded
     */
    virtual bool create_surface(VkInstance instance, void* window_handle,
                               VkSurfaceKHR* surface) = 0;
    
    /**
     * @brief Destroy the surface
     */
    virtual void destroy_surface(VkInstance instance, VkSurfaceKHR surface) = 0;
};

#ifdef _WIN32

/**
 * @brief Windows-specific Vulkan surface (Win32)
 */
class Win32VulkanSurface : public VulkanSurface {
public:
    bool create_surface(VkInstance instance, void* window_handle,
                       VkSurfaceKHR* surface) override;
    void destroy_surface(VkInstance instance, VkSurfaceKHR surface) override;
};

#endif

#ifdef __linux__

/**
 * @brief Linux Wayland Vulkan surface
 */
class WaylandVulkanSurface : public VulkanSurface {
public:
    bool create_surface(VkInstance instance, void* window_handle,
                       VkSurfaceKHR* surface) override;
    void destroy_surface(VkInstance instance, VkSurfaceKHR surface) override;
};

/**
 * @brief Linux X11 Vulkan surface
 */
class X11VulkanSurface : public VulkanSurface {
public:
    bool create_surface(VkInstance instance, void* window_handle,
                       VkSurfaceKHR* surface) override;
    void destroy_surface(VkInstance instance, VkSurfaceKHR surface) override;
};

#endif

}  // namespace gws::renderer::gpu
