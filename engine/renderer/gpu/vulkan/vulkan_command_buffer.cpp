#include "vulkan_command_buffer.h"
#include "logging/logger.h"
#include <cassert>

namespace gws::renderer::gpu {

VulkanCommandBufferPool::VulkanCommandBufferPool(VkDevice device, 
                                                 uint32_t queue_family,
                                                 uint32_t buffers_per_frame,
                                                 uint32_t frames_in_flight)
    : device(device),
      buffers_per_frame(buffers_per_frame),
      frames_in_flight(frames_in_flight) {
    
    // Create command pool
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = queue_family;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
                     VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    
    if (vkCreateCommandPool(device, &pool_info, nullptr, &command_pool) != VK_SUCCESS) {
        GWS_LOG_ERROR("Failed to create command pool");
        return;
    }
    
    GWS_LOG_INFO("✅ Command pool created (queue family: {})", queue_family);
    
    // Allocate command buffers
    create_buffers();
}

VulkanCommandBufferPool::~VulkanCommandBufferPool() {
    cleanup();
}

void VulkanCommandBufferPool::create_buffers() {
    uint32_t total_buffers = buffers_per_frame * frames_in_flight;
    
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = total_buffers;
    
    std::vector<VkCommandBuffer> vk_buffers(total_buffers);
    if (vkAllocateCommandBuffers(device, &alloc_info, vk_buffers.data()) != VK_SUCCESS) {
        GWS_LOG_ERROR("Failed to allocate command buffers");
        return;
    }
    
    // Wrap in managed objects
    buffers.reserve(total_buffers);
    for (auto vk_buffer : vk_buffers) {
        buffers.push_back(std::make_unique<VulkanCommandBuffer>(vk_buffer, command_pool));
    }
    
    // Initialize available queue for first frame
    for (uint32_t i = 0; i < buffers_per_frame; ++i) {
        available_buffers.push(buffers[i].get());
    }
    
    GWS_LOG_INFO("✅ Allocated {} command buffers ({} frames × {} buffers)", 
                total_buffers, frames_in_flight, buffers_per_frame);
}

void VulkanCommandBufferPool::cleanup() {
    // Clear buffers
    buffers.clear();
    
    // Clear available queue
    while (!available_buffers.empty()) {
        available_buffers.pop();
    }
    
    // Destroy command pool
    if (command_pool != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, command_pool, nullptr);
        command_pool = VK_NULL_HANDLE;
    }
}

VulkanCommandBuffer* VulkanCommandBufferPool::allocate() {
    if (available_buffers.empty()) {
        GWS_LOG_WARN("No available command buffers for frame {}", current_frame);
        return nullptr;
    }
    
    VulkanCommandBuffer* buffer = available_buffers.front();
    available_buffers.pop();
    
    // Begin recording
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    if (vkBeginCommandBuffer(buffer->get_vk_handle(), &begin_info) != VK_SUCCESS) {
        GWS_LOG_ERROR("Failed to begin command buffer recording");
        return nullptr;
    }
    
    buffer->set_recording(true);
    return buffer;
}

void VulkanCommandBufferPool::release(VulkanCommandBuffer* cmd_buffer) {
    if (!cmd_buffer || !cmd_buffer->is_recording()) {
        return;
    }
    
    // End recording
    if (vkEndCommandBuffer(cmd_buffer->get_vk_handle()) != VK_SUCCESS) {
        GWS_LOG_ERROR("Failed to end command buffer recording");
    }
    
    cmd_buffer->set_recording(false);
}

void VulkanCommandBufferPool::advance_frame() {
    current_frame = (current_frame + 1) % frames_in_flight;
    current_buffer_index = current_frame * buffers_per_frame;
    
    // Repopulate available queue for new frame
    while (!available_buffers.empty()) {
        available_buffers.pop();
    }
    
    for (uint32_t i = 0; i < buffers_per_frame; ++i) {
        uint32_t buffer_index = current_buffer_index + i;
        if (buffer_index < buffers.size()) {
            available_buffers.push(buffers[buffer_index].get());
        }
    }
    
    GWS_LOG_DEBUG("Advanced to frame {} (buffers {}-{})", 
                  current_frame, current_buffer_index, 
                  current_buffer_index + buffers_per_frame - 1);
}

uint32_t VulkanCommandBufferPool::get_available_count() const {
    return available_buffers.size();
}

void VulkanCommandBufferPool::reset_all() {
    if (command_pool != VK_NULL_HANDLE) {
        vkResetCommandPool(device, command_pool, 0);
        GWS_LOG_DEBUG("Reset all command buffers");
    }
}

}  // namespace gws::renderer::gpu
