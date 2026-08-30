// ============================================================================
// gpu_zones.cpp — see gpu_zones.h.
// ============================================================================

#include "gpu_zones.h"

#ifndef GWS_ZONES_NO_VULKAN
#include "gpu_profiler.h"
#include <spdlog/spdlog.h>
#endif

#include <algorithm>
#include <cstdint>

namespace engine::vulkan {

// ---------------------------------------------------------------------------
// ZoneRegistry
// ---------------------------------------------------------------------------

uint32_t ZoneRegistry::index_of(const std::string& name) {
    const auto it = index_.find(name);
    if (it != index_.end()) return it->second;
    // Refuse rather than wrap: two zones sharing a slot would report one as the
    // other, which is a confident wrong number -- the precise failure this
    // system replaced.
    if (names_.size() >= kMaxZones) return kInvalid;

    const uint32_t idx = static_cast<uint32_t>(names_.size());
    names_.push_back(name);
    index_.emplace(name, idx);
    ran_.push_back(false);
    return idx;
}

uint32_t ZoneRegistry::find(const std::string& name) const {
    const auto it = index_.find(name);
    return it == index_.end() ? kInvalid : it->second;
}

void ZoneRegistry::begin_frame() {
    // Names and slots persist; only liveness resets. A zone that is not
    // dispatched this frame must report absence, not its last value.
    std::fill(ran_.begin(), ran_.end(), false);
}

void ZoneRegistry::mark_ran(uint32_t index) {
    if (index >= ran_.size()) return;
    ran_[index] = true;
}

bool ZoneRegistry::ran(uint32_t index) const {
    return index < ran_.size() && ran_[index];
}

uint32_t ZoneRegistry::ran_count() const {
    uint32_t n = 0;
    for (bool b : ran_) if (b) ++n;
    return n;
}

#ifndef GWS_ZONES_NO_VULKAN
// ---------------------------------------------------------------------------
// GpuZones
// ---------------------------------------------------------------------------

bool GpuZones::init(VkPhysicalDevice phys, VkDevice device, uint32_t graphics_queue_family) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(phys, &props);
    period_ns_ = static_cast<double>(props.limits.timestampPeriod);

    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qf_count, nullptr);
    std::vector<VkQueueFamilyProperties> qfs(qf_count);
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qf_count, qfs.data());

    const bool valid_bits = graphics_queue_family < qfs.size() &&
                            qfs[graphics_queue_family].timestampValidBits > 0;
    supported_ = (period_ns_ > 0.0) && valid_bits;
    if (!supported_) {
        spdlog::warn("[gpu-zones] device reports no usable timestamps - GPU profiling disabled");
        return false;
    }

    VkQueryPoolCreateInfo qp{};
    qp.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qp.queryType  = VK_QUERY_TYPE_TIMESTAMP;
    qp.queryCount = ZoneRegistry::kMaxZones * 2 * kRings;
    if (vkCreateQueryPool(device, &qp, nullptr, &pool_) != VK_SUCCESS) {
        spdlog::warn("[gpu-zones] failed to create the timestamp query pool");
        supported_ = false;
        return false;
    }
    spdlog::info("[gpu-zones] ready: up to {} zones, {} frames deep, {:.1f} ns/tick",
                 ZoneRegistry::kMaxZones, kRings, period_ns_);
    return true;
}

