#pragma once
// Per-frame uniform data recorded INTO the command buffer.
//
// Several passes kept one host-mapped uniform buffer and memcpy'd this frame's
// parameters into it while recording. With two frames in flight the CPU
// records frame N+1 while the GPU may still be executing frame N, so frame N
// could read frame N+1's camera matrices -- or a half-written mix of the two.
// The same class of race F16 fixed for the ray-tracing scene.
//
// vkCmdUpdateBuffer instead puts the data in the command stream: it lands in
// submission order, after every earlier read of the buffer and before every
// later one, with no extra buffers to ring. Only for small blocks (the spec
// caps it at 64 KiB, size a multiple of 4) and only outside a render pass.
// The buffer needs VK_BUFFER_USAGE_TRANSFER_DST_BIT.

#include <vulkan/vulkan.h>

#include <cassert>

namespace gws::renderer::gpu {

inline void cmd_update_uniform(VkCommandBuffer cmd, VkBuffer buffer,
                               const void* data, VkDeviceSize size) {
    assert(size > 0 && size <= 65536 && (size % 4) == 0);
    // Readers can be any shader stage of any earlier frame (vertex, fragment,
    // compute, and geometry where a pass voxelises), so the barriers name
    // ALL_COMMANDS: two tiny barriers a frame per pass, not worth narrowing.
    VkMemoryBarrier pre{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    pre.srcAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
    pre.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 1, &pre, 0, nullptr, 0, nullptr);
    vkCmdUpdateBuffer(cmd, buffer, 0, size, data);
    VkMemoryBarrier post{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    post.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    post.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 1, &post, 0, nullptr, 0, nullptr);
}

}  // namespace gws::renderer::gpu
