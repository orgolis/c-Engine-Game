/**
 * @file vulkan_sampler_cache.h
 * @brief Deduplicating VkSampler cache (Master Plan Stage 2 — texture handling).
 *
 * Vulkan caps the number of live samplers (`maxSamplerAllocationCount`, often
 * ~4000). Before this cache every `Texture` created its own `VkSampler`, so a
 * large scene could exhaust the limit and leaked one sampler per texture. The
 * cache maps a small, hashable `SamplerDesc` → one shared `VkSampler`; the
 * TextureManager owns a single cache and hands cached samplers to textures.
 *
 * Samplers returned by `get()` are owned by the cache and destroyed with it —
 * callers must NOT `vkDestroySampler` them.
 */

#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace gws::renderer::gpu {

class VulkanDevice;

/// Small, value-comparable description of a sampler configuration. Two textures
/// asking for the same wrap/filter/anisotropy share one `VkSampler`.
struct SamplerDesc {
    VkFilter             min_filter   = VK_FILTER_LINEAR;
    VkFilter             mag_filter   = VK_FILTER_LINEAR;
    VkSamplerMipmapMode  mipmap_mode  = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    VkSamplerAddressMode address_u    = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode address_v    = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode address_w    = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    /// Requested max anisotropy. <= 1.0 disables anisotropy. The cache clamps
    /// to the device limit and ignores it when the device did not enable the
    /// `samplerAnisotropy` feature.
    float                max_anisotropy = 16.0f;
    float                min_lod      = 0.0f;
    float                max_lod      = VK_LOD_CLAMP_NONE;
    VkBorderColor        border_color = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

    bool operator==(const SamplerDesc& o) const {
        return min_filter == o.min_filter && mag_filter == o.mag_filter &&
               mipmap_mode == o.mipmap_mode && address_u == o.address_u &&
               address_v == o.address_v && address_w == o.address_w &&
               max_anisotropy == o.max_anisotropy && min_lod == o.min_lod &&
               max_lod == o.max_lod && border_color == o.border_color;
    }

    /// Stable hash over all fields (used as the cache key).
    uint64_t hash() const;
};

struct SamplerDescHash {
    size_t operator()(const SamplerDesc& d) const { return static_cast<size_t>(d.hash()); }
};

class SamplerCache {
public:
    explicit SamplerCache(VulkanDevice* device) : device_(device) {}
    ~SamplerCache();

    SamplerCache(const SamplerCache&)            = delete;
    SamplerCache& operator=(const SamplerCache&) = delete;

    /// Get-or-create the shared sampler for `desc`. Owned by the cache.
    /// Returns VK_NULL_HANDLE only on a hard `vkCreateSampler` failure.
    VkSampler get(const SamplerDesc& desc);

    /// Number of distinct samplers currently cached (for diagnostics/tests).
    size_t size() const { return cache_.size(); }

private:
    VulkanDevice*                                                   device_ = nullptr;
    std::unordered_map<SamplerDesc, VkSampler, SamplerDescHash>     cache_;
};

} // namespace gws::renderer::gpu