void GpuZones::shutdown(VkDevice device) {
    if (pool_ != VK_NULL_HANDLE) {
        vkDestroyQueryPool(device, pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }
    supported_ = false;
}

void GpuZones::begin_frame(VkCommandBuffer cmd, uint64_t frame_index) {
    zones_.begin_frame();
    open_zone_ = ZoneRegistry::kInvalid;
    if (!supported_ || cmd == VK_NULL_HANDLE) return;

    frame_index_ = frame_index;
    write_ring_  = static_cast<uint32_t>(frame_index % kRings);

    // Reset the whole ring, not just the zones used last frame: a slot left
    // unreset from an older frame would return a stale result that looks valid.
    vkCmdResetQueryPool(cmd, pool_,
                        write_ring_ * ZoneRegistry::kMaxZones * 2,
                        ZoneRegistry::kMaxZones * 2);
    ring_written_[write_ring_] = true;
    for (uint32_t i = 0; i < ZoneRegistry::kMaxZones; ++i) ran_in_ring_[write_ring_][i] = false;
}

void GpuZones::begin_zone(VkCommandBuffer cmd, const std::string& name) {
    if (!supported_ || cmd == VK_NULL_HANDLE) return;
    if (open_zone_ != ZoneRegistry::kInvalid) {
        // Overlapping zones would write two begins before an end and silently
        // mis-attribute both. Say so rather than produce a plausible number.
        spdlog::warn("[gpu-zones] '{}' opened while '{}' is still open - ignored",
                     name, zones_.name_at(open_zone_));
        return;
    }
    const uint32_t idx = zones_.index_of(name);
    if (idx == ZoneRegistry::kInvalid) {
        spdlog::warn("[gpu-zones] zone table full ({}) - '{}' is not being timed",
                     ZoneRegistry::kMaxZones, name);
        return;
    }
    open_zone_ = idx;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        pool_, begin_slot(idx, write_ring_));
}

void GpuZones::end_zone(VkCommandBuffer cmd) {
    if (!supported_ || cmd == VK_NULL_HANDLE) return;
    if (open_zone_ == ZoneRegistry::kInvalid) return;   // unbalanced end; ignore
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        pool_, end_slot(open_zone_, write_ring_));
    zones_.mark_ran(open_zone_);
    ran_in_ring_[write_ring_][open_zone_] = true;
    open_zone_ = ZoneRegistry::kInvalid;
}

void GpuZones::end_frame(VkDevice device) {
    if (!supported_ || pool_ == VK_NULL_HANDLE) return;
    if (open_zone_ != ZoneRegistry::kInvalid) {
        spdlog::warn("[gpu-zones] frame ended with '{}' still open",
                     zones_.name_at(open_zone_));
        open_zone_ = ZoneRegistry::kInvalid;
    }

    // Probe from the NEWEST ring backwards for one whose results are actually
    // available. Which frame the caller's fence belongs to depends on how many
    // frames the app keeps in flight, which this class does not know; guessing
    // wrong means always reading a ring that is still executing and therefore
    // never reporting anything at all. A handful of non-blocking queries is
    // correct for any frames-in-flight count up to kRings.
    uint32_t read_ring = write_ring_;
    bool     found     = false;
    for (uint32_t back = 0; back < kRings && !found; ++back) {
        const uint32_t ring = (write_ring_ + kRings - back) % kRings;
        if (!ring_written_[ring]) continue;
        for (uint32_t z = 0; z < zones_.count() && !found; ++z) {
            if (!ran_in_ring_[ring][z]) continue;
            uint64_t probe[2] = {0, 0};
            if (vkGetQueryPoolResults(device, pool_, begin_slot(z, ring), 2,
                                      sizeof(probe), probe, sizeof(uint64_t),
                                      VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
                read_ring = ring;
                found     = true;
            }
        }
    }
    if (!found) return;   // nothing complete yet; leave the last frame's numbers up

    std::vector<std::pair<std::string, float>> out;
    out.reserve(zones_.count());

    for (uint32_t z = 0; z < zones_.count(); ++z) {
        // Did not run in that frame: report 0. This is the whole point -- an
        // effect the user just switched off must stop showing a cost.
        if (!ran_in_ring_[read_ring][z]) {
            last_ms_[z] = 0.0f;
            out.emplace_back(zones_.name_at(z), 0.0f);
            continue;
        }
        uint64_t ts[2] = {0, 0};
        const VkResult vr = vkGetQueryPoolResults(
            device, pool_, begin_slot(z, read_ring), 2,
            sizeof(ts), ts, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
        if (vr == VK_SUCCESS && ts[1] > ts[0]) {
            const double ns = static_cast<double>(ts[1] - ts[0]) * period_ns_;
            last_ms_[z] = static_cast<float>(ns / 1.0e6);
        }
        // Ran but the result is not back: keep the previous value rather than
        // flickering to zero.
        out.emplace_back(zones_.name_at(z), last_ms_[z]);
    }

    GPUProfiler::instance().submit_frame(out);
}
#endif  // !GWS_ZONES_NO_VULKAN

}  // namespace engine::vulkan
