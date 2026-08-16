/**
 * @file vulkan_scene_mesh.cpp
 * @brief Mesh implementation (DEVICE_LOCAL vertex/index buffers, staged upload).
 *
 * WHY THIS CHANGED. These buffers used to be HOST_VISIBLE | HOST_COHERENT, with
 * a comment saying a staging upload was "the right move (TODO)". On a discrete
 * GPU that TODO is not a nicety: `find_memory_type` returns the FIRST matching
 * type, and on an RTX 3060 the first HOST_VISIBLE|HOST_COHERENT type sits on a
 * heap with no DEVICE_LOCAL bit — system RAM. Every vertex and every index of
 * every mesh was therefore fetched across PCIe by the GPU, on every pass, every
 * frame.
 *
 * Measured cost of that: a resolution-1024 terrain is 49.5 MB of vertices and
 * 24 MB of indices. The G-Buffer pass pulled it, then the shadow pass pulled it
 * again — about 147 MB per frame, ~8.8 GB/s at 60 fps, which saturates a
 * PCIe 3.0 x16 link before a single texture is touched. That was the reported
 * "graphics lag on large high-resolution terrain".
 *
 * Geometry now lives in DEVICE_LOCAL memory and is written once through a
 * staging copy. See MeshUploadBatch for why the copies are batched rather than
 * submitted per mesh.
 */

#include "vulkan_scene_mesh.h"
#include "vulkan_device.h"
#include "culling.h"

#include <meshoptimizer.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <cstdint>

namespace gws::renderer::gpu {

namespace {

/// Create a buffer in DEVICE_LOCAL memory and queue its contents for upload.
///
/// The contents go in through `batch` rather than a map+memcpy, because
/// device-local memory generally is not host-visible. The caller must flush the
/// batch before the buffer is read.
void create_device_buffer(VulkanDevice* device, VkBufferUsageFlags usage,
                          VkDeviceSize size, const void* src,
                          MeshUploadBatch& batch,
                          VkBuffer& out_buf, VkDeviceMemory& out_mem) {
    VkDevice vkdev = device->get_device();

    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = size;
    // TRANSFER_DST: the only way into device-local memory is a copy.
    bi.usage       = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vkdev, &bi, nullptr, &out_buf) != VK_SUCCESS) {
        throw std::runtime_error("Mesh: vkCreateBuffer failed");
    }

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(vkdev, out_buf, &req);

    // When the buffer will be referenced by device address (RT path uses
    // buffer_reference to fetch vertices/indices in the reflection
    // shader), the backing memory MUST be allocated with the
    // device-address flag — otherwise getBufferDeviceAddress is invalid
    // and the shader dereference hangs the GPU.
    VkMemoryAllocateFlagsInfo flags_info{};
    flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flags_info.flags = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
                          ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
                          : 0;

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.pNext           = (flags_info.flags != 0) ? &flags_info : nullptr;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = device->find_memory_type(
        req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vkdev, &ai, nullptr, &out_mem) != VK_SUCCESS) {
        vkDestroyBuffer(vkdev, out_buf, nullptr);
        out_buf = VK_NULL_HANDLE;
        throw std::runtime_error("Mesh: vkAllocateMemory failed");
    }
    vkBindBufferMemory(vkdev, out_buf, out_mem, 0);

    batch.stage(out_buf, src, size);
}

} // namespace

// ============================================================================
// MeshUploadBatch
// ============================================================================

MeshUploadBatch::~MeshUploadBatch() {
    // Flushing here is what makes the scoped use safe: a caller that builds
    // meshes and lets the batch expire gets a correct upload without having to
    // remember. A caller that flushed already pays nothing — flush() is a no-op
    // with an empty queue.
    if (!copies_.empty()) flush();
}

