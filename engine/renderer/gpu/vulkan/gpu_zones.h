#pragma once
// ============================================================================
// gpu_zones — name-keyed GPU timestamp zones (performance audit item F2).
//
// WHY THIS REPLACES WHAT WAS HERE. VulkanRenderGraph owned a timestamp query
// pool keyed by RenderGraphStage, so it could time exactly five things:
// Shadow, Geometry, Lighting, Transparent, PostProcess. Those were the only
// two vkCmdWriteTimestamp sites in the whole renderer.
//
// But SSAO, SSR, DDGI, clouds, volumetric light, froxel fog and water are
// dispatched BETWEEN graph stages -- they are compute passes and the graph
// models render-pass stages -- so none of them was timed at all. Worse, the
// profiler's "total GPU time" was the sum of the five stages it was handed, so
// the reported total and the derived GPU-load percentage both systematically
// EXCLUDED the most expensive work in the frame. The overlay could report a
// comfortable total while the GPU was saturated.
//
// That is the ui_viewport incident one layer up: a diagnostic that reports
// confidently and omits the costly path. This file exists so there is ONE
// timestamp system, keyed by name rather than by an enum of render-pass
// stages, that anything can open a zone in.
//
// SPLIT IN TWO ON PURPOSE. ZoneRegistry is the name/slot/liveness bookkeeping
// and has no Vulkan in it, so the part with the interesting failure modes --
// a name that changes slot between frames, a full table aliasing two zones
// onto one slot, a pass that stopped running still reporting its last value --
// is testable headlessly. GpuZones is the thin Vulkan shell around it.
//
// The Vulkan half is opt-OUT (GWS_ZONES_NO_VULKAN), not opt-in: every real
// consumer wants it, and a guard that each consumer had to remember to define
// fails by silently compiling the class away at the call site rather than by
// erroring in the file that owns it.
// ============================================================================

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef GWS_ZONES_NO_VULKAN
#include <vulkan/vulkan.h>
#endif

namespace engine::vulkan {

// ---------------------------------------------------------------------------
// ZoneRegistry — pure bookkeeping. No Vulkan.
// ---------------------------------------------------------------------------
class ZoneRegistry {
public:
    static constexpr uint32_t kMaxZones = 64;
    static constexpr uint32_t kInvalid  = 0xFFFFFFFFu;

    /// Stable slot for `name`, registering it on first sight.
    ///
    /// Stability is the whole contract: a slot that moved between frames would
    /// read another zone's timestamps and report a confident wrong number,
    /// which is the failure this system exists to end. Returns kInvalid when
    /// the table is full -- deliberately NOT wrapping, because wrapping would
    /// alias two zones onto one slot and produce exactly that wrong number
    /// rather than an honest absence.
    uint32_t index_of(const std::string& name);

    /// Look up without registering. kInvalid when unknown.
    uint32_t find(const std::string& name) const;

    uint32_t           count() const { return static_cast<uint32_t>(names_.size()); }
    const std::string& name_at(uint32_t i) const { return names_[i]; }
    bool               full() const { return names_.size() >= kMaxZones; }

    /// Start a frame: every zone reverts to "did not run".
    ///
    /// Without this a pass that stops being dispatched -- an effect the user
    /// just switched off -- keeps reporting its last duration forever, and the
    /// overlay shows time being spent on work that is no longer happening.
    void begin_frame();

    void mark_ran(uint32_t index);
    bool ran(uint32_t index) const;

    /// How many zones ran this frame.
    uint32_t ran_count() const;

private:
    std::vector<std::string>                     names_;
    std::unordered_map<std::string, uint32_t>    index_;
    std::vector<bool>                            ran_;
};

#ifndef GWS_ZONES_NO_VULKAN
// ---------------------------------------------------------------------------
// GpuZones — the Vulkan shell.
// ---------------------------------------------------------------------------
class GpuZones {
public:
    /// Frames of timestamps kept in flight. Results are read from the oldest
    /// ring that has completed, so resolving never blocks the GPU.
    static constexpr uint32_t kRings = 3;

    bool init(VkPhysicalDevice phys, VkDevice device, uint32_t graphics_queue_family);
    void shutdown(VkDevice device);
    bool supported() const { return supported_; }

    /// Reset this frame's slots. MUST be called outside an active render pass
    /// (Vulkan forbids vkCmdResetQueryPool inside one) and before any zone.
    void begin_frame(VkCommandBuffer cmd, uint64_t frame_index);

    /// Open a zone. Zones must not overlap on one command buffer -- this is a
    /// flat timeline, not a stack, because overlapping GPU zones on a single
    /// queue do not have a meaningful nesting to report.
    void begin_zone(VkCommandBuffer cmd, const std::string& name);
    void end_zone(VkCommandBuffer cmd);

    /// Read back whatever ring has completed and push it to GPUProfiler.
    void end_frame(VkDevice device);

    const ZoneRegistry& registry() const { return zones_; }

private:
    uint32_t begin_slot(uint32_t zone, uint32_t ring) const {
        return (ring * ZoneRegistry::kMaxZones + zone) * 2;
    }
    uint32_t end_slot(uint32_t zone, uint32_t ring) const { return begin_slot(zone, ring) + 1; }

    ZoneRegistry zones_;
    // Which zones actually ran in each in-flight ring. Needed to tell two
    // states apart that a query result alone cannot distinguish: a zone that
    // did NOT run (report 0 -- the pass is off) versus one that ran but whose
    // result is not back yet (carry the last value -- an overlay that flickers
    // to zero reads as "the pass vanished").
    bool         ran_in_ring_[kRings][ZoneRegistry::kMaxZones] = {};
    float        last_ms_[ZoneRegistry::kMaxZones]             = {};
    VkQueryPool  pool_              = VK_NULL_HANDLE;
    double       period_ns_         = 0.0;
    bool         supported_         = false;
    uint32_t     write_ring_        = 0;
    uint64_t     frame_index_       = 0;
    uint32_t     open_zone_         = ZoneRegistry::kInvalid;
    bool         ring_written_[kRings] = {};
};

/// A zone that closes itself. The manual pair is easy to leave unbalanced on an
/// early return, and an unbalanced zone silently poisons the next one's slot.
class ScopedGpuZone {
public:
    ScopedGpuZone(GpuZones* z, VkCommandBuffer cmd, const std::string& name)
        : zones_(z), cmd_(cmd) {
        if (zones_ && cmd_ != VK_NULL_HANDLE) zones_->begin_zone(cmd_, name);
    }
    ~ScopedGpuZone() { if (zones_ && cmd_ != VK_NULL_HANDLE) zones_->end_zone(cmd_); }
    ScopedGpuZone(const ScopedGpuZone&)            = delete;
    ScopedGpuZone& operator=(const ScopedGpuZone&) = delete;

private:
    GpuZones*       zones_;
    VkCommandBuffer cmd_;
};
#endif  // !GWS_ZONES_NO_VULKAN

}  // namespace engine::vulkan
