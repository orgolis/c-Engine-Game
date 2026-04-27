#include "query_pool.h"
#include "vulkan_device.h"
#include <cassert>

namespace engine::vulkan {

VulkanQueryPool::~VulkanQueryPool() {
  destroy();
}

void VulkanQueryPool::create(VkDevice device, uint32_t query_count) {
  assert(device != VK_NULL_HANDLE);
  assert(query_count > 0);

  device_ = device;
  query_count_ = query_count;

  // Create query pool for timestamps
  VkQueryPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
  pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
  pool_info.queryCount = query_count;

  VkResult result = vkCreateQueryPool(device, &pool_info, nullptr, &query_pool_);
  assert(result == VK_SUCCESS && "Failed to create query pool");

  // Create buffer for query results (8 bytes per query)
  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = query_count * sizeof(uint64_t);
  buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  result = vkCreateBuffer(device, &buffer_info, nullptr, &result_buffer_);
  assert(result == VK_SUCCESS && "Failed to create result buffer");

  // Allocate memory for result buffer (CPU-visible, host coherent)
  VkMemoryRequirements mem_req;
  vkGetBufferMemoryRequirements(device, result_buffer_, &mem_req);

  VkMemoryAllocateInfo alloc_info{};
  alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  alloc_info.allocationSize = mem_req.size;
  alloc_info.memoryTypeIndex = VulkanDevice::find_memory_type(
      mem_req.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  result = vkAllocateMemory(device, &alloc_info, nullptr, &result_buffer_memory_);
  assert(result == VK_SUCCESS && "Failed to allocate result buffer memory");

  vkBindBufferMemory(device, result_buffer_, result_buffer_memory_, 0);

  // Get GPU timestamp period from device
  float gpu_timestamp_period = 1.0f;  // Will be updated from physical device
  // timestamp_to_ms_ = gpu_timestamp_period / 1e9f * 1e3f = gpu_timestamp_period / 1e6f
}

void VulkanQueryPool::destroy() {
  if (device_ == VK_NULL_HANDLE) return;

  if (query_pool_ != VK_NULL_HANDLE) {
    vkDestroyQueryPool(device_, query_pool_, nullptr);
    query_pool_ = VK_NULL_HANDLE;
  }

  if (result_buffer_ != VK_NULL_HANDLE) {
    vkDestroyBuffer(device_, result_buffer_, nullptr);
    result_buffer_ = VK_NULL_HANDLE;
  }

  if (result_buffer_memory_ != VK_NULL_HANDLE) {
    vkFreeMemory(device_, result_buffer_memory_, nullptr);
    result_buffer_memory_ = VK_NULL_HANDLE;
  }

  device_ = VK_NULL_HANDLE;
  query_count_ = 0;
}

void VulkanQueryPool::write_timestamp(VkCommandBuffer cmd, VkPipelineStageFlagBits stage, uint32_t query_idx) {
  assert(cmd != VK_NULL_HANDLE);
  assert(query_idx < query_count_);
  assert(query_pool_ != VK_NULL_HANDLE);

  vkCmdWriteTimestamp(cmd, stage, query_pool_, query_idx);
}

bool VulkanQueryPool::get_results(std::vector<uint64_t>& out_timestamps, bool wait) {
  assert(query_pool_ != VK_NULL_HANDLE);
  assert(result_buffer_ != VK_NULL_HANDLE);

  out_timestamps.resize(query_count_);

  // Copy query results to result buffer
  VkQueryResultFlags flags = VK_QUERY_RESULT_64_BIT;
  if (wait) {
    flags |= VK_QUERY_RESULT_WAIT_BIT;
  }

  VkResult result = vkGetQueryPoolResults(
      device_,
      query_pool_,
      0,                                // firstQuery
      query_count_,                     // queryCount
      query_count_ * sizeof(uint64_t),  // dataSize
      out_timestamps.data(),
      sizeof(uint64_t),                 // stride
      flags);

  return result == VK_SUCCESS;
}

bool VulkanQueryPool::is_ready() const {
  // Non-blocking check for query availability
  std::vector<uint64_t> dummy(query_count_);
  VkResult result = vkGetQueryPoolResults(
      device_,
      query_pool_,
      0,
      query_count_,
      query_count_ * sizeof(uint64_t),
      dummy.data(),
      sizeof(uint64_t),
      VK_QUERY_RESULT_64_BIT);

  return result == VK_SUCCESS;
}

void VulkanQueryPool::reset(VkCommandBuffer cmd) {
  assert(cmd != VK_NULL_HANDLE);
  assert(query_pool_ != VK_NULL_HANDLE);

  vkCmdResetQueryPool(cmd, query_pool_, 0, query_count_);
}

}  // namespace engine::vulkan
