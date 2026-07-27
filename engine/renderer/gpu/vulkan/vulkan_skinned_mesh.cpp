// ============================================================================
// vulkan_skinned_mesh.cpp — GPU compute skinning. See vulkan_skinned_mesh.h.
// ============================================================================

#include "vulkan_skinned_mesh.h"
#include "vulkan_device.h"
#include "skinning_spirv.h"

#include <spdlog/spdlog.h>
#include <array>
#include <cstring>

namespace gws::renderer::gpu {

namespace {

// Must match skinning.comp's std430 InVertex (5 x vec4 = 80 bytes).
struct GpuSkinnedVertex {
    glm::vec4  pos_uvx;   // xyz position, w uv.x
    glm::vec4  nrm_uvy;   // xyz normal,   w uv.y
    glm::vec4  tangent;   // xyzw
    glm::uvec4 joints;
    glm::vec4  weights;
};
static_assert(sizeof(GpuSkinnedVertex) == 80, "GpuSkinnedVertex must match std430");

struct SkinPush { uint32_t vertex_count; uint32_t bone_count; };

bool make_host_buffer(VulkanDevice* device, VkBufferUsageFlags usage,
                      VkDeviceSize size, VkBuffer& buf, VkDeviceMemory& mem) {
    VkDevice vk = device->get_device();
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size  = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vk, &bi, nullptr, &buf) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(vk, buf, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = device->find_memory_type(
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(vk, &ai, nullptr, &mem) != VK_SUCCESS) {
        vkDestroyBuffer(vk, buf, nullptr); buf = VK_NULL_HANDLE; return false;
    }
    vkBindBufferMemory(vk, buf, mem, 0);
    return true;
}

} // namespace

std::unique_ptr<SkinnedMesh> SkinnedMesh::create(
    VulkanDevice* device, const std::vector<SkinnedVertex>& vertices,
    const std::vector<uint32_t>& indices, std::vector<Submesh> submeshes,
    std::vector<glm::mat4> inverse_bind_matrices) {
    if (!device || vertices.empty() || indices.empty() || inverse_bind_matrices.empty()) {
        spdlog::error("SkinnedMesh::create: invalid args (verts={}, idx={}, bones={})",
                      vertices.size(), indices.size(), inverse_bind_matrices.size());
        return nullptr;
    }
    auto out = std::unique_ptr<SkinnedMesh>(new SkinnedMesh());
    out->device_        = device;
    out->inverse_bind_  = std::move(inverse_bind_matrices);
    out->vertex_count_  = static_cast<uint32_t>(vertices.size());
    out->bone_count_    = static_cast<uint32_t>(out->inverse_bind_.size());
    if (!out->init(device, vertices, indices, std::move(submeshes))) return nullptr;
    spdlog::info("SkinnedMesh created ({} verts, {} bones)", out->vertex_count_, out->bone_count_);
    return out;
}

bool SkinnedMesh::init(VulkanDevice* device, const std::vector<SkinnedVertex>& vertices,
                       const std::vector<uint32_t>& indices, std::vector<Submesh> submeshes) {
    VkDevice vk = device->get_device();

    // 1) Output Mesh from the bind pose (bounds/meshlets bake from it). The vbo
    //    gets STORAGE usage so the compute pass can write it.
    std::vector<SceneVertex> bind;
    bind.reserve(vertices.size());
    for (const SkinnedVertex& sv : vertices)
        bind.push_back(SceneVertex{sv.position, sv.normal, sv.uv, sv.tangent});
    output_ = Mesh::create(device, bind, indices, std::move(submeshes),
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (!output_) { spdlog::error("SkinnedMesh: output Mesh::create failed"); return false; }

    // 2) Input SSBO (packed std430), uploaded once.
    std::vector<GpuSkinnedVertex> packed(vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i) {
        const SkinnedVertex& s = vertices[i];
        packed[i].pos_uvx = glm::vec4(s.position, s.uv.x);
        packed[i].nrm_uvy = glm::vec4(s.normal, s.uv.y);
        packed[i].tangent = s.tangent;
        packed[i].joints  = s.joints;
        packed[i].weights = s.weights;
    }
    const VkDeviceSize in_size = sizeof(GpuSkinnedVertex) * packed.size();
    if (!make_host_buffer(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, in_size,
                          in_ssbo_, in_mem_)) return false;
    {
        void* p = nullptr;
        vkMapMemory(vk, in_mem_, 0, in_size, 0, &p);
        std::memcpy(p, packed.data(), static_cast<size_t>(in_size));
        vkUnmapMemory(vk, in_mem_);
    }

    // 3) Bone SSBO (skinning matrices), host-visible + persistently mapped.
    const VkDeviceSize bone_size = sizeof(glm::mat4) * bone_count_;
    if (!make_host_buffer(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, bone_size,
                          bone_ssbo_, bone_mem_)) return false;
    if (vkMapMemory(vk, bone_mem_, 0, bone_size, 0, &bone_mapped_) != VK_SUCCESS) return false;
    // Initialise to the inverse-bind's inverse? No — leave identity-ish until
    // first skin(); write identity so a pre-skin draw is at bind pose.
    {
        std::vector<glm::mat4> ident(bone_count_, glm::mat4(1.0f));
        std::memcpy(bone_mapped_, ident.data(), static_cast<size_t>(bone_size));
    }

    // 4) Descriptor set layout (3 storage buffers) + pool + set.
    std::array<VkDescriptorSetLayoutBinding, 3> binds{};
    for (uint32_t i = 0; i < 3; ++i) {
        binds[i].binding = i;
        binds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binds[i].descriptorCount = 1;
        binds[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 3; li.pBindings = binds.data();
    if (vkCreateDescriptorSetLayout(vk, &li, nullptr, &dsl_) != VK_SUCCESS) return false;

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3};
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = 1; pi.poolSizeCount = 1; pi.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(vk, &pi, nullptr, &pool_) != VK_SUCCESS) return false;

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool_; ai.descriptorSetCount = 1; ai.pSetLayouts = &dsl_;
    if (vkAllocateDescriptorSets(vk, &ai, &set_) != VK_SUCCESS) return false;

    VkDescriptorBufferInfo in_bi{in_ssbo_, 0, in_size};
    VkDescriptorBufferInfo bn_bi{bone_ssbo_, 0, bone_size};
    VkDescriptorBufferInfo out_bi{output_->get_vertex_buffer(), 0,
                                  sizeof(SceneVertex) * vertex_count_};
    std::array<VkWriteDescriptorSet, 3> w{};
    const VkDescriptorBufferInfo* infos[3] = {&in_bi, &bn_bi, &out_bi};
    for (uint32_t i = 0; i < 3; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = set_; w[i].dstBinding = i; w[i].descriptorCount = 1;
        w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[i].pBufferInfo = infos[i];
    }
    vkUpdateDescriptorSets(vk, 3, w.data(), 0, nullptr);

    // 5) Compute pipeline.
    VkShaderModuleCreateInfo smi{};
    smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = kSkinningSpv_size; smi.pCode = kSkinningSpv;
    VkShaderModule sm = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vk, &smi, nullptr, &sm) != VK_SUCCESS) return false;

    VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SkinPush)};
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1; pli.pSetLayouts = &dsl_;
    pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(vk, &pli, nullptr, &layout_) != VK_SUCCESS) {
        vkDestroyShaderModule(vk, sm, nullptr); return false;
    }
    VkComputePipelineCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ci.stage.module = sm; ci.stage.pName = "main";
    ci.layout = layout_;
    const VkResult r = vkCreateComputePipelines(vk, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline_);
    vkDestroyShaderModule(vk, sm, nullptr);
    return r == VK_SUCCESS;
}

void SkinnedMesh::skin(VkCommandBuffer cmd, const std::vector<glm::mat4>& bone_global) {
    if (pipeline_ == VK_NULL_HANDLE || bone_global.empty()) return;

    // skinning[j] = boneGlobal[j] * inverseBind[j]  (CPU precompute → upload).
    const uint32_t n = std::min<uint32_t>(bone_count_, static_cast<uint32_t>(bone_global.size()));
    auto* dst = static_cast<glm::mat4*>(bone_mapped_);
    for (uint32_t j = 0; j < n; ++j) dst[j] = bone_global[j] * inverse_bind_[j];
    for (uint32_t j = n; j < bone_count_; ++j) dst[j] = glm::mat4(1.0f);

    const VkBuffer vbo = output_->get_vertex_buffer();
    auto buf_barrier = [&](VkAccessFlags src, VkAccessFlags dst_a,
                           VkPipelineStageFlags ss, VkPipelineStageFlags ds) {
        VkBufferMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        b.srcAccessMask = src; b.dstAccessMask = dst_a;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.buffer = vbo; b.offset = 0; b.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd, ss, ds, 0, 0, nullptr, 1, &b, 0, nullptr);
    };

    // Previous frame's vertex-attribute read must finish before we overwrite.
    buf_barrier(VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0, 1, &set_, 0, nullptr);
    SkinPush pc{vertex_count_, bone_count_};
    vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
    vkCmdDispatch(cmd, (vertex_count_ + 63) / 64, 1, 1);

    // Compute write must be visible to the geometry stage's vertex fetch.
    buf_barrier(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT);
}

SkinnedMesh::~SkinnedMesh() { destroy(); }

void SkinnedMesh::destroy() {
    if (!device_) return;
    VkDevice vk = device_->get_device();
    if (pipeline_) vkDestroyPipeline(vk, pipeline_, nullptr);
    if (layout_)   vkDestroyPipelineLayout(vk, layout_, nullptr);
    if (pool_)     vkDestroyDescriptorPool(vk, pool_, nullptr);
    if (dsl_)      vkDestroyDescriptorSetLayout(vk, dsl_, nullptr);
    if (bone_mapped_) vkUnmapMemory(vk, bone_mem_);
    if (bone_ssbo_) vkDestroyBuffer(vk, bone_ssbo_, nullptr);
    if (bone_mem_)  vkFreeMemory(vk, bone_mem_, nullptr);
    if (in_ssbo_)   vkDestroyBuffer(vk, in_ssbo_, nullptr);
    if (in_mem_)    vkFreeMemory(vk, in_mem_, nullptr);
    output_.reset();
    pipeline_ = VK_NULL_HANDLE; layout_ = VK_NULL_HANDLE; pool_ = VK_NULL_HANDLE;
    dsl_ = VK_NULL_HANDLE; bone_ssbo_ = VK_NULL_HANDLE; bone_mem_ = VK_NULL_HANDLE;
    bone_mapped_ = nullptr; in_ssbo_ = VK_NULL_HANDLE; in_mem_ = VK_NULL_HANDLE;
    device_ = nullptr;
}

} // namespace gws::renderer::gpu
