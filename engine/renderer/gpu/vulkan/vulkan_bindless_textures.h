#pragma once

// ============================================================================
// vulkan_bindless_textures — one descriptor set holding every resident texture.
//
// A shader indexes this array by number instead of binding a texture per draw.
// That is the whole point: terrain burns FOURTEEN bindings today for one splat
// map plus three maps across four layers, which is the only reason it needs a
// pipeline of its own, and it is the wall behind both ">4 terrain layers" and
// material layering.
//
// Three Vulkan features carry the design and each is doing real work:
//
//   PARTIALLY_BOUND        the array is sized for the maximum and is
//                          legitimately full of holes. Without it every unused
//                          slot would need a dummy write or the device is in
//                          undefined state.
//   UPDATE_AFTER_BIND      a texture can be registered while the set is bound
//                          to a recorded command buffer. Without it, loading a
//                          texture mid-frame means waiting for device idle.
//   VARIABLE_DESCRIPTOR_COUNT  the set is allocated at the size actually
//                          wanted rather than the driver's reported maximum,
//                          which is 1048576 on the development GPU.
//
// The SLOT bookkeeping lives in bindless_index_allocator.h, deliberately apart
// from this: descriptor writes fail loudly against validation, the arithmetic
// fails silently.
// ============================================================================

#include "bindless_index_allocator.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>

namespace gws::renderer::gpu {

class VulkanDevice;

class VulkanBindlessTextures {
public:
    /// Null when the device cannot do descriptor indexing. That is a supported
    /// configuration, not a failure -- callers keep their bound-texture path.
    static std::unique_ptr<VulkanBindlessTextures> create(VulkanDevice* device);

    ~VulkanBindlessTextures();
    VulkanBindlessTextures(const VulkanBindlessTextures&)            = delete;
    VulkanBindlessTextures& operator=(const VulkanBindlessTextures&) = delete;

    /// Put a texture in the table and return its slot. kInvalid when full --
    /// callers must treat that as "no texture", never as slot 0.
    uint32_t register_texture(VkImageView view, VkSampler sampler);

    /// Return a slot. The descriptor is left as it was: PARTIALLY_BOUND means
    /// a stale descriptor in an unreferenced slot is legal, and clearing it
    /// would need a dummy image to point at.
    void unregister(uint32_t index);

    VkDescriptorSetLayout layout() const { return layout_; }
    VkDescriptorSet       set()    const { return set_; }
    uint32_t capacity() const { return alloc_.capacity(); }
    uint32_t live()     const { return alloc_.live(); }

    static constexpr uint32_t kInvalid = BindlessIndexAllocator::kInvalid;

private:
    VulkanBindlessTextures() = default;
    bool init(VulkanDevice* device, uint32_t capacity);

    VulkanDevice*         device_ = nullptr;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool      pool_   = VK_NULL_HANDLE;
    VkDescriptorSet       set_    = VK_NULL_HANDLE;
    BindlessIndexAllocator alloc_;
};

}  // namespace gws::renderer::gpu