void MeshUploadBatch::stage(VkBuffer dst, const void* src, VkDeviceSize size) {
    if (dst == VK_NULL_HANDLE || src == nullptr || size == 0) return;
    Copy c;
    c.dst    = dst;
    c.offset = static_cast<VkDeviceSize>(blob_.size());
    c.size   = size;
    blob_.resize(blob_.size() + static_cast<size_t>(size));
    std::memcpy(blob_.data() + static_cast<size_t>(c.offset), src,
                static_cast<size_t>(size));
    copies_.push_back(c);
}

bool MeshUploadBatch::flush() {
    if (copies_.empty()) return true;
    if (!device_) { copies_.clear(); blob_.clear(); return false; }

    VkDevice vkdev = device_->get_device();

    // ---- One staging buffer for the whole batch ---------------------------
    VkBuffer       staging     = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    bool ok = true;

    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = static_cast<VkDeviceSize>(blob_.size());
    bi.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vkdev, &bi, nullptr, &staging) != VK_SUCCESS) ok = false;

    if (ok) {
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(vkdev, staging, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = req.size;
        ai.memoryTypeIndex = device_->find_memory_type(
            req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(vkdev, &ai, nullptr, &staging_mem) != VK_SUCCESS) ok = false;
    }
    if (ok) {
        vkBindBufferMemory(vkdev, staging, staging_mem, 0);
        void* mapped = nullptr;
        if (vkMapMemory(vkdev, staging_mem, 0, bi.size, 0, &mapped) == VK_SUCCESS) {
            std::memcpy(mapped, blob_.data(), blob_.size());
            vkUnmapMemory(vkdev, staging_mem);
        } else {
            ok = false;
        }
    }

    // ---- One command buffer, one submit, one wait -------------------------
    if (ok) {
        VkCommandBufferAllocateInfo ca{};
        ca.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ca.commandPool        = device_->get_command_pool();
        ca.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ca.commandBufferCount = 1;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(vkdev, &ca, &cmd) == VK_SUCCESS) {
            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &begin);

            for (const Copy& c : copies_) {
                VkBufferCopy region{};
                region.srcOffset = c.offset;
                region.dstOffset = 0;
                region.size      = c.size;
                vkCmdCopyBuffer(cmd, staging, c.dst, 1, &region);
            }

            // One barrier for the whole batch: every copy must be visible to
            // vertex/index fetch and to acceleration-structure builds before
            // anything reads these buffers.
            VkMemoryBarrier mb{};
            mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            mb.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
                               VK_ACCESS_INDEX_READ_BIT |
                               VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(
                cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                0, 1, &mb, 0, nullptr, 0, nullptr);

            vkEndCommandBuffer(cmd);

            VkSubmitInfo submit{};
            submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers    = &cmd;
            vkQueueSubmit(device_->get_graphics_queue(), 1, &submit, VK_NULL_HANDLE);
            // Waiting once per BATCH is the whole point. The alternative —
            // waiting per mesh — is what made a 256-chunk terrain rebuild
            // unacceptable.
            vkQueueWaitIdle(device_->get_graphics_queue());
            vkFreeCommandBuffers(vkdev, device_->get_command_pool(), 1, &cmd);
        } else {
            ok = false;
        }
    }

    if (!ok) {
        spdlog::error("MeshUploadBatch: staging upload failed ({} copies, {} bytes) — "
                      "meshes built through this batch will render empty",
                      copies_.size(), blob_.size());
    } else {
        spdlog::debug("MeshUploadBatch: uploaded {} buffers, {:.2f} MB, 1 submit",
                      copies_.size(), blob_.size() / 1048576.0);
    }

    if (staging_mem != VK_NULL_HANDLE) vkFreeMemory(vkdev, staging_mem, nullptr);
    if (staging     != VK_NULL_HANDLE) vkDestroyBuffer(vkdev, staging, nullptr);
    copies_.clear();
    blob_.clear();
    return ok;
}

Mesh::~Mesh() { destroy(); }

