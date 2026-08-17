/**
 * @file vulkan_rt_scene.cpp
 * @brief Implementation of the BLAS/TLAS container.
 *
 * Vulkan reference for the build flow:
 *   - Per BLAS: query sizes via vkGetAccelerationStructureBuildSizesKHR,
 *     allocate a buffer of that size with AS_STORAGE usage, create the AS
 *     wrapping that buffer, then vkCmdBuildAccelerationStructuresKHR with
 *     the scratch buffer.
 *   - Per TLAS: same dance, but the geometry is an INSTANCES_KHR pointing
 *     at a buffer of VkAccelerationStructureInstanceKHR records.
 *
 * Synchronisation between BLAS build → TLAS build → ray query is handled
 * by memory barriers with srcAccess = ACCELERATION_STRUCTURE_WRITE,
 * dstAccess = ACCELERATION_STRUCTURE_READ.
 */

#include <chrono>
#include "vulkan_rt_scene.h"
#include "vulkan_device.h"
#include "vulkan_rt_functions.h"
#include "vulkan_scene_mesh.h"
#include "vulkan_scene_material.h"
#include "vulkan_render_graph.h" // DrawItem

#include <spdlog/spdlog.h>
#include <cstring>
#include <cstdint>

namespace gws::renderer::gpu {

namespace {

constexpr VkDeviceSize align(VkDeviceSize size, VkDeviceSize alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

} // namespace

void AccelerationStructure::destroy(VkDevice device, const VulkanRtFunctions* fns) {
    if (handle != VK_NULL_HANDLE && fns != nullptr) {
        fns->destroyAccelerationStructure(device, handle, nullptr);
        handle = VK_NULL_HANDLE;
    }
    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    }
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
    device_address = 0;
}

// ---- Construction ---------------------------------------------------------

std::unique_ptr<VulkanRtScene> VulkanRtScene::create(VulkanDevice* device) {
    if (device == nullptr) return nullptr;
    if (!device->has_ray_tracing()) {
        spdlog::warn("VulkanRtScene::create: device does not support ray tracing");
        return nullptr;
    }
    auto scene = std::unique_ptr<VulkanRtScene>(new VulkanRtScene());
    if (!scene->initialize(device)) return nullptr;
    spdlog::info("VulkanRtScene created");
    return scene;
}

bool VulkanRtScene::initialize(VulkanDevice* device) {
    device_ = device;
    fns_    = device->get_rt_functions();
    if (fns_ == nullptr) return false;
    return true;
}

void VulkanRtScene::clear() {
    if (device_ == nullptr) return;
    VkDevice vk = device_->get_device();

    // Block until any in-flight frame using the current handles has
    // retired. Without this, destroying a BLAS / TLAS that's still
    // referenced by an unfinished command buffer causes a GPU hang.
    vkDeviceWaitIdle(vk);

    for (auto& [_, blas] : blas_cache_) {
        blas.destroy(vk, fns_);
    }
    blas_cache_.clear();
    tlas_.destroy(vk, fns_);
    tlas_capacity_       = 0;
    last_instance_count_ = 0;
    // Keep scratch + instance buffers — they're untyped and will be
    // reused on the next update. They can stay valid across scene loads.

    spdlog::info("VulkanRtScene: cleared (BLAS cache + TLAS)");
}

VulkanRtScene::~VulkanRtScene() {
    if (device_ == nullptr) return;
    VkDevice vk = device_->get_device();

    for (auto& [_, blas] : blas_cache_) {
        blas.destroy(vk, fns_);
    }
    blas_cache_.clear();
    tlas_.destroy(vk, fns_);

    if (instance_mapped_ != nullptr && instance_memory_ != VK_NULL_HANDLE) {
        vkUnmapMemory(vk, instance_memory_);
        instance_mapped_ = nullptr;
    }
    destroy_buffer(instance_buffer_, instance_memory_);
    if (instance_data_mapped_ != nullptr && instance_data_memory_ != VK_NULL_HANDLE) {
        vkUnmapMemory(vk, instance_data_memory_);
        instance_data_mapped_ = nullptr;
    }
    destroy_buffer(instance_data_buffer_, instance_data_memory_);
    destroy_buffer(scratch_buffer_, scratch_memory_);
}

// ---- Buffer helpers -------------------------------------------------------

bool VulkanRtScene::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                   VkMemoryPropertyFlags props,
                                   VkBuffer& out_buffer,
                                   VkDeviceMemory& out_memory) const {
    VkDevice vk = device_->get_device();
    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = size;
    bi.usage       = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vk, &bi, nullptr, &out_buffer) != VK_SUCCESS) return false;

    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(vk, out_buffer, &mr);

    // Buffers that participate in AS builds / instance records / scratch
    // need device addresses; that requires the device-address allocation
    // flag in addition to the standard memory type selection.
    VkMemoryAllocateFlagsInfo flags_info{};
    flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flags_info.flags = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
                          ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
                          : 0;

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.pNext           = &flags_info;
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = device_->find_memory_type(mr.memoryTypeBits, props);
    if (vkAllocateMemory(vk, &ai, nullptr, &out_memory) != VK_SUCCESS) return false;
    vkBindBufferMemory(vk, out_buffer, out_memory, 0);
    return true;
}

