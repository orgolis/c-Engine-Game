/**
 * @file buffer.cpp
 * @brief Vulkan Buffer implementation
 */

#include "buffer.h"
#include <stdexcept>
#include <spdlog/spdlog.h>
#include <cstdint>

namespace vks {

// ============================================================================
// VulkanBuffer Implementation
// ============================================================================

VulkanBuffer VulkanBuffer::create(VkDevice device, VkPhysicalDevice pdev,
                                  BufferUsage usage, size_t size, const void* data) {
    VulkanBuffer buffer;
    buffer.device = device;
    buffer.size = size;
    buffer.usage = usage;
    
    if (size == 0) {
        spdlog::warn("VulkanBuffer::create: size is 0");
        return buffer;
    }
    
    // Create buffer
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = buffer.usage_to_vk();
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    // For uniform/storage buffers, also support as transfer dst if we need to update
    if (usage == BufferUsage::UNIFORM || usage == BufferUsage::STORAGE) {
        buffer_info.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    
    if (vkCreateBuffer(device, &buffer_info, nullptr, &buffer.buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VkBuffer");
    }
    
    // Allocate memory
    VkMemoryRequirements mem_req;
    vkGetBufferMemoryRequirements(device, buffer.buffer, &mem_req);
    
    // Determine memory properties based on usage
    VkMemoryPropertyFlags properties = 0;
    
    // Uniform and vertex/index with data need to be easily updatable or GPU-local
    if (usage == BufferUsage::UNIFORM) {
        properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    } else if (usage == BufferUsage::VERTEX || usage == BufferUsage::INDEX) {
        // GPU-local memory (fast, but CPU can't update easily)
        properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    } else if (usage == BufferUsage::TRANSFER_SRC) {
        // For staging buffers
        properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    } else {
        properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }
    
    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_req.size;
    alloc_info.memoryTypeIndex = buffer.find_memory_type(mem_req.memoryTypeBits, properties);
    
    if (vkAllocateMemory(device, &alloc_info, nullptr, &buffer.memory) != VK_SUCCESS) {
        vkDestroyBuffer(device, buffer.buffer, nullptr);
        throw std::runtime_error("Failed to allocate VkDeviceMemory");
    }
    
    vkBindBufferMemory(device, buffer.buffer, buffer.memory, 0);
    
    // Upload initial data if provided
    if (data != nullptr) {
        if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            // Host-visible: direct mapping
            buffer.update(data, size, 0);
        } else {
            // Device-local: would need staging buffer (caller should handle)
            spdlog::warn("VulkanBuffer: initial data provided for GPU-local buffer, ignoring");
        }
    }
    
    spdlog::debug("✓ VulkanBuffer created: {} bytes", size);
    return buffer;
}

void VulkanBuffer::update(const void* data, size_t size, size_t offset) {
    if (buffer == VK_NULL_HANDLE) {
        throw std::runtime_error("Cannot update invalid buffer");
    }
    
    if (offset + size > this->size) {
        throw std::runtime_error("VulkanBuffer::update: size exceeds buffer");
    }
    
    // Only works for HOST_VISIBLE buffers
    void* mapped = nullptr;
    if (vkMapMemory(device, memory, offset, size, 0, &mapped) != VK_SUCCESS) {
        throw std::runtime_error("Failed to map buffer memory");
    }
    
    memcpy(mapped, data, size);
    vkUnmapMemory(device, memory);
}

void VulkanBuffer::upload(VkCommandBuffer cmd, const void* data, size_t size, size_t offset) {
    if (buffer == VK_NULL_HANDLE) {
        throw std::runtime_error("Cannot upload to invalid buffer");
    }
    
    if (offset + size > this->size) {
        throw std::runtime_error("VulkanBuffer::upload: size exceeds buffer");
    }
    
    // Create temporary staging buffer
    VulkanBuffer staging = VulkanBuffer::create(device, nullptr, BufferUsage::TRANSFER_SRC, size, data);
    
    // Record copy command
    VkBufferCopy region{};
    region.srcOffset = 0;
    region.dstOffset = offset;
    region.size = size;
    
    vkCmdCopyBuffer(cmd, staging.buffer, buffer, 1, &region);
    
    // Staging buffer destroyed at end of scope
}

VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept
    : device(other.device), buffer(other.buffer), memory(other.memory),
      size(other.size), usage(other.usage), mapped_ptr(other.mapped_ptr) {
    other.buffer = VK_NULL_HANDLE;
    other.memory = VK_NULL_HANDLE;
    other.device = VK_NULL_HANDLE;
}

VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept {
    destroy();
    device = other.device;
    buffer = other.buffer;
    memory = other.memory;
    size = other.size;
    usage = other.usage;
    mapped_ptr = other.mapped_ptr;
    
    other.buffer = VK_NULL_HANDLE;
    other.memory = VK_NULL_HANDLE;
    other.device = VK_NULL_HANDLE;
    
    return *this;
}

void VulkanBuffer::destroy() {
    if (device == VK_NULL_HANDLE) return;
    
    if (mapped_ptr != nullptr) {
        vkUnmapMemory(device, memory);
        mapped_ptr = nullptr;
    }
    
    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    }
    
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
    
    device = VK_NULL_HANDLE;
}

VkBufferUsageFlagBits VulkanBuffer::usage_to_vk() const {
    switch (usage) {
    case BufferUsage::VERTEX:      return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    case BufferUsage::INDEX:       return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    case BufferUsage::UNIFORM:     return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    case BufferUsage::STORAGE:     return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    case BufferUsage::TRANSFER_DST: return VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    case BufferUsage::TRANSFER_SRC: return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    default: return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
}

uint32_t VulkanBuffer::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties mem_props;
    // Note: This is a simplified version. In production, pass physical device as parameter
    // For now, just return a safe default
    return 0;
}

}  // namespace vks