Mesh::Mesh(Mesh&& other) noexcept
    : device_(other.device_),
      vbo_(other.vbo_), vbo_memory_(other.vbo_memory_),
      ibo_(other.ibo_), ibo_memory_(other.ibo_memory_),
      vertex_count_(other.vertex_count_),
      index_count_(other.index_count_),
      submeshes_(std::move(other.submeshes_)) {
    other.device_     = nullptr;
    other.vbo_        = VK_NULL_HANDLE;
    other.vbo_memory_ = VK_NULL_HANDLE;
    other.ibo_        = VK_NULL_HANDLE;
    other.ibo_memory_ = VK_NULL_HANDLE;
    other.vertex_count_ = 0;
    other.index_count_  = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        destroy();
        device_       = other.device_;
        vbo_          = other.vbo_;
        vbo_memory_   = other.vbo_memory_;
        ibo_          = other.ibo_;
        ibo_memory_   = other.ibo_memory_;
        vertex_count_ = other.vertex_count_;
        index_count_  = other.index_count_;
        submeshes_    = std::move(other.submeshes_);
        other.device_       = nullptr;
        other.vbo_          = VK_NULL_HANDLE;
        other.vbo_memory_   = VK_NULL_HANDLE;
        other.ibo_          = VK_NULL_HANDLE;
        other.ibo_memory_   = VK_NULL_HANDLE;
        other.vertex_count_ = 0;
        other.index_count_  = 0;
    }
    return *this;
}

void Mesh::destroy() {
    if (!device_) return;
    VkDevice vkdev = device_->get_device();
    if (vbo_        != VK_NULL_HANDLE) { vkDestroyBuffer(vkdev, vbo_, nullptr);      vbo_ = VK_NULL_HANDLE; }
    if (vbo_memory_ != VK_NULL_HANDLE) { vkFreeMemory(vkdev, vbo_memory_, nullptr);  vbo_memory_ = VK_NULL_HANDLE; }
    if (ibo_        != VK_NULL_HANDLE) { vkDestroyBuffer(vkdev, ibo_, nullptr);      ibo_ = VK_NULL_HANDLE; }
    if (ibo_memory_ != VK_NULL_HANDLE) { vkFreeMemory(vkdev, ibo_memory_, nullptr);  ibo_memory_ = VK_NULL_HANDLE; }
    device_ = nullptr;
}