VkDeviceAddress VulkanRtScene::buffer_device_address(VkBuffer buffer) const {
    VkBufferDeviceAddressInfo info{};
    info.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = buffer;
    return fns_->getBufferDeviceAddress(device_->get_device(), &info);
}

void VulkanRtScene::destroy_buffer(VkBuffer& buffer, VkDeviceMemory& memory) const {
    VkDevice vk = device_->get_device();
    if (buffer != VK_NULL_HANDLE) { vkDestroyBuffer(vk, buffer, nullptr); buffer = VK_NULL_HANDLE; }
    if (memory != VK_NULL_HANDLE) { vkFreeMemory(vk, memory, nullptr); memory = VK_NULL_HANDLE; }
}

bool VulkanRtScene::ensure_scratch(VkDeviceSize size) {
    if (size <= scratch_capacity_) return true;
    // RETIRE, do not destroy. Builds already recorded into this frame's command
    // buffer still hold this buffer's device address; freeing it now means they
    // write into freed memory when the frame executes.
    if (scratch_buffer_ != VK_NULL_HANDLE) {
        retired_scratch_.push_back(RetiredBuffer{scratch_buffer_, scratch_memory_, rt_frame_});
        scratch_buffer_ = VK_NULL_HANDLE;
        scratch_memory_ = VK_NULL_HANDLE;
    }
    // Add headroom so we don't reallocate on every frame.
    scratch_capacity_ = align(size * 2, 256);
    if (!create_buffer(scratch_capacity_,
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       scratch_buffer_, scratch_memory_)) {
        return false;
    }
    scratch_address_ = buffer_device_address(scratch_buffer_);
    return true;
}

// ---- BLAS construction ----------------------------------------------------

