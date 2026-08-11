/**
 * @file vulkan_rt_scene.h
 * @brief Hardware ray-tracing scene container — owns BLAS + TLAS.
 *
 * Phase 2 of the RT rollout. This class is the thin layer between the
 * editor's per-frame `DrawItem` list and the Vulkan acceleration-structure
 * APIs. It:
 *
 *   1. Maintains a per-mesh BLAS cache. The first time a `Mesh*` is seen,
 *      a BLAS is built from its LOD-0 vertex/index data and stored.
 *   2. Each `update()` call rebuilds the TLAS from the supplied draw
 *      items. One instance per draw item, referencing the cached BLAS for
 *      that draw's mesh with the draw's world-space transform.
 *
 * Phase 3 will read the TLAS device address in a shader-side ray query for
 * shadows. Phase 4 will do the same for AO.
 */
#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace gws::renderer::gpu {

class VulkanDevice;
class Mesh;
struct DrawItem;
struct VulkanRtFunctions;

/// Owned buffer + AS handle + device address. One per BLAS, one per TLAS.
struct AccelerationStructure {
    VkAccelerationStructureKHR handle  = VK_NULL_HANDLE;
    VkBuffer                   buffer  = VK_NULL_HANDLE;
    VkDeviceMemory             memory  = VK_NULL_HANDLE;
    VkDeviceAddress            device_address = 0;
    // The source VBO this BLAS was built from. Used to validate cache
    // entries: if a Mesh* key gets reused (an old mesh destroyed and a
    // new mesh allocated at the same address), the cached BLAS's source
    // VBO will no longer match the mesh's current VBO and the entry is
    // rebuilt instead of returned as a stale hit.
    VkBuffer                   source_vbo = VK_NULL_HANDLE;

    void destroy(VkDevice device, const VulkanRtFunctions* fns);
};

class VulkanRtScene {
public:
    /// Construct. Returns nullptr if RT is not supported on the device.
    static std::unique_ptr<VulkanRtScene> create(VulkanDevice* device);

    VulkanRtScene() = default;
    ~VulkanRtScene();
    VulkanRtScene(const VulkanRtScene&) = delete;
    VulkanRtScene& operator=(const VulkanRtScene&) = delete;

    /// Per-frame: ensure each draw item's mesh has a BLAS, then rebuild
    /// the TLAS with one instance per draw item. The TLAS becomes valid
    /// for ray queries after this call (synchronised against the
    /// downstream lighting pass via the standard render-graph barriers).
    ///
    /// @param cmd   Command buffer to record build commands into.
    /// @param items Pointer to the draw item array.
    /// @param count Number of draw items.
    void update(VkCommandBuffer cmd, const DrawItem* items, size_t count);

    /// Drop the per-mesh BLAS cache and the current TLAS. Call this when
    /// the editor swaps scenes (or otherwise destroys all the `Mesh*`
    /// objects that may be keyed into the cache) so the next `update()`
    /// rebuilds from clean state. Waits for device idle internally to
    /// guarantee no in-flight frame is still using the dropped handles.
    void clear();

    /// TLAS device address — passed to the lighting shader so `rayQueryEXT`
    /// can target the right scene. Zero before the first `update()`.
    VkDeviceAddress get_tlas_device_address() const { return tlas_.device_address; }
    VkAccelerationStructureKHR get_tlas_handle() const { return tlas_.handle; }

    /// Number of instances written into the most recent TLAS build.
    uint32_t get_instance_count() const { return last_instance_count_; }

    /// Per-instance shading data SSBO (vbo/ibo addresses + PBR factors),
    /// indexed by the TLAS instanceCustomIndex. Bound by the RT reflection
    /// pass so it can re-shade hit points. Null before the first update().
    VkBuffer get_instance_data_buffer() const { return instance_data_buffer_; }

private:
    bool initialize(VulkanDevice* device);
    bool ensure_blas(VkCommandBuffer cmd, const Mesh* mesh);
    void rebuild_tlas(VkCommandBuffer cmd, const DrawItem* items, size_t count);

    // Helpers — buffer with usage bits and device address allocated.
    bool create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                        VkMemoryPropertyFlags props,
                        VkBuffer& out_buffer, VkDeviceMemory& out_memory) const;
    VkDeviceAddress buffer_device_address(VkBuffer buffer) const;
    void destroy_buffer(VkBuffer& buffer, VkDeviceMemory& memory) const;
    bool ensure_scratch(VkDeviceSize size);

    VulkanDevice*                  device_      = nullptr;
    const VulkanRtFunctions*       fns_         = nullptr;

    // Per-mesh BLAS cache. Key is the raw Mesh* identity — same lifetime
    // contract as the rest of the renderer (caller keeps meshes alive).
    std::unordered_map<const Mesh*, AccelerationStructure> blas_cache_;

    AccelerationStructure tlas_{};
    // Capacity of the currently-allocated TLAS storage buffer. We only
    // destroy + recreate the TLAS when a frame's required size exceeds
    // this, so steady-state frames just refit / rebuild in place.
    VkDeviceSize          tlas_capacity_ = 0;

    // Reused per-frame instance buffer (host-visible, mapped once).
    VkBuffer        instance_buffer_       = VK_NULL_HANDLE;
    VkDeviceMemory  instance_memory_       = VK_NULL_HANDLE;
    void*           instance_mapped_       = nullptr;
    VkDeviceSize    instance_capacity_     = 0;

    // Per-instance shading data SSBO (host-visible), parallel to the TLAS
    // instances. Indexed by DrawItem index = instanceCustomIndex.
    VkBuffer        instance_data_buffer_   = VK_NULL_HANDLE;
    VkDeviceMemory  instance_data_memory_   = VK_NULL_HANDLE;
    void*           instance_data_mapped_   = nullptr;
    VkDeviceSize    instance_data_capacity_ = 0;

    // Reused scratch buffer — grown as needed.
    VkBuffer        scratch_buffer_        = VK_NULL_HANDLE;
    VkDeviceMemory  scratch_memory_        = VK_NULL_HANDLE;
    VkDeviceSize    scratch_capacity_      = 0;
    VkDeviceAddress scratch_address_       = 0;

    uint32_t        last_instance_count_   = 0;
};

} // namespace gws::renderer::gpu