std::unique_ptr<Mesh> Mesh::create(VulkanDevice* device,
                                   const std::vector<SceneVertex>& vertices,
                                   const std::vector<uint32_t>& indices,
                                   std::vector<Submesh> submeshes,
                                   VkBufferUsageFlags extra_vbo_usage,
                                   MeshUploadBatch* batch) {
    if (!device || vertices.empty() || indices.empty()) {
        spdlog::error("Mesh::create: invalid args (vertices={}, indices={})",
                      vertices.size(), indices.size());
        return nullptr;
    }

    auto out     = std::unique_ptr<Mesh>(new Mesh());
    out->device_ = device;
    out->vertex_count_ = static_cast<uint32_t>(vertices.size());
    out->index_count_  = static_cast<uint32_t>(indices.size());

    // Compute bounding box from vertex positions.
    {
        glm::vec3 min(1e9f);
        glm::vec3 max(-1e9f);
        for (const auto& v : vertices) {
            min = glm::min(min, v.position);
            max = glm::max(max, v.position);
        }
        out->bounding_box_ = AABB(min, max);
    }

    if (submeshes.empty()) {
        // No submeshes supplied: synthesise a single submesh covering the
        // whole index buffer at a single LOD tier.
        Submesh s{};
        s.material_index = 0;
        MeshLod lod{};
        lod.index_offset = 0;
        lod.index_count  = out->index_count_;
        lod.distance_threshold = 0.0f;
        s.lods.push_back(lod);
        submeshes.push_back(std::move(s));
    } else {
        // Caller supplied submeshes but without LODs: synthesise a single
        // LOD per submesh that covers the whole submesh range. Detect this
        // by an empty `lods` vector and read from the legacy fields if the
        // caller set them via a pre-LOD-aware path. Otherwise leave alone.
        for (auto& s : submeshes) {
            if (s.lods.empty()) {
                MeshLod lod{};
                lod.index_offset = 0;
                lod.index_count  = out->index_count_;
                lod.distance_threshold = 0.0f;
                s.lods.push_back(lod);
            }
        }
    }
    out->submeshes_ = std::move(submeshes);

    // -----------------------------------------------------------------------
    // Meshlet baking — per submesh, for LOD 0 only.
    //
    // meshoptimizer groups triangles into clusters of up to 64 vertices /
    // 124 triangles. We rebuild LOD 0's index range so that each meshlet's
    // triangles are contiguous in the index buffer; at draw time we issue
    // one `vkCmdDrawIndexed` per visible meshlet (after CPU frustum cull).
    //
    // Higher LODs (decimated tiers) aren't meshlet'd — they're already
    // coarse enough that per-cluster culling adds more overhead than
    // benefit, and reordering them would invalidate the LOD index
    // sequence.
    // -----------------------------------------------------------------------
    std::vector<uint32_t> mutable_indices = indices;
    constexpr size_t MAX_MESHLET_VERTICES  = 64;
    constexpr size_t MAX_MESHLET_TRIANGLES = 124;
    constexpr float  CONE_WEIGHT           = 0.5f;
    constexpr size_t MIN_TRIS_TO_BAKE      = 256; // below this, brute draw is faster

    for (auto& sm : out->submeshes_) {
        if (sm.lods.empty()) continue;
        auto& lod0 = sm.lods[0];
        if (lod0.index_count < MIN_TRIS_TO_BAKE * 3) continue;
        if (lod0.index_offset + lod0.index_count > mutable_indices.size()) continue;

        const uint32_t* src   = mutable_indices.data() + lod0.index_offset;
        const size_t   src_n = lod0.index_count;

        const size_t max_meshlets = meshopt_buildMeshletsBound(
            src_n, MAX_MESHLET_VERTICES, MAX_MESHLET_TRIANGLES);
        std::vector<meshopt_Meshlet> raw_meshlets(max_meshlets);
        std::vector<unsigned int>    mlet_verts(max_meshlets * MAX_MESHLET_VERTICES);
        std::vector<unsigned char>   mlet_tris (max_meshlets * MAX_MESHLET_TRIANGLES * 3);

        const size_t actual_count = meshopt_buildMeshlets(
            raw_meshlets.data(), mlet_verts.data(), mlet_tris.data(),
            src, src_n,
            &vertices[0].position.x, vertices.size(), sizeof(SceneVertex),
            MAX_MESHLET_VERTICES, MAX_MESHLET_TRIANGLES, CONE_WEIGHT);
        raw_meshlets.resize(actual_count);

        // Rebuild LOD 0's indices in meshlet order; record per-meshlet ranges
        // and bounds.
        std::vector<uint32_t> rebuilt; rebuilt.reserve(src_n);
        sm.lod0_meshlets.reserve(actual_count);

        for (const auto& rm : raw_meshlets) {
            Meshlet info{};
            info.first_index = lod0.index_offset + static_cast<uint32_t>(rebuilt.size());
            info.index_count = rm.triangle_count * 3;

            for (uint32_t t = 0; t < rm.triangle_count; ++t) {
                for (int v = 0; v < 3; ++v) {
                    const uint8_t  local  = mlet_tris[rm.triangle_offset + t * 3 + v];
                    const uint32_t global = mlet_verts[rm.vertex_offset + local];
                    rebuilt.push_back(global);
                }
            }

            const meshopt_Bounds b = meshopt_computeMeshletBounds(
                &mlet_verts[rm.vertex_offset],
                &mlet_tris[rm.triangle_offset],
                rm.triangle_count,
                &vertices[0].position.x, vertices.size(), sizeof(SceneVertex));
            info.center = glm::vec3(b.center[0], b.center[1], b.center[2]);
            info.radius = b.radius;
            sm.lod0_meshlets.push_back(info);
        }

        // Triangle count should be preserved by meshoptimizer; if it isn't,
        // bail on this submesh to avoid corrupting the index buffer.
        if (rebuilt.size() == src_n) {
            std::copy(rebuilt.begin(), rebuilt.end(),
                      mutable_indices.begin() + lod0.index_offset);
        } else {
            spdlog::warn("Mesh::create: meshlet rebuild changed index count ({} → {}), "
                         "keeping original ordering — meshlet culling disabled for this submesh",
                         src_n, rebuilt.size());
            sm.lod0_meshlets.clear();
        }
    }

    // Add the AS-build + device-address usage bits when the device
    // supports ray tracing — they're harmless on non-RT paths and let
    // VulkanRtScene build a BLAS from the same buffers without copying.
    VkBufferUsageFlags vbo_usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | extra_vbo_usage;
    VkBufferUsageFlags ibo_usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (device->has_ray_tracing()) {
        const VkBufferUsageFlags rt_bits =
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        vbo_usage |= rt_bits;
        ibo_usage |= rt_bits;
    }
    try {
        // Without a caller-supplied batch, use a local one: it flushes in its
        // destructor at the end of this scope, so a one-off caller still gets a
        // fully uploaded mesh by the time create() returns. With a batch, the
        // caller owns the flush and pays for one submit across many meshes.
        MeshUploadBatch local(device);
        MeshUploadBatch& up = batch ? *batch : local;
        create_device_buffer(device, vbo_usage,
                             sizeof(SceneVertex) * vertices.size(),
                             vertices.data(), up,
                             out->vbo_, out->vbo_memory_);
        create_device_buffer(device, ibo_usage,
                             sizeof(uint32_t) * mutable_indices.size(),
                             mutable_indices.data(), up,
                             out->ibo_, out->ibo_memory_);
    } catch (const std::exception& e) {
        spdlog::error("Mesh::create: {}", e.what());
        return nullptr;
    }

    size_t total_meshlets = 0;
    for (const auto& sm : out->submeshes_) total_meshlets += sm.lod0_meshlets.size();
    spdlog::debug("Mesh: {} verts, {} indices, {} submeshes, {} meshlets",
                  out->vertex_count_, out->index_count_,
                  out->submeshes_.size(), total_meshlets);
    return out;
}

