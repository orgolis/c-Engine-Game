#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <queue>
#include <cstdint>
#include <memory>

namespace gws::renderer::gpu {

/**
 * @brief Manages Vulkan command pools and command buffer allocation
 * 
 * Uses a ring-buffer pattern for efficient command buffer reuse across frames.
 * Supports per-frame command buffer allocation with automatic reset.
 */
class VulkanCommandBuffer {
public:
    VulkanCommandBuffer(VkCommandBuffer buffer, VkCommandPool pool)
        : buffer(buffer), pool(pool) {}
    
    VkCommandBuffer get_vk_handle() const { return buffer; }
    VkCommandPool get_pool() const { return pool; }
    
    bool is_recording() const { return recording; }
    void set_recording(bool value) { recording = value; }

private:
    friend class VulkanCommandBufferPool;
    
    VkCommandBuffer buffer = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    bool recording = false;
};

/**
 * @brief Ring-buffer allocator for command buffers
 * 
 * Pre-allocates command buffers at initialization and rotates through them
 * to avoid allocation overhead. Each frame gets its own set of buffers.
 */
class VulkanCommandBufferPool {
public:
    VulkanCommandBufferPool(VkDevice device, uint32_t queue_family, 
                           uint32_t buffers_per_frame = 3, 
                           uint32_t frames_in_flight = 2);
    ~VulkanCommandBufferPool();
    
    /**
     * @brief Allocate a command buffer for the current frame
     * @return Pointer to allocated command buffer (not owned by caller)
     */
    VulkanCommandBuffer* allocate();
    
    /**
     * @brief Return a command buffer after recording
     */
    void release(VulkanCommandBuffer* cmd_buffer);
    
    /**
     * @brief Reset all buffers for the next frame
     * Must be called at the start of each frame
     */
    void advance_frame();
    
    /**
     * @brief Get the number of available command buffers for current frame
     */
    uint32_t get_available_count() const;
    
    /**
     * @brief Wait for frame to complete before reusing buffers
     * Typically called when swapchain is out of date
     */
    void reset_all();
    
    /**
     * @brief Get underlying command pool
     */
    VkCommandPool get_command_pool() const { return command_pool; }

private:
    void create_buffers();
    void cleanup();
    
    VkDevice device;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    
    // Ring-buffer configuration
    uint32_t buffers_per_frame;
    uint32_t frames_in_flight;
    uint32_t current_frame = 0;
    uint32_t current_buffer_index = 0;
    
    // Allocated command buffers (persistent, reused)
    std::vector<std::unique_ptr<VulkanCommandBuffer>> buffers;
    
    // Queue of available buffers for current frame
    std::queue<VulkanCommandBuffer*> available_buffers;
};

}  // namespace gws::renderer::gpu
