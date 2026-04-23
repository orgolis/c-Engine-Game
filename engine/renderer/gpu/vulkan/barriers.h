#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

namespace gws::renderer::gpu {

/**
 * @brief Image barrier information
 */
struct ImageBarrier {
    VkImage image;
    VkImageLayout old_layout;
    VkImageLayout new_layout;
    VkAccessFlags src_access_mask;
    VkAccessFlags dst_access_mask;
    uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED;
    uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED;
};

/**
 * @brief Buffer barrier information
 */
struct BufferBarrier {
    VkBuffer buffer;
    VkAccessFlags src_access_mask;
    VkAccessFlags dst_access_mask;
    VkDeviceSize offset = 0;
    VkDeviceSize size = VK_WHOLE_SIZE;
    uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED;
    uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED;
};

/**
 * @brief Synchronization and Memory Barrier Manager
 * 
 * Handles insertion of appropriate pipeline barriers for synchronization
 * and memory access ordering in Vulkan.
 */
class BarrierManager {
public:
    /**
     * @brief Add an image transition barrier
     */
    static void image_barrier(VkCommandBuffer cmd_buffer,
                             VkImage image,
                             VkImageLayout old_layout,
                             VkImageLayout new_layout,
                             VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    
    /**
     * @brief Add a buffer barrier
     */
    static void buffer_barrier(VkCommandBuffer cmd_buffer,
                              VkBuffer buffer,
                              VkDeviceSize offset,
                              VkDeviceSize size,
                              VkAccessFlags src_access,
                              VkAccessFlags dst_access,
                              VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                              VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    
    /**
     * @brief Transition image from undefined to transfer destination
     */
    static void transition_to_transfer_dst(VkCommandBuffer cmd_buffer, VkImage image);
    
    /**
     * @brief Transition image from transfer destination to shader read-only
     */
    static void transition_to_shader_read(VkCommandBuffer cmd_buffer, VkImage image);
    
    /**
     * @brief Transition image to color attachment
     */
    static void transition_to_color_attachment(VkCommandBuffer cmd_buffer, VkImage image);
    
    /**
     * @brief Transition image to depth attachment
     */
    static void transition_to_depth_attachment(VkCommandBuffer cmd_buffer, VkImage image);
    
    /**
     * @brief Transition image to present
     */
    static void transition_to_present(VkCommandBuffer cmd_buffer, VkImage image);
    
    /**
     * @brief Get access flags for a given layout
     */
    static VkAccessFlags get_access_mask(VkImageLayout layout);
    
    /**
     * @brief Get pipeline stage for a given layout
     */
    static VkPipelineStageFlags get_pipeline_stage(VkImageLayout layout);
};

/**
 * @brief Synchronization Primitives
 */
class Synchronization {
public:
    /**
     * @brief Create a semaphore for GPU-GPU synchronization
     */
    static VkSemaphore create_semaphore(VkDevice device);
    
    /**
     * @brief Create a fence for GPU-CPU synchronization
     */
    static VkFence create_fence(VkDevice device, bool initially_signaled = false);
    
    /**
     * @brief Wait for fence (CPU blocks until GPU finishes)
     */
    static void wait_fence(VkDevice device, VkFence fence, uint64_t timeout = UINT64_MAX);
    
    /**
     * @brief Reset fence to unsignaled state
     */
    static void reset_fence(VkDevice device, VkFence fence);
    
    /**
     * @brief Destroy semaphore
     */
    static void destroy_semaphore(VkDevice device, VkSemaphore semaphore);
    
    /**
     * @brief Destroy fence
     */
    static void destroy_fence(VkDevice device, VkFence fence);
};

}  // namespace gws::renderer::gpu