void Mesh::bind(VkCommandBuffer cmd) const {
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vbo_, &offset);
    vkCmdBindIndexBuffer(cmd, ibo_, 0, VK_INDEX_TYPE_UINT32);
}

void Mesh::draw_submesh(VkCommandBuffer cmd, size_t submesh_idx,
                        size_t lod_index) const {
    if (submesh_idx >= submeshes_.size()) return;
    const auto& s = submeshes_[submesh_idx];
    if (s.lods.empty()) return;
    const size_t li = std::min(lod_index, s.lods.size() - 1);
    const auto& lod = s.lods[li];
    vkCmdDrawIndexed(cmd, lod.index_count, 1, lod.index_offset, 0, 0);
}

void Mesh::draw_meshlet(VkCommandBuffer cmd, uint32_t first_index,
                        uint32_t index_count) const {
    vkCmdDrawIndexed(cmd, index_count, 1, first_index, 0, 0);
}

size_t Mesh::select_lod(size_t submesh_idx, float camera_distance) const {
    if (submesh_idx >= submeshes_.size()) return 0;
    const auto& s = submeshes_[submesh_idx];
    if (s.lods.size() <= 1) return 0;
    // LODs are stored in increasing distance_threshold order; walk from the
    // back and return the first tier whose threshold this distance clears.
    for (size_t i = s.lods.size(); i-- > 0; ) {
        if (camera_distance >= s.lods[i].distance_threshold) {
            return i;
        }
    }
    return 0;
}

