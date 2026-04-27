#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>

namespace gws {

// Forward declarations
class Device;
class VulkanBuffer;

// Indirect Draw Command (GPU side)
struct IndirectDrawCommand {
    uint32_t vertex_count;
    uint32_t instance_count;
    uint32_t first_vertex;
    uint32_t first_instance;
};

// GPU-driven rendering: submit batches using vkCmdDrawIndirectCount
// Workflow:
//   1. Culling system (frustum + HZB) outputs indirect buffer with visible items
//   2. GPU counter tracks number of visible items
//   3. vkCmdDrawIndirectCount executes all visible draw calls in one dispatch
// Reduces CPU overhead and enables GPU-parallelized culling

class IndirectDispatcher {
public:
    struct Config {
        uint32_t max_draw_calls = 4096;
        bool gpu_driven_enabled = false;  // Use vkCmdDrawIndirectCount
    };

    IndirectDispatcher(Device* device, const Config& cfg);
    ~IndirectDispatcher();

    // Initialize indirect dispatch buffers
    void initialize();

    // Dispatch indirect rendering
    // indirect_buffer: buffer containing VkDrawIndirectCommand entries
    // count_buffer: buffer containing visible count (single uint32_t)
    // count_offset: byte offset to uint32_t count in count_buffer
    void dispatch_indirect(
        VkCommandBuffer cmd,
        VkBuffer indirect_buffer,
        VkBuffer count_buffer,
        VkDeviceSize count_offset,
        uint32_t max_draw_count,
        VkPipelineBindPoint bind_point
    );

    // Alternative: legacy indirect dispatch (all items, no dynamic count)
    void dispatch_indirect_fixed(
        VkCommandBuffer cmd,
        VkBuffer indirect_buffer,
        uint32_t draw_count,
        VkPipelineBindPoint bind_point
    );

    bool is_gpu_driven() const { return config_.gpu_driven_enabled; }
    void set_gpu_driven(bool enabled) { config_.gpu_driven_enabled = enabled; }

private:
    Device* device_;
    Config config_;

    std::unique_ptr<VulkanBuffer> count_buffer_;  // For dynamic count tracking
};

}  // namespace gws
