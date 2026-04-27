#include "indirect_dispatcher.h"
#include "../device.h"
#include "vulkan_buffer.h"

namespace gws {

IndirectDispatcher::IndirectDispatcher(Device* device, const Config& cfg)
    : device_(device), config_(cfg) {
}

IndirectDispatcher::~IndirectDispatcher() = default;

void IndirectDispatcher::initialize() {
    // Create count buffer for GPU-driven culling results
    count_buffer_ = std::make_unique<VulkanBuffer>(device_, sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // Initialize to zero
    // Deferred: initial upload from CPU via transfer queue
}

void IndirectDispatcher::dispatch_indirect(
    VkCommandBuffer cmd,
    VkBuffer indirect_buffer,
    VkBuffer count_buffer,
    VkDeviceSize count_offset,
    uint32_t max_draw_count,
    VkPipelineBindPoint bind_point) {

    if (!config_.gpu_driven_enabled) {
        // Fallback to CPU-count dispatch
        dispatch_indirect_fixed(cmd, indirect_buffer, max_draw_count, bind_point);
        return;
    }

    // GPU-driven: count is in GPU buffer, fetched dynamically
    // vkCmdDrawIndirectCount = indirect rendering with GPU-computed count
    //
    // Benefits:
    //   - No GPU→CPU readback stall (count stays on GPU)
    //   - GPU culling outputs directly to count buffer
    //   - Single vkCmd submission for all visible items
    //
    // Signature: (cmd, buffer, offset, countBuffer, countOffset, maxCount, stride)

    vkCmdDrawIndirectCount(cmd,
        indirect_buffer,         // Buffer containing draw commands
        0,                       // Byte offset to first command
        count_buffer,            // Buffer containing draw count
        count_offset,            // Byte offset to count in count_buffer
        max_draw_count,          // Max number of draws (safety limit)
        sizeof(IndirectDrawCommand)  // Stride between commands (typically 16 bytes)
    );

    // Alternative pattern (if using structured buffers):
    // vkCmdDrawIndirectCount(cmd,
    //     indirect_buffer,
    //     0,
    //     count_buffer,          // GPU counter from culling compute shader
    //     0,                      // Count is at offset 0
    //     config_.max_draw_calls, // Safety cap
    //     sizeof(IndirectDrawCommand)
    // );
}

void IndirectDispatcher::dispatch_indirect_fixed(
    VkCommandBuffer cmd,
    VkBuffer indirect_buffer,
    uint32_t draw_count,
    VkPipelineBindPoint bind_point) {

    // Legacy: CPU specifies exact count (no GPU counter)
    // Used when GPU-driven counting is disabled or during debug
    //
    // Signature: (cmd, buffer, offset, drawCount, stride)

    vkCmdDrawIndirect(cmd,
        indirect_buffer,         // Buffer containing draw commands
        0,                       // Byte offset to first command
        draw_count,              // Number of draws
        sizeof(IndirectDrawCommand)  // Stride between commands
    );
}

}  // namespace gws