void Mesh::generate_lods(const std::vector<SceneVertex>& vertices,
                         std::vector<uint32_t>& indices,
                         std::vector<Submesh>& submeshes,
                         const std::vector<float>& lod_ratios,
                         const std::vector<float>& lod_distances) {
    if (vertices.empty() || indices.empty() || submeshes.empty()) return;
    if (lod_ratios.size() != lod_distances.size()) {
        spdlog::warn("Mesh::generate_lods: lod_ratios ({}) and lod_distances ({}) size mismatch — skipping",
                     lod_ratios.size(), lod_distances.size());
        return;
    }

    // Snapshot the current submesh layout. We treat each submesh's lods[0] as
    // the source range. Then for each `lod_ratios[i]`, we run meshopt_simplify
    // on that source and append the simplified range to the index buffer.
    const float* vertex_pos_ptr  = &vertices[0].position.x;
    const size_t vertex_stride   = sizeof(SceneVertex);
    const size_t source_vertices = vertices.size();

    for (auto& s : submeshes) {
        if (s.lods.empty()) continue;
        const MeshLod source = s.lods[0];
        if (source.index_count < 3) continue;

        // Take a copy: each iteration of the inner loop calls
        // `indices.insert` which can reallocate and invalidate any pointer
        // into the vector.
        const std::vector<uint32_t> source_indices(
            indices.begin() + source.index_offset,
            indices.begin() + source.index_offset + source.index_count);

        for (size_t li = 0; li < lod_ratios.size(); ++li) {
            const size_t target = std::max<size_t>(
                3, static_cast<size_t>(source.index_count * lod_ratios[li]));
            // meshopt_simplify guarantees the output is a subset of the
            // input topology, so the output indices fit in the same vertex
            // pool as the source.
            std::vector<uint32_t> simplified(source.index_count);
            float result_error = 0.0f;
            const size_t simplified_count = meshopt_simplify(
                simplified.data(),
                source_indices.data(),
                source.index_count,
                vertex_pos_ptr,
                source_vertices,
                vertex_stride,
                target,
                /*target_error=*/1e-2f,
                /*options=*/0,
                &result_error);

            if (simplified_count == 0 || simplified_count == source.index_count) {
                // Nothing to do — meshopt couldn't decimate further.
                spdlog::debug("Mesh::generate_lods: LOD {} ratio {:.2f} stalled at {}/{}",
                              li + 1, lod_ratios[li], simplified_count, source.index_count);
                continue;
            }
            simplified.resize(simplified_count);

            MeshLod lod{};
            lod.index_offset       = static_cast<uint32_t>(indices.size());
            lod.index_count        = static_cast<uint32_t>(simplified_count);
            lod.distance_threshold = lod_distances[li];

            indices.insert(indices.end(), simplified.begin(), simplified.end());
            s.lods.push_back(lod);

            spdlog::debug("Mesh::generate_lods: submesh LOD {} {} → {} idx ({:.0f}%) err={:.4f}",
                          li + 1, source.index_count, simplified_count,
                          100.0f * simplified_count / source.index_count, result_error);
        }
    }
}

VkVertexInputBindingDescription Mesh::vertex_binding() {
    VkVertexInputBindingDescription b{};
    b.binding   = 0;
    b.stride    = sizeof(SceneVertex);
    b.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return b;
}

std::vector<VkVertexInputAttributeDescription> Mesh::vertex_attributes() {
    std::vector<VkVertexInputAttributeDescription> a(4);
    a[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(SceneVertex, position)};
    a[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(SceneVertex, normal)};
    a[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(SceneVertex, uv)};
    a[3] = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SceneVertex, tangent)};
    return a;
}

} // namespace gws::renderer::gpu
