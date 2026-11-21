#pragma once

// ============================================================================
// GPU checkpoints: how far the GPU got before it stopped.
//
// When a frame makes the GPU hang (a shader that does not come back, a
// structure it walks forever) Windows resets the driver after two seconds and
// every Vulkan call answers VK_ERROR_DEVICE_LOST. Which pass was running is
// then unknowable -- unless the command stream carried markers the driver kept.
// NVIDIA's VK_NV_device_diagnostic_checkpoints does exactly that: a marker
// recorded between commands, read back after the loss with the pipeline stage
// it reached.
//
// Off unless GWS_GPU_CHECKPOINTS=1 (and the GPU has the extension): the device
// enables it, every timed GPU zone (gpu_zones.h) records its name going in and
// coming out, and report() lists the last ones the GPU reached.
// ============================================================================

#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace gws::gpu_ckpt {

inline PFN_vkCmdSetCheckpointNV     g_set = nullptr;
inline PFN_vkGetQueueCheckpointDataNV g_get = nullptr;
inline VkQueue                      g_queue = VK_NULL_HANDLE;

/// Asked for (GWS_GPU_CHECKPOINTS=1): the device enables the extension when it has it.
inline bool requested() {
    const char* e = std::getenv("GWS_GPU_CHECKPOINTS");
    return e && e[0] == '1';
}

/// After the device was created with VK_NV_device_diagnostic_checkpoints.
inline void init(VkDevice device, VkQueue queue) {
    g_set = reinterpret_cast<PFN_vkCmdSetCheckpointNV>(vkGetDeviceProcAddr(device, "vkCmdSetCheckpointNV"));
    g_get = reinterpret_cast<PFN_vkGetQueueCheckpointDataNV>(vkGetDeviceProcAddr(device, "vkGetQueueCheckpointDataNV"));
    g_queue = queue;
    if (!g_set || !g_get) g_set = nullptr, g_get = nullptr;
}

inline bool enabled() { return g_set != nullptr; }

/// A name kept for the life of the process: a marker is only a pointer, read
/// back after the loss.
inline const char* intern(const std::string& name) {
    static std::mutex m;
    static std::set<std::string> names;
    std::lock_guard<std::mutex> lock(m);
    return names.insert(name).first->c_str();
}

/// Record `name` (a literal or intern()ed) at this point of the command buffer.
inline void mark(VkCommandBuffer cmd, const char* name) {
    if (g_set && cmd != VK_NULL_HANDLE) g_set(cmd, name);
}

/// After VK_ERROR_DEVICE_LOST: the markers the GPU reached last, with the stage
/// (the driver keeps the last one reached at the top and at the bottom of the
/// pipe: the pass that started and did not finish is between them).
inline std::string report() {
    if (!g_get || g_queue == VK_NULL_HANDLE) return "no GPU checkpoints (run with GWS_GPU_CHECKPOINTS=1 on NVIDIA)";
    uint32_t n = 0;
    g_get(g_queue, &n, nullptr);
    std::vector<VkCheckpointDataNV> data(n);
    for (auto& d : data) d.sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV;
    g_get(g_queue, &n, data.data());
    std::string out;
    for (const auto& d : data) {
        const char* stage = d.stage == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT      ? "top of pipe"
                            : d.stage == VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT ? "bottom of pipe"
                                                                              : "stage";
        out += std::string(stage) + ": " + (d.pCheckpointMarker ? static_cast<const char*>(d.pCheckpointMarker) : "(none)") + "; ";
    }
    return out.empty() ? "no checkpoint reached" : out;
}

}  // namespace gws::gpu_ckpt
