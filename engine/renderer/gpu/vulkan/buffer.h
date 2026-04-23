/**
 * @file buffer.h
 * @brief Vulkan Buffer abstraction - RAII wrapper for VkBuffer with VMA
 * 
 * Provides unified interface for vertex, index, uniform, and storage buffers
 * with automatic GPU memory management and CPU-GPU synchronization.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <cstring>
#include <cstddef>

namespace vks {

/**
 * @enum BufferUsage
 * @brief Specifies how a buffer will be used
 */
enum class BufferUsage {
    VERTEX,           ///< Vertex buffer (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
    INDEX,            ///< Index buffer (VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
    UNIFORM,          ///< Uniform buffer (VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
    STORAGE,          ///< Storage buffer (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
    TRANSFER_DST,     ///< Transfer destination (VK_BUFFER_USAGE_TRANSFER_DST_BIT)
    TRANSFER_SRC,     ///< Transfer source (VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
};

/**
 * @class VulkanBuffer
 * @brief RAII wrapper for GPU buffers
 * 
 * Automatically manages VkBuffer and device memory lifecycle.
 * Supports CPU and GPU data updates with synchronization.
 */
class VulkanBuffer {
public:
    VulkanBuffer() = default;
    ~VulkanBuffer() { destroy(); }
    
    /**
     * @brief Create a buffer with optional initial data
     * @param device Vulkan logical device
     * @param pdev Physical device for memory selection
     * @param usage Buffer usage type
     * @param size Buffer size in bytes
     * @param data Optional initial CPU data to upload
     * @return Created buffer
     */
    static VulkanBuffer create(VkDevice device, VkPhysicalDevice pdev,
                              BufferUsage usage, size_t size, const void* data = nullptr);
    
    /**
     * @brief Get the underlying VkBuffer handle
     */
    VkBuffer get() const { return buffer; }
    
    /**
     * @brief Get buffer size in bytes
     */
    size_t get_size() const { return size; }
    
    /**
     * @brief Get buffer usage type
     */
    BufferUsage get_usage() const { return usage; }
    
    /**
     * @brief Check if buffer is valid
     */
    bool is_valid() const { return buffer != VK_NULL_HANDLE; }
    
    /**
     * @brief Update buffer data (for HOST_VISIBLE buffers like uniforms)
     * @param data Pointer to CPU data
     * @param size Bytes to copy
     * @param offset Offset in buffer
     */
    void update(const void* data, size_t size, size_t offset = 0);
    
    /**
     * @brief GPU copy data into buffer via staging
     * @param cmd Command buffer for recording transfer
     * @param data CPU source data
     * @param size Bytes to copy
     * @param offset Buffer offset
     * 
     * Note: Caller must submit and wait for command buffer
     */
    void upload(VkCommandBuffer cmd, const void* data, size_t size, size_t offset = 0);
    
    // Move semantics (non-copyable)
    VulkanBuffer(VulkanBuffer&& other) noexcept;
    VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;
    
    // Delete copies
    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;

private:
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    size_t size = 0;
    BufferUsage usage = BufferUsage::VERTEX;
    
    // For HOST_VISIBLE buffers: mapped pointer
    void* mapped_ptr = nullptr;
    
    void destroy();
    VkBufferUsageFlagBits usage_to_vk() const;
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const;
};

}  // namespace vks
