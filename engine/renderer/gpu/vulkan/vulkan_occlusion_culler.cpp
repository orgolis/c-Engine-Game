/**
 * @file vulkan_occlusion_culler.cpp
 * @brief Implementation of per-draw occlusion queries.
 */

#include "vulkan_occlusion_culler.h"
#include "vulkan_device.h"

#include <spdlog/spdlog.h>
#include <algorithm>

namespace gws::renderer::gpu {

std::unique_ptr<VulkanOcclusionCuller>
VulkanOcclusionCuller::create(VulkanDevice* device, const Config& cfg) {
    if (device == nullptr) {
        spdlog::error("VulkanOcclusionCuller::create: null device");
        return nullptr;
    }
    auto c = std::unique_ptr<VulkanOcclusionCuller>(new VulkanOcclusionCuller());
    if (!c->initialize(device, cfg)) return nullptr;
    spdlog::info("VulkanOcclusionCuller created (max_draws={})", cfg.max_draws);
    return c;
}

VulkanOcclusionCuller::~VulkanOcclusionCuller() { destroy(); }

bool VulkanOcclusionCuller::initialize(VulkanDevice* device, const Config& cfg) {
    device_    = device;
    max_draws_ = cfg.max_draws;
    visibility_.assign(max_draws_, 1); // first frame: everything visible

    VkQueryPoolCreateInfo qi{};
    qi.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qi.queryType  = VK_QUERY_TYPE_OCCLUSION;
    qi.queryCount = max_draws_;
    if (vkCreateQueryPool(device_->get_device(), &qi, nullptr, &query_pool_) != VK_SUCCESS) {
        spdlog::error("VulkanOcclusionCuller: vkCreateQueryPool failed");
        return false;
    }
    return true;
}

void VulkanOcclusionCuller::destroy() {
    if (device_ == nullptr) return;
    if (query_pool_ != VK_NULL_HANDLE) {
        vkDestroyQueryPool(device_->get_device(), query_pool_, nullptr);
        query_pool_ = VK_NULL_HANDLE;
    }
    device_ = nullptr;
}

void VulkanOcclusionCuller::begin_frame(VkCommandBuffer cmd, uint32_t draw_count) {
    if (!enabled_) {
        pending_draw_count_ = 0;
        return;
    }
    // Cap at the pool size — over-cap draws simply aren't queried (treated as
    // always-visible by the recorder).
    pending_draw_count_ = std::min(draw_count, max_draws_);

    // If the list size changed since last frame, the visibility cache is no
    // longer meaningfully aligned — invalidate it.
    if (draw_count != static_cast<uint32_t>(visibility_.size())) {
        visibility_.assign(max_draws_, 1);
        have_prev_results_ = false;
    }

    if (cmd != VK_NULL_HANDLE && query_pool_ != VK_NULL_HANDLE && pending_draw_count_ > 0) {
        // Reset *every* slot — slots we didn't write to last frame would
        // otherwise yield stale data on the next resolve.
        vkCmdResetQueryPool(cmd, query_pool_, 0, max_draws_);
    }
}

bool VulkanOcclusionCuller::was_visible(uint32_t draw_index) const {
    if (!enabled_ || !have_prev_results_) return true;
    if (draw_index >= visibility_.size())  return true;
    return visibility_[draw_index] != 0;
}

void VulkanOcclusionCuller::begin_draw(VkCommandBuffer cmd, uint32_t draw_index) {
    if (!enabled_ || cmd == VK_NULL_HANDLE || query_pool_ == VK_NULL_HANDLE) return;
    if (draw_index >= max_draws_) return;
    vkCmdBeginQuery(cmd, query_pool_, draw_index, 0); // 0 = imprecise, "any sample passed" is enough
}

void VulkanOcclusionCuller::end_draw(VkCommandBuffer cmd, uint32_t draw_index) {
    if (!enabled_ || cmd == VK_NULL_HANDLE || query_pool_ == VK_NULL_HANDLE) return;
    if (draw_index >= max_draws_) return;
    vkCmdEndQuery(cmd, query_pool_, draw_index);
}

void VulkanOcclusionCuller::resolve_results() {
    if (!enabled_ || query_pool_ == VK_NULL_HANDLE || pending_draw_count_ == 0) {
        last_visible_count_ = pending_draw_count_;
        last_culled_count_  = 0;
        return;
    }

    // Fetch sample counts as uint32_t (one per draw). Use 64-bit so we can
    // also check the availability bit cheaply if we want — but right now we
    // call this only after waiting on the frame's fence, so results are
    // guaranteed available.
    std::vector<uint32_t> samples(pending_draw_count_, 0u);
    VkResult vr = vkGetQueryPoolResults(
        device_->get_device(),
        query_pool_,
        0,                                            // firstQuery
        pending_draw_count_,                          // queryCount
        samples.size() * sizeof(uint32_t),            // dataSize
        samples.data(),                               // pData
        sizeof(uint32_t),                             // stride
        VK_QUERY_RESULT_WAIT_BIT);                    // flags

    if (vr != VK_SUCCESS) {
        // Don't trust the partial data — keep everything visible.
        std::fill(visibility_.begin(), visibility_.end(), 1);
        have_prev_results_  = false;
        last_visible_count_ = pending_draw_count_;
        last_culled_count_  = 0;
        return;
    }

    if (visibility_.size() != pending_draw_count_) {
        visibility_.assign(pending_draw_count_, 1);
    }
    uint32_t visible = 0;
    for (uint32_t i = 0; i < pending_draw_count_; ++i) {
        const bool any = samples[i] > 0u;
        visibility_[i] = any ? 1 : 0;
        visible += any ? 1u : 0u;
    }
    have_prev_results_ = true;
    last_visible_count_ = visible;
    last_culled_count_  = pending_draw_count_ - visible;
}

} // namespace gws::renderer::gpu
