/**
 * @file vulkan_rt_functions.h
 * @brief Function-pointer table for the KHR ray-tracing extensions.
 *
 * Vulkan extension entry points are not statically linked — they must be
 * resolved at runtime via `vkGetDeviceProcAddr` once the logical device
 * is created. This struct holds the resolved pointers and is loaded once
 * by `VulkanDevice` when ray tracing was successfully enabled.
 *
 * Phase 2 of the RT rollout uses these for BLAS + TLAS build operations.
 * Phase 3 adds inline `rayQueryEXT` to the lighting frag (no extra function
 * pointers needed there — `rayQuery` is a shader feature, not a C entry
 * point).
 */
#pragma once

#include <vulkan/vulkan.h>

namespace gws::renderer::gpu {

struct VulkanRtFunctions {
    PFN_vkCmdBuildAccelerationStructuresKHR        cmdBuildAccelerationStructures        = nullptr;
    PFN_vkCreateAccelerationStructureKHR           createAccelerationStructure           = nullptr;
    PFN_vkDestroyAccelerationStructureKHR          destroyAccelerationStructure          = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR    getAccelerationStructureBuildSizes    = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR getAccelerationStructureDeviceAddress = nullptr;
    PFN_vkGetBufferDeviceAddressKHR                getBufferDeviceAddress                = nullptr;

    /// Resolve all entry points. Returns false if any required pointer is
    /// missing (which indicates a driver bug, since we already verified
    /// the extensions are present on the device).
    bool load(VkDevice device);
};

} // namespace gws::renderer::gpu