bool VulkanRtScene::ensure_blas(VkCommandBuffer cmd, const Mesh* mesh) {
    if (mesh == nullptr) return false;
    const VkBuffer current_vbo = mesh->get_vertex_buffer();

    // Cache lookup with stale-entry detection. A cached entry whose
    // `source_vbo` no longer matches the mesh's current vbo handle has
    // been invalidated by mesh destruction — the Mesh* pointer was reused
    // by the allocator. Drop it and rebuild.
    auto it = blas_cache_.find(mesh);
    if (it != blas_cache_.end()) {
        if (it->second.source_vbo == current_vbo) {
            return true;
        }
        // Stale entry — wait for the GPU to drain before destroying the
        // old BLAS, since the previous frame's lighting pass may still be
        // using it via the TLAS. Without this, we'd destroy a live AS and
        // hang on the next ray query.
        vkDeviceWaitIdle(device_->get_device());
        it->second.destroy(device_->get_device(), fns_);
        blas_cache_.erase(it);
    }

    VkDevice vk = device_->get_device();
    if (current_vbo == VK_NULL_HANDLE || mesh->get_index_buffer() == VK_NULL_HANDLE) {
        spdlog::warn("VulkanRtScene::ensure_blas: mesh has null vertex/index buffer; skipping");
        return false;
    }
    const VkDeviceAddress vbo_addr = buffer_device_address(current_vbo);
    const VkDeviceAddress ibo_addr = buffer_device_address(mesh->get_index_buffer());
    if (vbo_addr == 0 || ibo_addr == 0) {
        // A zero device address indicates the buffer was created without
        // VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT — feeding that into a
        // BLAS build produces a GPU hang. Log and skip; the mesh just
        // won't cast RT shadows.
        spdlog::error(
            "VulkanRtScene::ensure_blas: mesh buffer missing device-address usage "
            "(vbo_addr=0x{:x} ibo_addr=0x{:x}). Skipping BLAS build.",
            vbo_addr, ibo_addr);
        return false;
    }
    const uint32_t triangle_count = mesh->index_count() / 3;
    if (triangle_count == 0) {
        spdlog::warn("VulkanRtScene::ensure_blas: mesh has 0 triangles; skipping");
        return false;
    }
    spdlog::debug("VulkanRtScene: building BLAS for mesh with {} tri", triangle_count);

    // Geometry description — a single triangle geometry per BLAS. Stride
    // must match SceneVertex exactly (pos+normal+uv+tangent = 12 floats);
    // any mismatch makes the builder walk past valid data into random
    // memory and either produce garbage geometry or hang.
    VkAccelerationStructureGeometryTrianglesDataKHR tri{};
    tri.sType                       = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    tri.vertexFormat                = VK_FORMAT_R32G32B32_SFLOAT;
    tri.vertexData.deviceAddress    = vbo_addr;
    tri.vertexStride                = sizeof(SceneVertex);
    tri.maxVertex                   = mesh->vertex_count() - 1;
    tri.indexType                   = VK_INDEX_TYPE_UINT32;
    tri.indexData.deviceAddress     = ibo_addr;

    VkAccelerationStructureGeometryKHR geom{};
    geom.sType                       = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom.geometryType                = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geom.geometry.triangles          = tri;
    geom.flags                       = VK_GEOMETRY_OPAQUE_BIT_KHR;

    VkAccelerationStructureBuildGeometryInfoKHR build{};
    build.sType                      = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type                       = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build.flags                      = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.mode                       = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount              = 1;
    build.pGeometries                = &geom;

    VkAccelerationStructureBuildSizesInfoKHR size_info{};
    size_info.sType                  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    fns_->getAccelerationStructureBuildSizes(
        vk, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &build, &triangle_count, &size_info);

    AccelerationStructure blas{};
    if (!create_buffer(size_info.accelerationStructureSize,
                       VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       blas.buffer, blas.memory)) {
        spdlog::error("VulkanRtScene: BLAS buffer allocation failed"); return false;
    }
    VkAccelerationStructureCreateInfoKHR ci{};
    ci.sType   = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    ci.buffer  = blas.buffer;
    ci.size    = size_info.accelerationStructureSize;
    ci.type    = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    if (fns_->createAccelerationStructure(vk, &ci, nullptr, &blas.handle) != VK_SUCCESS) {
        spdlog::error("VulkanRtScene: createAccelerationStructure(BLAS) failed");
        blas.destroy(vk, fns_);
        return false;
    }

    if (!ensure_scratch(size_info.buildScratchSize)) { blas.destroy(vk, fns_); return false; }

    build.dstAccelerationStructure   = blas.handle;
    build.scratchData.deviceAddress  = scratch_address_;

    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = triangle_count;
    const VkAccelerationStructureBuildRangeInfoKHR* p_range = &range;
    fns_->cmdBuildAccelerationStructures(cmd, 1, &build, &p_range);

    // Make this BLAS visible to the upcoming TLAS build.
    VkMemoryBarrier mb{};
    mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         0, 1, &mb, 0, nullptr, 0, nullptr);

    VkAccelerationStructureDeviceAddressInfoKHR addr_info{};
    addr_info.sType                = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addr_info.accelerationStructure = blas.handle;
    blas.device_address = fns_->getAccelerationStructureDeviceAddress(vk, &addr_info);
    blas.source_vbo     = current_vbo;

    blas_cache_.emplace(mesh, std::move(blas));
    return true;
}

// ---- TLAS construction ----------------------------------------------------

