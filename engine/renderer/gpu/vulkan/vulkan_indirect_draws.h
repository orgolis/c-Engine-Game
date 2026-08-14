#pragma once
// ============================================================================
// VulkanIndirectDrawBuffer — the GPU-driven indirect draw path (3.2).
//
// The engine had ZERO vkCmdDrawIndirect* calls. Every draw was recorded on the
// CPU, one vkCmdDrawIndexed at a time, and the meshlet path was the worst case:
// a mesh with two hundred visible meshlets cost two hundred draw calls, all of
// them against the same bound vertex and index buffers, differing only in which
// range of indices to read.
//
// That is exactly what indirect draws exist for. VkDrawIndexedIndirectCommand
// is a 1:1 mapping of the arguments vkCmdDrawIndexed already takes, so those
// two hundred calls become two hundred structs and ONE
// vkCmdDrawIndexedIndirect.
//
// I had previously recorded this item as blocked on a shared/pooled vertex+
// index buffer redesign. That is wrong for this step: indirect draws read the
// CURRENTLY BOUND buffers, so per-mesh multi-draw needs no pooling at all.
// Pooling is what a single whole-scene call would need, and that is a later,
// larger change.
//
// The real prize is what this makes possible next: with draw arguments living
// in a GPU buffer, a compute shader can set instanceCount = 0 for culled draws
// and the CPU never learns which ones were dropped. That is GPU-driven culling,
// and it cannot be built at all while the arguments only exist as function
// parameters.
//
// Host-visible and persistently mapped. The command list is rebuilt every frame
// from a visibility set that changes every frame; a staging copy would add a
// transfer and a barrier to move data that is written once and read once.
// ============================================================================

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace gws::renderer::gpu {

class VulkanDevice;

class VulkanIndirectDrawBuffer {
public:
    static std::unique_ptr<VulkanIndirectDrawBuffer> create(VulkanDevice* device,
                                                            uint32_t initial_commands = 4096);
    VulkanIndirectDrawBuffer() = default;
    ~VulkanIndirectDrawBuffer();

    VulkanIndirectDrawBuffer(const VulkanIndirectDrawBuffer&)            = delete;
    VulkanIndirectDrawBuffer& operator=(const VulkanIndirectDrawBuffer&) = delete;

    /// Drop everything recorded last frame. Call once per frame before the
    /// first append.
    void begin_frame() { commands_.clear(); }

    /// Record one draw. Returns the command's index, which is what a later
    /// compute pass would use to address it.
    uint32_t append(uint32_t index_count, uint32_t first_index,
                    int32_t vertex_offset = 0, uint32_t instance_count = 1);

    /// Upload everything appended this frame. Returns false if the buffer could
    /// not be grown, in which case the caller must fall back to direct draws
    /// rather than issue a call reading stale commands.
    bool upload();

    /// Issue `count` draws starting at `first`. Falls back to a loop of
    /// single-draw indirect calls when multiDrawIndirect was not granted --
    /// still indirect, still reading from the buffer, just not batched.
    void draw(VkCommandBuffer cmd, uint32_t first, uint32_t count) const;

    uint32_t command_count() const { return static_cast<uint32_t>(commands_.size()); }
    VkBuffer buffer()        const { return buffer_; }

    /// Real GPU submissions issued by the last draw() -- 1 when multi-draw is
    /// available, `count` when it is not. Reported rather than assumed, because
    /// "we use indirect draws now" and "we issue fewer calls" are different
    /// claims and only the second one is a performance result.
    uint32_t last_submission_count() const { return last_submission_count_; }

private:
    bool init(VulkanDevice* device, uint32_t initial_commands);
    bool grow(uint32_t commands);

    VulkanDevice*  device_ = nullptr;
    VkDevice       vk_     = VK_NULL_HANDLE;
    VkBuffer       buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    void*          mapped_ = nullptr;
    uint32_t       capacity_ = 0;
    bool           multi_draw_ = false;

    std::vector<VkDrawIndexedIndirectCommand> commands_;
    mutable uint32_t last_submission_count_ = 0;
};

}  // namespace gws::renderer::gpu
