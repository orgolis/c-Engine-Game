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
class Material;
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
    std::vector<DrawItem> casting_;   // this frame's draws without the ones that cast no shadow

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

    // Host-written buffers get one slot per frame in flight, plus one. The CPU
    // records frame N while frame N-1 may still be executing (the renderer
    // waits only for frame N-2's fence), so a single buffer was overwritten
    // under a frame that had not read it yet. A slot is written again three
    // updates later, when the frame that used it has retired.
    static constexpr uint32_t kHostRing = 3;
    struct HostSlot {
        VkBuffer       buffer   = VK_NULL_HANDLE;
        VkDeviceMemory memory   = VK_NULL_HANDLE;
        void*          mapped   = nullptr;
        VkDeviceSize   capacity = 0;
    };
    bool ensure_host_slot(HostSlot& slot, VkDeviceSize size, VkBufferUsageFlags usage);
    void destroy_host_slot(HostSlot& slot);

    // TLAS build input -- read by the build through its device address only,
    // so it can change every frame without touching a descriptor.
    HostSlot instance_ring_[kHostRing];

    // Per-instance shading data SSBO, parallel to the TLAS instances, indexed
    // by DrawItem index = instanceCustomIndex. DEVICE-LOCAL with a stable
    // handle, because the reflection and DDGI passes bind it through
    // descriptor sets that a frame in flight may still be using; each frame's
    // records are written to a staging slot and copied in on the GPU timeline,
    // after the previous frame's reads.
    HostSlot        instance_data_staging_[kHostRing];
    VkBuffer        instance_data_buffer_   = VK_NULL_HANDLE;
    VkDeviceMemory  instance_data_memory_   = VK_NULL_HANDLE;
    VkDeviceSize    instance_data_capacity_ = 0;

    // Reused scratch buffer — grown as needed.
    VkBuffer        scratch_buffer_        = VK_NULL_HANDLE;
    VkDeviceMemory  scratch_memory_        = VK_NULL_HANDLE;
    VkDeviceSize    scratch_capacity_      = 0;
    VkDeviceAddress scratch_address_       = 0;

    /// Scratch buffers replaced by a mid-frame growth, kept alive until the GPU
    /// can no longer be reading them.
    ///
    /// The scratch buffer is shared by every acceleration-structure build in a
    /// frame, and those builds are RECORDED into one command buffer before any
    /// of them executes. Growing the scratch used to destroy the old buffer
    /// immediately — while BLAS builds already recorded in that same command
    /// buffer still referenced its device address. Executing them then wrote to
    /// freed memory.
    ///
    /// It needed two things at once to show: enough meshes that a later build
    /// asked for more scratch than the first had, all within one frame. A scene
    /// with only terrain, or only models, would size the scratch once and never
    /// grow it — which is why this hid until a project had both.
    struct RetiredBuffer {
        VkBuffer       buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        uint32_t       frame  = 0;
    };
    std::vector<RetiredBuffer> retired_scratch_;
    uint32_t                   rt_frame_ = 0;

    uint32_t        last_instance_count_   = 0;

    /// What the TLAS and the instance data were last built from, per draw. A
    /// frame whose list matches it field for field changes nothing the GPU
    /// holds, so update() skips the BLAS lookups, the instance writes, the
    /// TLAS build and the instance-data upload: last frame's structures are
    /// still exact. The vertex buffer is part of it because ensure_blas keys a
    /// rebuild on it (a re-uploaded mesh at a reused address).
    struct BuiltFrom {
        const Mesh*     mesh     = nullptr;
        const Material* material = nullptr;
        VkBuffer        vbo      = VK_NULL_HANDLE;
        glm::mat4       model{1.0f};
    };
    std::vector<BuiltFrom> built_from_;
    bool                   have_built_  = false;
    bool                   last_skipped_ = false;
    bool                   tlas_build_ok_ = false;   // rebuild_tlas reached the build

public:
    /// Did the last update() find nothing changed and skip all its work?
    bool last_update_skipped() const { return last_skipped_; }
};

} // namespace gws::renderer::gpu
