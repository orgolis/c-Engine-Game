/**
 * @file vulkan_sampler_cache.cpp
 * @brief SamplerDesc hashing + deduplicating VkSampler creation.
 */

#include "vulkan_sampler_cache.h"
#include "vulkan_device.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>

namespace gws::renderer::gpu {

namespace {
// Fold a 32-bit field into a running FNV-1a-style 64-bit hash.
inline void mix(uint64_t& h, uint32_t v) {
    h ^= v;
    h *= 1099511628211ull;
}
inline void mix_f(uint64_t& h, float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    mix(h, bits);
}
} // namespace

uint64_t SamplerDesc::hash() const {
    uint64_t h = 1469598103934665603ull;
    mix(h, static_cast<uint32_t>(min_filter));
    mix(h, static_cast<uint32_t>(mag_filter));
    mix(h, static_cast<uint32_t>(mipmap_mode));
    mix(h, static_cast<uint32_t>(address_u));
    mix(h, static_cast<uint32_t>(address_v));
    mix(h, static_cast<uint32_t>(address_w));
    mix_f(h, max_anisotropy);
    mix_f(h, min_lod);
    mix_f(h, max_lod);
    mix(h, static_cast<uint32_t>(border_color));
    return h;
}

SamplerCache::~SamplerCache() {
    if (!device_) return;
    VkDevice vkdev = device_->get_device();
    for (auto& [desc, sampler] : cache_) {
        if (sampler != VK_NULL_HANDLE) vkDestroySampler(vkdev, sampler, nullptr);
    }
    cache_.clear();
}

VkSampler SamplerCache::get(const SamplerDesc& desc) {
    if (auto it = cache_.find(desc); it != cache_.end()) return it->second;

    VkSamplerCreateInfo si{};
    si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.minFilter    = desc.min_filter;
    si.magFilter    = desc.mag_filter;
    si.mipmapMode   = desc.mipmap_mode;
    si.addressModeU = desc.address_u;
    si.addressModeV = desc.address_v;
    si.addressModeW = desc.address_w;
    si.minLod       = desc.min_lod;
    si.maxLod       = desc.max_lod;
    si.mipLodBias   = 0.0f;
    si.borderColor  = desc.border_color;
    si.compareEnable = VK_FALSE;
    si.unnormalizedCoordinates = VK_FALSE;

    // Anisotropy only when the device enabled the feature AND the caller asked
    // for more than 1x. Clamp to the device limit.
    if (desc.max_anisotropy > 1.0f && device_->anisotropy_enabled()) {
        si.anisotropyEnable = VK_TRUE;
        si.maxAnisotropy    = std::min(desc.max_anisotropy, device_->max_anisotropy());
    } else {
        si.anisotropyEnable = VK_FALSE;
        si.maxAnisotropy    = 1.0f;
    }

    VkSampler sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(device_->get_device(), &si, nullptr, &sampler) != VK_SUCCESS) {
        spdlog::error("SamplerCache: vkCreateSampler failed");
        return VK_NULL_HANDLE;
    }
    cache_.emplace(desc, sampler);
    return sampler;
}

} // namespace gws::renderer::gpu
