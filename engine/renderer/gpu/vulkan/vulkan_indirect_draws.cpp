// ============================================================================
// VulkanIndirectDrawBuffer — see vulkan_indirect_draws.h.
// ============================================================================
#include "vulkan_indirect_draws.h"

#include "vulkan_device.h"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstring>
#include <cstdlib>

namespace gws::renderer::gpu {

std::unique_ptr<VulkanIndirectDrawBuffer> VulkanIndirectDrawBuffer::create(
    VulkanDevice* device, uint32_t initial_commands) {
    auto b = std::unique_ptr<VulkanIndirectDrawBuffer>(new VulkanIndirectDrawBuffer());
    if (!b->init(device, initial_commands)) return nullptr;
    return b;
}

VulkanIndirectDrawBuffer::~VulkanIndirectDrawBuffer() {
    if (vk_ == VK_NULL_HANDLE) return;
    if (mapped_) { vkUnmapMemory(vk_, memory_); mapped_ = nullptr; }
    if (buffer_) vkDestroyBuffer(vk_, buffer_, nullptr);
    if (memory_) vkFreeMemory(vk_, memory_, nullptr);
    // The device is idle by the time the renderer tears its buffers down, so
    // anything still on the retirement list can go now.
    for (const Retired& r : retired_) {
        if (r.buffer) vkDestroyBuffer(vk_, r.buffer, nullptr);
        if (r.memory) vkFreeMemory(vk_, r.memory, nullptr);
    }
    retired_.clear();
}

// Grace in frames before a replaced buffer is freed. Frames in flight is 2-3;
// 8 leaves room without keeping meaningful memory alive -- each retired buffer
// is one allocation of a few hundred KB at most, and growth is rare.
static constexpr uint64_t kIndirectRetireFrames = 8;

void VulkanIndirectDrawBuffer::begin_frame() {
    ++frame_;

    // Free buffers retired long enough ago that no in-flight frame can still
    // be reading them.
    for (size_t i = 0; i < retired_.size();) {
        if (frame_ - retired_[i].frame >= kIndirectRetireFrames) {
            if (retired_[i].buffer) vkDestroyBuffer(vk_, retired_[i].buffer, nullptr);
            if (retired_[i].memory) vkFreeMemory(vk_, retired_[i].memory, nullptr);
            retired_[i] = retired_.back();
            retired_.pop_back();
        } else {
            ++i;
        }
    }

    // Size for what last frame actually needed, before anything is recorded.
    // A frame that reveals more geometry than any before it can still grow
    // mid-recording -- grow() retires safely now -- but it no longer happens
    // every time a big mesh comes into view.
    if (high_water_ > capacity_) {
        grow(high_water_ * 3 / 2 + 256);
    }

    commands_.clear();
    uploaded_ = 0;
}

bool VulkanIndirectDrawBuffer::init(VulkanDevice* device, uint32_t initial_commands) {
    if (!device) return false;
    device_     = device;
    vk_         = device->get_device();
    multi_draw_ = device->multi_draw_indirect_enabled();

    if (!grow(initial_commands == 0 ? 1024 : initial_commands)) return false;

    spdlog::info("[Indirect] draw buffer ready — {} commands, multiDrawIndirect {}",
                 capacity_, multi_draw_ ? "ON" : "OFF (falling back to a loop)");
    return true;
}

bool VulkanIndirectDrawBuffer::grow(uint32_t commands) {
    const VkDeviceSize bytes = VkDeviceSize(commands) * sizeof(VkDrawIndexedIndirectCommand);

    if (mapped_) { vkUnmapMemory(vk_, memory_); mapped_ = nullptr; }
    // Retire, never destroy here. Draws recorded earlier in THIS frame's
    // command buffer -- and draws still executing from previous frames --
    // reference the outgoing buffer by handle. Destroying it on the spot is a
    // use-after-free that validation catches as
    // VUID-vkDestroyBuffer-buffer-00922 and that a driver reports, much later
    // and much less helpfully, as a device loss.
    if (buffer_ || memory_) {
        retired_.push_back(Retired{buffer_, memory_, frame_});
        buffer_ = VK_NULL_HANDLE;
        memory_ = VK_NULL_HANDLE;
    }

    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size  = bytes;
    // STORAGE too, not only INDIRECT: the point of putting draw arguments in a
    // buffer is that a compute shader can edit them (zeroing instanceCount for
    // culled draws). Adding the usage later would mean recreating the buffer,
    // so it costs nothing to declare the intent now.
    bi.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vk_, &bi, nullptr, &buffer_) != VK_SUCCESS) {
        spdlog::error("[Indirect] buffer creation failed ({} commands)", commands);
        return false;
    }

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(vk_, buffer_, &req);

    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(device_->get_physical_device(), &mp);

    const VkMemoryPropertyFlags want =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t type_index = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((req.memoryTypeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & want) == want) { type_index = i; break; }
    if (type_index == UINT32_MAX) {
        spdlog::error("[Indirect] no host-visible coherent memory type");
        return false;
    }

    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = type_index;
    if (vkAllocateMemory(vk_, &ai, nullptr, &memory_) != VK_SUCCESS) {
        spdlog::error("[Indirect] memory allocation failed");
        return false;
    }
    vkBindBufferMemory(vk_, buffer_, memory_, 0);
    if (vkMapMemory(vk_, memory_, 0, bytes, 0, &mapped_) != VK_SUCCESS) {
        spdlog::error("[Indirect] memory map failed");
        return false;
    }

    capacity_ = commands;
    return true;
}

