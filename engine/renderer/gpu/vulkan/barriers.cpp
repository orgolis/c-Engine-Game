#include "barriers.h"
#include "logging/logger.h"

namespace gws::renderer::gpu {

// ============================================================================
// Barrier Manager Implementation
// ============================================================================

VkAccessFlags BarrierManager::get_access_mask(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            return 0;
        case VK_IMAGE_LAYOUT_GENERAL:
            return VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_ACCESS_SHADER_READ_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return VK_ACCESS_TRANSFER_READ_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_ACCESS_TRANSFER_WRITE_BIT;
        case VK_IMAGE_LAYOUT_PREINITIALIZED:
            return VK_ACCESS_HOST_WRITE_BIT;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            return VK_ACCESS_MEMORY_READ_BIT;
        default:
            return 0;
    }
}

VkPipelineStageFlags BarrierManager::get_pipeline_stage(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        case VK_IMAGE_LAYOUT_GENERAL:
            return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_PIPELINE_STAGE_TRANSFER_BIT;
        case VK_IMAGE_LAYOUT_PREINITIALIZED:
            return VK_PIPELINE_STAGE_HOST_BIT;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        default:
            return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
}

void BarrierManager::image_barrier(VkCommandBuffer cmd_buffer,
                                  VkImage image,
                                  VkImageLayout old_layout,
                                  VkImageLayout new_layout,
                                  VkPipelineStageFlags src_stage,
                                  VkPipelineStageFlags dst_stage) {
    
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcAccessMask = get_access_mask(old_layout);
    barrier.dstAccessMask = get_access_mask(new_layout);
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    
    // Detect if this is a depth image and adjust aspect mask
    if (new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
        new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    
    vkCmdPipelineBarrier(cmd_buffer, src_stage, dst_stage, 0,
                        0, nullptr, 0, nullptr, 1, &barrier);
}

void BarrierManager::buffer_barrier(VkCommandBuffer cmd_buffer,
                                   VkBuffer buffer,
                                   VkDeviceSize offset,
                                   VkDeviceSize size,
                                   VkAccessFlags src_access,
                                   VkAccessFlags dst_access,
                                   VkPipelineStageFlags src_stage,
                                   VkPipelineStageFlags dst_stage) {
    
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.buffer = buffer;
    barrier.offset = offset;
    barrier.size = size;
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    
    vkCmdPipelineBarrier(cmd_buffer, src_stage, dst_stage, 0,
                        0, nullptr, 1, &barrier, 0, nullptr);
}

void BarrierManager::transition_to_transfer_dst(VkCommandBuffer cmd_buffer, VkImage image) {
    image_barrier(cmd_buffer, image, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT);
}

void BarrierManager::transition_to_shader_read(VkCommandBuffer cmd_buffer, VkImage image) {
    image_barrier(cmd_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

void BarrierManager::transition_to_color_attachment(VkCommandBuffer cmd_buffer, VkImage image) {
    image_barrier(cmd_buffer, image, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
}

void BarrierManager::transition_to_depth_attachment(VkCommandBuffer cmd_buffer, VkImage image) {
    image_barrier(cmd_buffer, image, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);
}

void BarrierManager::transition_to_present(VkCommandBuffer cmd_buffer, VkImage image) {
    image_barrier(cmd_buffer, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}

// ============================================================================
// Synchronization Implementation
// ============================================================================

VkSemaphore Synchronization::create_semaphore(VkDevice device) {
    VkSemaphoreCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    VkSemaphore semaphore = VK_NULL_HANDLE;
    if (vkCreateSemaphore(device, &create_info, nullptr, &semaphore) != VK_SUCCESS) {
        GWS_LOG_ERROR("Failed to create semaphore");
        return VK_NULL_HANDLE;
    }
    
    return semaphore;
}

VkFence Synchronization::create_fence(VkDevice device, bool initially_signaled) {
    VkFenceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    create_info.flags = initially_signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;
    
    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(device, &create_info, nullptr, &fence) != VK_SUCCESS) {
        GWS_LOG_ERROR("Failed to create fence");
        return VK_NULL_HANDLE;
    }
    
    return fence;
}

void Synchronization::wait_fence(VkDevice device, VkFence fence, uint64_t timeout) {
    if (fence != VK_NULL_HANDLE) {
        vkWaitForFences(device, 1, &fence, VK_TRUE, timeout);
    }
}

void Synchronization::reset_fence(VkDevice device, VkFence fence) {
    if (fence != VK_NULL_HANDLE) {
        vkResetFences(device, 1, &fence);
    }
}

void Synchronization::destroy_semaphore(VkDevice device, VkSemaphore semaphore) {
    if (semaphore != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
}

void Synchronization::destroy_fence(VkDevice device, VkFence fence) {
    if (fence != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroyFence(device, fence, nullptr);
    }
}

}  // namespace gws::renderer::gpu
