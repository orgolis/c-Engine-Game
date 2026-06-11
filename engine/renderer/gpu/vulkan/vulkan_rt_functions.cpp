#include "vulkan_rt_functions.h"
#include <spdlog/spdlog.h>

namespace gws::renderer::gpu {

bool VulkanRtFunctions::load(VkDevice device) {
    auto resolve = [&](const char* name) -> PFN_vkVoidFunction {
        PFN_vkVoidFunction f = vkGetDeviceProcAddr(device, name);
        if (f == nullptr) {
            spdlog::error("VulkanRtFunctions: failed to resolve {}", name);
        }
        return f;
    };

    cmdBuildAccelerationStructures =
        reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
            resolve("vkCmdBuildAccelerationStructuresKHR"));
    createAccelerationStructure =
        reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
            resolve("vkCreateAccelerationStructureKHR"));
    destroyAccelerationStructure =
        reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
            resolve("vkDestroyAccelerationStructureKHR"));
    getAccelerationStructureBuildSizes =
        reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
            resolve("vkGetAccelerationStructureBuildSizesKHR"));
    getAccelerationStructureDeviceAddress =
        reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
            resolve("vkGetAccelerationStructureDeviceAddressKHR"));
    getBufferDeviceAddress =
        reinterpret_cast<PFN_vkGetBufferDeviceAddressKHR>(
            resolve("vkGetBufferDeviceAddressKHR"));

    return cmdBuildAccelerationStructures != nullptr &&
           createAccelerationStructure   != nullptr &&
           destroyAccelerationStructure  != nullptr &&
           getAccelerationStructureBuildSizes != nullptr &&
           getAccelerationStructureDeviceAddress != nullptr &&
           getBufferDeviceAddress != nullptr;
}

} // namespace gws::renderer::gpu