uint32_t VulkanIndirectDrawBuffer::append(uint32_t index_count, uint32_t first_index,
                                          int32_t vertex_offset, uint32_t instance_count) {
    VkDrawIndexedIndirectCommand c{};
    c.indexCount    = index_count;
    c.instanceCount = instance_count;
    c.firstIndex    = first_index;
    c.vertexOffset  = vertex_offset;
    // Always 0. Anything else requires the drawIndirectFirstInstance feature,
    // which is deliberately not requested -- an unsupported non-zero value is
    // undefined behaviour, not a validation error.
    c.firstInstance = 0;

    commands_.push_back(c);
    if (commands_.size() > high_water_)
        high_water_ = static_cast<uint32_t>(commands_.size());
    return static_cast<uint32_t>(commands_.size() - 1);
}

bool VulkanIndirectDrawBuffer::upload() {
    if (commands_.empty()) return true;

    // GWS_STRESS_INDIRECT_GROW=1 forces a reallocation on every upload, so the
    // growth path runs at a LATER submesh -- after earlier draws were already
    // recorded against the outgoing buffer. Kept because it is the regression
    // test for exactly that bug: natural growth almost always lands on the
    // first submesh of the first frame, before any draw exists, so merely
    // shrinking the initial capacity proved nothing. Run the editor with this
    // set and validation on; it must report no
    // VUID-vkDestroyBuffer-buffer-00922.
    static const bool force_grow = std::getenv("GWS_STRESS_INDIRECT_GROW") != nullptr;
    if (force_grow) {
        if (!grow(capacity_ + 1)) return false;
        uploaded_ = 0;
    }

    if (commands_.size() > capacity_) {
        const uint32_t want = static_cast<uint32_t>(commands_.size() * 3 / 2 + 256);
        if (!grow(want)) return false;
        // grow() destroys the old buffer and creates a new one, so nothing in
        // it has been uploaded. Re-copy from the start.
        uploaded_ = 0;
    }

    const uint32_t have = static_cast<uint32_t>(commands_.size());
    if (uploaded_ >= have) return true;   // nothing new since the last call

    const size_t stride = sizeof(VkDrawIndexedIndirectCommand);
    std::memcpy(static_cast<uint8_t*>(mapped_) + uploaded_ * stride,
                commands_.data() + uploaded_,
                (have - uploaded_) * stride);
    uploaded_ = have;
    return true;
}

void VulkanIndirectDrawBuffer::draw(VkCommandBuffer cmd, uint32_t first, uint32_t count) const {
    last_submission_count_ = 0;
    if (cmd == VK_NULL_HANDLE || buffer_ == VK_NULL_HANDLE || count == 0) return;
    if (first + count > commands_.size()) return;   // never read past what was uploaded

    const uint32_t stride = sizeof(VkDrawIndexedIndirectCommand);
    const VkDeviceSize offset = VkDeviceSize(first) * stride;

    if (multi_draw_) {
        // The whole point: one submission regardless of how many draws.
        vkCmdDrawIndexedIndirect(cmd, buffer_, offset, count, stride);
        last_submission_count_ = 1;
        return;
    }

    // Still indirect -- the arguments come from the buffer, so a compute pass
    // editing them works either way. Only the batching is lost.
    for (uint32_t i = 0; i < count; ++i)
        vkCmdDrawIndexedIndirect(cmd, buffer_, offset + VkDeviceSize(i) * stride, 1, stride);
    last_submission_count_ = count;
}

}  // namespace gws::renderer::gpu