void VulkanRtScene::rebuild_tlas(VkCommandBuffer cmd, const DrawItem* items, size_t count) {
    VkDevice vk = device_->get_device();
    last_instance_count_ = 0;

    // Always build a TLAS — even with 0 instances. An empty TLAS is a
    // valid acceleration structure that ray queries simply miss against.
    // This keeps the lighting pass's binding-13 descriptor pointing at a
    // live handle at all times (e.g. after File→New Scene produces an
    // empty scene). A dangling AS descriptor — even one the shader never
    // reads because rtShadowEnabled is 0 — can hang some drivers.

    // Ensure the instance buffer can hold N records (min 1 so the buffer
    // is always valid even for an empty scene). Each instance is
    // VkAccelerationStructureInstanceKHR (64 bytes).
    const size_t       alloc_count = count > 0 ? count : 1;
    const VkDeviceSize required = sizeof(VkAccelerationStructureInstanceKHR) * alloc_count;
    if (required > instance_capacity_) {
        if (instance_mapped_ != nullptr) {
            vkUnmapMemory(vk, instance_memory_);
            instance_mapped_ = nullptr;
        }
        destroy_buffer(instance_buffer_, instance_memory_);
        instance_capacity_ = align(required * 2, 256);
        if (!create_buffer(instance_capacity_,
                           VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           instance_buffer_, instance_memory_)) {
            spdlog::error("VulkanRtScene: instance buffer allocation failed"); return;
        }
        vkMapMemory(vk, instance_memory_, 0, instance_capacity_, 0, &instance_mapped_);
    }

    // Fill the instance records.
    auto* dst = static_cast<VkAccelerationStructureInstanceKHR*>(instance_mapped_);
    uint32_t written = 0;
    for (size_t i = 0; i < count; ++i) {
        const DrawItem& d = items[i];
        if (d.mesh == nullptr) continue;
        auto it = blas_cache_.find(d.mesh);
        if (it == blas_cache_.end()) continue;

        VkAccelerationStructureInstanceKHR& inst = dst[written];
        // 3x4 row-major transform: write the model matrix's top three rows.
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 4; ++col) {
                inst.transform.matrix[row][col] = d.model[col][row];
            }
        }
        inst.instanceCustomIndex                    = static_cast<uint32_t>(i);
        inst.mask                                   = 0xFF;
        inst.instanceShaderBindingTableRecordOffset = 0;
        inst.flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        inst.accelerationStructureReference         = it->second.device_address;
        ++written;
    }
    last_instance_count_ = written;
    // NOTE: do NOT early-out on written == 0 — we still build an empty
    // (but valid) TLAS so the descriptor binding never dangles.

    const VkDeviceAddress inst_addr = buffer_device_address(instance_buffer_);

    VkAccelerationStructureGeometryInstancesDataKHR insts{};
    insts.sType                  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    insts.data.deviceAddress     = inst_addr;

    VkAccelerationStructureGeometryKHR geom{};
    geom.sType                   = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom.geometryType            = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geom.geometry.instances      = insts;

    VkAccelerationStructureBuildGeometryInfoKHR build{};
    build.sType                  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type                   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build.flags                  = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.mode                   = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount          = 1;
    build.pGeometries            = &geom;

    VkAccelerationStructureBuildSizesInfoKHR size_info{};
    size_info.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    fns_->getAccelerationStructureBuildSizes(
        vk, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &build, &written, &size_info);

    // Only destroy + recreate the TLAS if the required size exceeds our
    // current allocation. Steady-state frames reuse the same buffer +
    // handle, which is both fast and safe (no use-after-free races with
    // descriptor sets still pointing at the old handle).
    if (tlas_.handle == VK_NULL_HANDLE ||
        size_info.accelerationStructureSize > tlas_capacity_) {
        tlas_.destroy(vk, fns_);
        // Allocate with 2x headroom so we don't reallocate on every
        // small instance-count change. Matches the scratch/instance
        // buffer growth policy.
        tlas_capacity_ = align(size_info.accelerationStructureSize * 2, 256);
        if (!create_buffer(tlas_capacity_,
                           VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           tlas_.buffer, tlas_.memory)) {
            spdlog::error("VulkanRtScene: TLAS buffer allocation failed"); return;
        }
        VkAccelerationStructureCreateInfoKHR ci{};
        ci.sType   = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        ci.buffer  = tlas_.buffer;
        // The FULL capacity, not this frame's required size.
        //
        // The reuse test above asks whether the new build fits in
        // `tlas_capacity_` — the buffer's size, allocated with 2x headroom so a
        // small change in instance count does not reallocate. But the
        // acceleration STRUCTURE used to be created with only the size the
        // instance count needed at that moment, so the headroom was never
        // usable: as soon as the scene gained instances the build overflowed a
        // structure the driver had been told was smaller. That writes past the
        // structure's declared size, which is memory corruption, which loses
        // the device — a white window and no crash.
        //
        // It surfaces while a project loads, because meshes stream in
        // asynchronously and every one that arrives adds TLAS instances after
        // the first build has already sized the structure.
        ci.size    = tlas_capacity_;
        ci.type    = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        if (fns_->createAccelerationStructure(vk, &ci, nullptr, &tlas_.handle) != VK_SUCCESS) {
            spdlog::error("VulkanRtScene: createAccelerationStructure(TLAS) failed");
            return;
        }
        VkAccelerationStructureDeviceAddressInfoKHR addr_info{};
        addr_info.sType                = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        addr_info.accelerationStructure = tlas_.handle;
        tlas_.device_address = fns_->getAccelerationStructureDeviceAddress(vk, &addr_info);
    }

    if (!ensure_scratch(size_info.buildScratchSize)) return;

    build.dstAccelerationStructure  = tlas_.handle;
    build.scratchData.deviceAddress = scratch_address_;

    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = written;
    const VkAccelerationStructureBuildRangeInfoKHR* p_range = &range;
    fns_->cmdBuildAccelerationStructures(cmd, 1, &build, &p_range);

    // Make the TLAS visible to subsequent ray queries in fragment / compute.
    VkMemoryBarrier mb{};
    mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &mb, 0, nullptr, 0, nullptr);
}

