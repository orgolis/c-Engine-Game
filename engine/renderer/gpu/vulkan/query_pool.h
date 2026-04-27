#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

namespace engine::vulkan {

/**
 * VulkanQueryPool - Manages Vulkan timestamp query pools
 * 
 * Handles creation, timestamp recording, and result retrieval with proper
 * GPU→CPU synchronization. Uses circular buffering to avoid query stalls.
 */
class VulkanQueryPool {
public:
  VulkanQueryPool() = default;
  ~VulkanQueryPool();

  VulkanQueryPool(const VulkanQueryPool&) = delete;
  VulkanQueryPool& operator=(const VulkanQueryPool&) = delete;

  /**
   * Initialize query pool with given capacity
   * @param device Vulkan logical device
   * @param query_count Maximum number of queries (timestamps)
   */
  void create(VkDevice device, uint32_t query_count);

  /**
   * Destroy query pool and associated resources
   */
  void destroy();

  /**
   * Write timestamp to query at given index
   * @param cmd Command buffer to record timestamp write
   * @param stage Pipeline stage at which to write
   * @param query_idx Index into query pool (0 to query_count_-1)
   */
  void write_timestamp(VkCommandBuffer cmd, VkPipelineStageFlagBits stage, uint32_t query_idx);

  /**
   * Retrieve query results with optional synchronization
   * @param out_timestamps Vector to populate with timestamp values
   * @param wait If true, block until results are available
   * @return true if results were successfully retrieved
   */
  bool get_results(std::vector<uint64_t>& out_timestamps, bool wait = true);

  /**
   * Check if query results are available (non-blocking)
   * @return true if results can be retrieved without blocking
   */
  bool is_ready() const;

  /**
   * Get GPU timestamp to milliseconds conversion factor
   * @return Multiplier to convert raw GPU timestamps to milliseconds
   */
  float get_timestamp_to_ms_factor() const { return timestamp_to_ms_; }

  /**
   * Get the capacity of this query pool
   */
  uint32_t get_query_count() const { return query_count_; }

  /**
   * Reset query pool for next frame of recording
   */
  void reset(VkCommandBuffer cmd);

private:
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueryPool query_pool_ = VK_NULL_HANDLE;
  VkBuffer result_buffer_ = VK_NULL_HANDLE;       // GPU-visible buffer for query results
  VkDeviceMemory result_buffer_memory_ = VK_NULL_HANDLE;

  uint32_t query_count_ = 0;
  float timestamp_to_ms_ = 1.0f / 1e6f;  // Default: GPU timestamp in nanoseconds
};

}  // namespace engine::vulkan