// ---- Public entry ---------------------------------------------------------

namespace {
// Matches the std430 layout of RtInstanceData in ssr_rt.comp. 64 bytes.
struct RtInstanceDataGpu {
    uint64_t vbo_addr;
    uint64_t ibo_addr;
    float    base_color[4];
    float    metallic;
    float    roughness;
    float    pad0, pad1;
    float    emissive[4];
};
static_assert(sizeof(RtInstanceDataGpu) == 64, "RtInstanceData layout drift");
} // namespace

void VulkanRtScene::update(VkCommandBuffer cmd, const DrawItem* items, size_t count) {
    if (device_ == nullptr || cmd == VK_NULL_HANDLE) return;

    // Reclaim scratch buffers retired by a mid-frame growth, once no frame that
    // could reference them is still in flight. The grace is generous because a
    // scratch buffer is a few hundred KB and a use-after-free here costs the
    // whole device.
    ++rt_frame_;
    constexpr uint32_t kScratchRetireFrames = 8;
    if (!retired_scratch_.empty() && rt_frame_ > kScratchRetireFrames) {
        const uint32_t cutoff = rt_frame_ - kScratchRetireFrames;
        for (auto it = retired_scratch_.begin(); it != retired_scratch_.end();) {
            if (it->frame <= cutoff) {
                destroy_buffer(it->buffer, it->memory);
                it = retired_scratch_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Build BLAS for any meshes we haven't seen yet.
    for (size_t i = 0; i < count; ++i) {
        ensure_blas(cmd, items[i].mesh);
    }

    // Rebuild the TLAS with this frame's instance list.
    rebuild_tlas(cmd, items, count);

    // Build the per-instance shading data SSBO (indexed by DrawItem index =
    // instanceCustomIndex). The RT reflection pass reads this to re-shade
    // hit points without depending on screen-space colour.
    if (count > 0) {
        VkDevice vk = device_->get_device();
        const VkDeviceSize required = sizeof(RtInstanceDataGpu) * count;
        if (required > instance_data_capacity_) {
            if (instance_data_mapped_ != nullptr) {
                vkUnmapMemory(vk, instance_data_memory_);
                instance_data_mapped_ = nullptr;
            }
            destroy_buffer(instance_data_buffer_, instance_data_memory_);
            instance_data_capacity_ = align(required * 2, 256);
            if (create_buffer(instance_data_capacity_,
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              instance_data_buffer_, instance_data_memory_)) {
                vkMapMemory(vk, instance_data_memory_, 0, instance_data_capacity_,
                            0, &instance_data_mapped_);
            }
        }
        if (instance_data_mapped_ != nullptr) {
            auto* dst = static_cast<RtInstanceDataGpu*>(instance_data_mapped_);
            for (size_t i = 0; i < count; ++i) {
                RtInstanceDataGpu d{};
                const DrawItem& it = items[i];
                if (it.mesh != nullptr) {
                    d.vbo_addr = buffer_device_address(it.mesh->get_vertex_buffer());
                    d.ibo_addr = buffer_device_address(it.mesh->get_index_buffer());
                }
                if (it.material != nullptr) {
                    const auto& p = it.material->params();
                    d.base_color[0] = p.base_color_factor.x;
                    d.base_color[1] = p.base_color_factor.y;
                    d.base_color[2] = p.base_color_factor.z;
                    d.base_color[3] = p.base_color_factor.w;
                    d.metallic      = p.metallic_factor;
                    d.roughness     = p.roughness_factor;
                    d.emissive[0]   = p.emissive_factor.x;
                    d.emissive[1]   = p.emissive_factor.y;
                    d.emissive[2]   = p.emissive_factor.z;
                } else {
                    d.base_color[0] = d.base_color[1] = d.base_color[2] = d.base_color[3] = 1.0f;
                }
                dst[i] = d;
            }
        }
    }
}

} // namespace gws::renderer::gpu
