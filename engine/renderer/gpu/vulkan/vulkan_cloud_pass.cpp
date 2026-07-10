/**
 * @file vulkan_cloud_pass.cpp
 * @brief Implementation of the volumetric cloud pass.
 */

#include "vulkan_cloud_pass.h"
#include "vulkan_device.h"
#include "vulkan_g_buffer.h"
#include "cloud_spirv.h"

#include <spdlog/spdlog.h>
#include <array>
#include <cstring>

namespace gws::renderer::gpu {

namespace {
constexpr VkFormat kCloudFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kNoiseFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr uint32_t kShapeRes = 128;
constexpr uint32_t kDetailRes = 32;

struct RaymarchUBO {
    glm::mat4 inv_view_proj;
    glm::vec4 camera_pos;
    glm::vec4 sun_dir;
    glm::vec4 sun_color;
    glm::vec4 ambient;
    glm::vec4 layer;   // earth_radius, cloud_bottom, cloud_top, coverage
    glm::vec4 march;   // density, extinction, steps, light_steps
    glm::vec4 scales;  // shape_scale, detail_scale, detail_strength, powder
    glm::vec4 phase;   // hg_forward, hg_back, ambient_scale, max_march
    glm::vec4 wind;    // wind.xyz, time
    glm::vec4 misc;    // scatter_w, scatter_h, frame, unused
};

struct ResolveUBO {
    glm::mat4 inv_view_proj;
    glm::mat4 prev_view_proj;
    glm::vec4 camera_pos;
    glm::vec4 misc;    // w, h, blend, reproject_far
};

void img_barrier(VkCommandBuffer cmd, VkImage image,
                 VkImageLayout oldL, VkImageLayout newL,
                 VkAccessFlags srcA, VkAccessFlags dstA,
                 VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = oldL; b.newLayout = newL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    b.srcAccessMask = srcA; b.dstAccessMask = dstA;
    vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
}
} // namespace

std::unique_ptr<VulkanCloudPass> VulkanCloudPass::create(
    VulkanDevice* device, VulkanGBuffer* g_buffer,
    VkImageView hdr_color_view, VkFormat hdr_format,
    uint32_t width, uint32_t height) {
    if (device == nullptr || g_buffer == nullptr || hdr_color_view == VK_NULL_HANDLE) {
        spdlog::error("VulkanCloudPass::create: bad args"); return nullptr;
    }
    if (width == 0 || height == 0) return nullptr;
    auto p = std::unique_ptr<VulkanCloudPass>(new VulkanCloudPass());
    if (!p->initialize(device, g_buffer, hdr_color_view, hdr_format, width, height))
        return nullptr;
    spdlog::info("VulkanCloudPass created ({}x{}, cloud {}x{})",
                 width, height, p->cw_, p->ch_);
    return p;
}

VulkanCloudPass::~VulkanCloudPass() { destroy(); }

void VulkanCloudPass::destroy() {
    if (device_ == nullptr) return;
    VkDevice vk = device_->get_device();
    auto del_pipe = [&](VkPipeline& p){ if (p) { vkDestroyPipeline(vk, p, nullptr); p = VK_NULL_HANDLE; } };
    auto del_lay  = [&](VkPipelineLayout& p){ if (p) { vkDestroyPipelineLayout(vk, p, nullptr); p = VK_NULL_HANDLE; } };
    auto del_pool = [&](VkDescriptorPool& p){ if (p) { vkDestroyDescriptorPool(vk, p, nullptr); p = VK_NULL_HANDLE; } };
    auto del_dsl  = [&](VkDescriptorSetLayout& p){ if (p) { vkDestroyDescriptorSetLayout(vk, p, nullptr); p = VK_NULL_HANDLE; } };
    auto del_tgt  = [&](Target& t){ if (t.view) vkDestroyImageView(vk, t.view, nullptr); if (t.image) vkDestroyImage(vk, t.image, nullptr); if (t.mem) vkFreeMemory(vk, t.mem, nullptr); t = {}; };
    auto del_vol  = [&](Volume& v){ if (v.view) vkDestroyImageView(vk, v.view, nullptr); if (v.image) vkDestroyImage(vk, v.image, nullptr); if (v.mem) vkFreeMemory(vk, v.mem, nullptr); v = {}; };

    if (composite_framebuffer_) { vkDestroyFramebuffer(vk, composite_framebuffer_, nullptr); composite_framebuffer_ = VK_NULL_HANDLE; }
    if (composite_render_pass_) { vkDestroyRenderPass(vk, composite_render_pass_, nullptr); composite_render_pass_ = VK_NULL_HANDLE; }
    del_pipe(composite_pipeline_); del_lay(composite_layout_); del_pool(composite_pool_); del_dsl(composite_dsl_);
    del_pipe(resolve_pipeline_);   del_lay(resolve_layout_);   del_pool(resolve_pool_);   del_dsl(resolve_dsl_);
    del_pipe(raymarch_pipeline_);  del_lay(raymarch_layout_);  del_pool(raymarch_pool_);  del_dsl(raymarch_dsl_);
    del_pipe(cloud_sky_pipeline_); del_pool(cloud_sky_pool_); // shares shadow_dsl_/layout_
    del_pipe(shadow_pipeline_);    del_lay(shadow_layout_);    del_pool(shadow_pool_);    del_dsl(shadow_dsl_);
    del_pipe(noise_pipeline_);     del_lay(noise_layout_);     del_pool(noise_pool_);     del_dsl(noise_dsl_);

    del_tgt(current_); del_tgt(history_); del_tgt(resolved_); del_tgt(cloud_shadow_); del_tgt(cloud_sky_);
    del_vol(shape_); del_vol(detail_);

    if (shadow_ubo_mapped_) { vkUnmapMemory(vk, shadow_ubo_mem_); shadow_ubo_mapped_ = nullptr; }
    if (shadow_ubo_) { vkDestroyBuffer(vk, shadow_ubo_, nullptr); shadow_ubo_ = VK_NULL_HANDLE; }
    if (shadow_ubo_mem_) { vkFreeMemory(vk, shadow_ubo_mem_, nullptr); shadow_ubo_mem_ = VK_NULL_HANDLE; }

    if (raymarch_ubo_mapped_) { vkUnmapMemory(vk, raymarch_ubo_mem_); raymarch_ubo_mapped_ = nullptr; }
    if (raymarch_ubo_) { vkDestroyBuffer(vk, raymarch_ubo_, nullptr); raymarch_ubo_ = VK_NULL_HANDLE; }
    if (raymarch_ubo_mem_) { vkFreeMemory(vk, raymarch_ubo_mem_, nullptr); raymarch_ubo_mem_ = VK_NULL_HANDLE; }
    if (resolve_ubo_mapped_) { vkUnmapMemory(vk, resolve_ubo_mem_); resolve_ubo_mapped_ = nullptr; }
    if (resolve_ubo_) { vkDestroyBuffer(vk, resolve_ubo_, nullptr); resolve_ubo_ = VK_NULL_HANDLE; }
    if (resolve_ubo_mem_) { vkFreeMemory(vk, resolve_ubo_mem_, nullptr); resolve_ubo_mem_ = VK_NULL_HANDLE; }

    if (noise_sampler_) { vkDestroySampler(vk, noise_sampler_, nullptr); noise_sampler_ = VK_NULL_HANDLE; }
    if (cloud_sampler_) { vkDestroySampler(vk, cloud_sampler_, nullptr); cloud_sampler_ = VK_NULL_HANDLE; }
    if (depth_sampler_) { vkDestroySampler(vk, depth_sampler_, nullptr); depth_sampler_ = VK_NULL_HANDLE; }
    device_ = nullptr;
}

bool VulkanCloudPass::initialize(VulkanDevice* device, VulkanGBuffer* g_buffer,
                                 VkImageView hdr_color_view, VkFormat hdr_format,
                                 uint32_t width, uint32_t height) {
    device_ = device; g_buffer_ = g_buffer;
    width_ = width; height_ = height;
    float s = config_.resolution_scale > 0.05f ? config_.resolution_scale : 0.5f;
    cw_ = (uint32_t)(width * s);  if (cw_ < 1) cw_ = 1;
    ch_ = (uint32_t)(height * s); if (ch_ < 1) ch_ = 1;

    if (!create_samplers())        return false;
    if (!create_noise_volumes())   return false;
    if (!create_cloud_targets())   return false;
    if (!create_shadow_target())   return false;
    if (!create_uniform_buffers()) return false;
    if (!create_noise_pipeline())  return false;
    if (!create_shadow_pipeline()) return false;
    if (!create_cloud_sky())       return false;
    if (!create_raymarch_pipeline())  return false;
    if (!create_resolve_pipeline())   return false;
    if (!create_composite_pipeline(hdr_color_view, hdr_format)) return false;
    return true;
}

bool VulkanCloudPass::create_samplers() {
    VkDevice vk = device_->get_device();
    auto mk = [&](VkSampler& out, VkFilter f, VkSamplerAddressMode m) {
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = f; si.minFilter = f;
        si.addressModeU = m; si.addressModeV = m; si.addressModeW = m;
        return vkCreateSampler(vk, &si, nullptr, &out) == VK_SUCCESS;
    };
    return mk(noise_sampler_, VK_FILTER_LINEAR,  VK_SAMPLER_ADDRESS_MODE_REPEAT)
        && mk(cloud_sampler_, VK_FILTER_LINEAR,  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
        && mk(depth_sampler_, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
}

bool VulkanCloudPass::create_noise_volumes() {
    VkDevice vk = device_->get_device();
    auto make_vol = [&](Volume& v, uint32_t res) {
        v.res = res;
        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_3D;
        ii.format = kNoiseFormat;
        ii.extent = { res, res, res };
        ii.mipLevels = 1; ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(vk, &ii, nullptr, &v.image) != VK_SUCCESS) return false;
        VkMemoryRequirements mr{}; vkGetImageMemoryRequirements(vk, v.image, &mr);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = device_->find_memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(vk, &ai, nullptr, &v.mem) != VK_SUCCESS) return false;
        vkBindImageMemory(vk, v.image, v.mem, 0);
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = v.image; vi.viewType = VK_IMAGE_VIEW_TYPE_3D; vi.format = kNoiseFormat;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        return vkCreateImageView(vk, &vi, nullptr, &v.view) == VK_SUCCESS;
    };
    return make_vol(shape_, kShapeRes) && make_vol(detail_, kDetailRes);
}

bool VulkanCloudPass::create_cloud_targets() {
    VkDevice vk = device_->get_device();
    auto make_tgt = [&](Target& t) {
        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = kCloudFormat;
        ii.extent = { cw_, ch_, 1 };
        ii.mipLevels = 1; ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(vk, &ii, nullptr, &t.image) != VK_SUCCESS) return false;
        VkMemoryRequirements mr{}; vkGetImageMemoryRequirements(vk, t.image, &mr);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = device_->find_memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(vk, &ai, nullptr, &t.mem) != VK_SUCCESS) return false;
        vkBindImageMemory(vk, t.image, t.mem, 0);
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = t.image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = kCloudFormat;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        return vkCreateImageView(vk, &vi, nullptr, &t.view) == VK_SUCCESS;
    };
    return make_tgt(current_) && make_tgt(history_) && make_tgt(resolved_);
}

bool VulkanCloudPass::create_uniform_buffers() {
    VkDevice vk = device_->get_device();
    auto make_ubo = [&](VkBuffer& buf, VkDeviceMemory& mem, void*& mapped, VkDeviceSize size) {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = size; bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(vk, &bi, nullptr, &buf) != VK_SUCCESS) return false;
        VkMemoryRequirements mr{}; vkGetBufferMemoryRequirements(vk, buf, &mr);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = device_->find_memory_type(mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(vk, &ai, nullptr, &mem) != VK_SUCCESS) return false;
        vkBindBufferMemory(vk, buf, mem, 0);
        return vkMapMemory(vk, mem, 0, size, 0, &mapped) == VK_SUCCESS;
    };
    return make_ubo(raymarch_ubo_, raymarch_ubo_mem_, raymarch_ubo_mapped_, sizeof(RaymarchUBO))
        && make_ubo(resolve_ubo_,  resolve_ubo_mem_,  resolve_ubo_mapped_,  sizeof(ResolveUBO))
        && make_ubo(shadow_ubo_,   shadow_ubo_mem_,   shadow_ubo_mapped_,   sizeof(RaymarchUBO));
}

bool VulkanCloudPass::create_shadow_target() {
    VkDevice vk = device_->get_device();
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R16_SFLOAT;
    ii.extent = { kShadowRes, kShadowRes, 1 };
    ii.mipLevels = 1; ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(vk, &ii, nullptr, &cloud_shadow_.image) != VK_SUCCESS) return false;
    VkMemoryRequirements mr{}; vkGetImageMemoryRequirements(vk, cloud_shadow_.image, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = device_->find_memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vk, &ai, nullptr, &cloud_shadow_.mem) != VK_SUCCESS) return false;
    vkBindImageMemory(vk, cloud_shadow_.image, cloud_shadow_.mem, 0);
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = cloud_shadow_.image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = VK_FORMAT_R16_SFLOAT;
    vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    return vkCreateImageView(vk, &vi, nullptr, &cloud_shadow_.view) == VK_SUCCESS;
}

bool VulkanCloudPass::create_shadow_pipeline() {
    VkDevice vk = device_->get_device();
    std::array<VkDescriptorSetLayoutBinding, 4> b{};
    b[0] = { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    b[1] = { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    b[2] = { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    b[3] = { 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 4; li.pBindings = b.data();
    if (vkCreateDescriptorSetLayout(vk, &li, nullptr, &shadow_dsl_) != VK_SUCCESS) return false;

    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1; pli.pSetLayouts = &shadow_dsl_;
    if (vkCreatePipelineLayout(vk, &pli, nullptr, &shadow_layout_) != VK_SUCCESS) return false;

    VkShaderModuleCreateInfo smi{};
    smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = kCloudShadowSpv_size; smi.pCode = kCloudShadowSpv;
    VkShaderModule sm = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vk, &smi, nullptr, &sm) != VK_SUCCESS) return false;
    VkComputePipelineCreateInfo cpi{};
    cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; cpi.stage.module = sm; cpi.stage.pName = "main";
    cpi.layout = shadow_layout_;
    VkResult r = vkCreateComputePipelines(vk, VK_NULL_HANDLE, 1, &cpi, nullptr, &shadow_pipeline_);
    vkDestroyShaderModule(vk, sm, nullptr);
    if (r != VK_SUCCESS) return false;

    std::array<VkDescriptorPoolSize, 3> ps{};
    ps[0] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 };
    ps[1] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 };
    ps[2] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 };
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = 1; pi.poolSizeCount = 3; pi.pPoolSizes = ps.data();
    if (vkCreateDescriptorPool(vk, &pi, nullptr, &shadow_pool_) != VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo dai{};
    dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool = shadow_pool_; dai.descriptorSetCount = 1; dai.pSetLayouts = &shadow_dsl_;
    if (vkAllocateDescriptorSets(vk, &dai, &shadow_set_) != VK_SUCCESS) return false;

    VkDescriptorBufferInfo ubi{}; ubi.buffer = shadow_ubo_; ubi.offset = 0; ubi.range = sizeof(RaymarchUBO);
    VkImageLayout sr = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    std::array<VkDescriptorImageInfo, 3> ii{};
    ii[0] = { noise_sampler_, shape_.view,        sr };
    ii[1] = { noise_sampler_, detail_.view,       sr };
    ii[2] = { VK_NULL_HANDLE, cloud_shadow_.view, VK_IMAGE_LAYOUT_GENERAL };
    std::array<VkWriteDescriptorSet, 4> ws{};
    ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[0].dstSet = shadow_set_; ws[0].dstBinding = 0; ws[0].descriptorCount = 1; ws[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; ws[0].pBufferInfo = &ubi;
    for (uint32_t i = 0; i < 2; ++i) {
        ws[i + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[i + 1].dstSet = shadow_set_;
        ws[i + 1].dstBinding = i + 1; ws[i + 1].descriptorCount = 1;
        ws[i + 1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ws[i + 1].pImageInfo = &ii[i];
    }
    ws[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[3].dstSet = shadow_set_; ws[3].dstBinding = 3; ws[3].descriptorCount = 1; ws[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; ws[3].pImageInfo = &ii[2];
    vkUpdateDescriptorSets(vk, 4, ws.data(), 0, nullptr);
    return true;
}

bool VulkanCloudPass::create_cloud_sky() {
    VkDevice vk = device_->get_device();
    // Target image (lat-long RGBA16F).
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = kCloudFormat;
    ii.extent = { kSkyW, kSkyH, 1 };
    ii.mipLevels = 1; ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(vk, &ii, nullptr, &cloud_sky_.image) != VK_SUCCESS) return false;
    VkMemoryRequirements mr{}; vkGetImageMemoryRequirements(vk, cloud_sky_.image, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = device_->find_memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vk, &ai, nullptr, &cloud_sky_.mem) != VK_SUCCESS) return false;
    vkBindImageMemory(vk, cloud_sky_.image, cloud_sky_.mem, 0);
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = cloud_sky_.image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = kCloudFormat;
    vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(vk, &vi, nullptr, &cloud_sky_.view) != VK_SUCCESS) return false;

    // Pipeline reuses the shadow pass's layout (UBO + 2 noise + storage image).
    VkShaderModuleCreateInfo smi{};
    smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = kCloudSkySpv_size; smi.pCode = kCloudSkySpv;
    VkShaderModule sm = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vk, &smi, nullptr, &sm) != VK_SUCCESS) return false;
    VkComputePipelineCreateInfo cpi{};
    cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; cpi.stage.module = sm; cpi.stage.pName = "main";
    cpi.layout = shadow_layout_;
    VkResult r = vkCreateComputePipelines(vk, VK_NULL_HANDLE, 1, &cpi, nullptr, &cloud_sky_pipeline_);
    vkDestroyShaderModule(vk, sm, nullptr);
    if (r != VK_SUCCESS) return false;

    std::array<VkDescriptorPoolSize, 3> ps{};
    ps[0] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 };
    ps[1] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 };
    ps[2] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 };
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = 1; pi.poolSizeCount = 3; pi.pPoolSizes = ps.data();
    if (vkCreateDescriptorPool(vk, &pi, nullptr, &cloud_sky_pool_) != VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo dai{};
    dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool = cloud_sky_pool_; dai.descriptorSetCount = 1; dai.pSetLayouts = &shadow_dsl_;
    if (vkAllocateDescriptorSets(vk, &dai, &cloud_sky_set_) != VK_SUCCESS) return false;

    VkDescriptorBufferInfo ubi{}; ubi.buffer = shadow_ubo_; ubi.offset = 0; ubi.range = sizeof(RaymarchUBO);
    VkImageLayout sr = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    std::array<VkDescriptorImageInfo, 3> di{};
    di[0] = { noise_sampler_, shape_.view,    sr };
    di[1] = { noise_sampler_, detail_.view,   sr };
    di[2] = { VK_NULL_HANDLE, cloud_sky_.view, VK_IMAGE_LAYOUT_GENERAL };
    std::array<VkWriteDescriptorSet, 4> ws{};
    ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[0].dstSet = cloud_sky_set_; ws[0].dstBinding = 0; ws[0].descriptorCount = 1; ws[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; ws[0].pBufferInfo = &ubi;
    for (uint32_t i = 0; i < 2; ++i) {
        ws[i + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[i + 1].dstSet = cloud_sky_set_;
        ws[i + 1].dstBinding = i + 1; ws[i + 1].descriptorCount = 1;
        ws[i + 1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ws[i + 1].pImageInfo = &di[i];
    }
    ws[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[3].dstSet = cloud_sky_set_; ws[3].dstBinding = 3; ws[3].descriptorCount = 1; ws[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; ws[3].pImageInfo = &di[2];
    vkUpdateDescriptorSets(vk, 4, ws.data(), 0, nullptr);
    return true;
}

bool VulkanCloudPass::create_noise_pipeline() {
    VkDevice vk = device_->get_device();
    VkDescriptorSetLayoutBinding b{};
    b.binding = 0; b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    b.descriptorCount = 1; b.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 1; li.pBindings = &b;
    if (vkCreateDescriptorSetLayout(vk, &li, nullptr, &noise_dsl_) != VK_SUCCESS) return false;

    VkPushConstantRange pr{};
    pr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; pr.offset = 0; pr.size = 16;
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1; pli.pSetLayouts = &noise_dsl_;
    pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pr;
    if (vkCreatePipelineLayout(vk, &pli, nullptr, &noise_layout_) != VK_SUCCESS) return false;

    VkShaderModuleCreateInfo smi{};
    smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = kCloudNoiseSpv_size; smi.pCode = kCloudNoiseSpv;
    VkShaderModule sm = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vk, &smi, nullptr, &sm) != VK_SUCCESS) return false;
    VkComputePipelineCreateInfo cpi{};
    cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; cpi.stage.module = sm; cpi.stage.pName = "main";
    cpi.layout = noise_layout_;
    VkResult r = vkCreateComputePipelines(vk, VK_NULL_HANDLE, 1, &cpi, nullptr, &noise_pipeline_);
    vkDestroyShaderModule(vk, sm, nullptr);
    if (r != VK_SUCCESS) return false;

    VkDescriptorPoolSize ps{}; ps.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; ps.descriptorCount = 2;
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = 2; pi.poolSizeCount = 1; pi.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(vk, &pi, nullptr, &noise_pool_) != VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo dai{};
    dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool = noise_pool_; dai.descriptorSetCount = 1; dai.pSetLayouts = &noise_dsl_;
    if (vkAllocateDescriptorSets(vk, &dai, &noise_set_shape_)  != VK_SUCCESS) return false;
    if (vkAllocateDescriptorSets(vk, &dai, &noise_set_detail_) != VK_SUCCESS) return false;

    auto write_set = [&](VkDescriptorSet set, VkImageView view) {
        VkDescriptorImageInfo ii{}; ii.imageView = view; ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = set; w.dstBinding = 0; w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w.pImageInfo = &ii;
        vkUpdateDescriptorSets(vk, 1, &w, 0, nullptr);
    };
    write_set(noise_set_shape_,  shape_.view);
    write_set(noise_set_detail_, detail_.view);
    return true;
}

bool VulkanCloudPass::create_raymarch_pipeline() {
    VkDevice vk = device_->get_device();
    std::array<VkDescriptorSetLayoutBinding, 5> b{};
    b[0] = { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    b[1] = { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    b[2] = { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    b[3] = { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    b[4] = { 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 5; li.pBindings = b.data();
    if (vkCreateDescriptorSetLayout(vk, &li, nullptr, &raymarch_dsl_) != VK_SUCCESS) return false;

    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1; pli.pSetLayouts = &raymarch_dsl_;
    if (vkCreatePipelineLayout(vk, &pli, nullptr, &raymarch_layout_) != VK_SUCCESS) return false;

    VkShaderModuleCreateInfo smi{};
    smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = kCloudRaymarchSpv_size; smi.pCode = kCloudRaymarchSpv;
    VkShaderModule sm = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vk, &smi, nullptr, &sm) != VK_SUCCESS) return false;
    VkComputePipelineCreateInfo cpi{};
    cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; cpi.stage.module = sm; cpi.stage.pName = "main";
    cpi.layout = raymarch_layout_;
    VkResult r = vkCreateComputePipelines(vk, VK_NULL_HANDLE, 1, &cpi, nullptr, &raymarch_pipeline_);
    vkDestroyShaderModule(vk, sm, nullptr);
    if (r != VK_SUCCESS) return false;

    std::array<VkDescriptorPoolSize, 3> ps{};
    ps[0] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 };
    ps[1] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 };
    ps[2] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 };
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = 1; pi.poolSizeCount = 3; pi.pPoolSizes = ps.data();
    if (vkCreateDescriptorPool(vk, &pi, nullptr, &raymarch_pool_) != VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo dai{};
    dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool = raymarch_pool_; dai.descriptorSetCount = 1; dai.pSetLayouts = &raymarch_dsl_;
    if (vkAllocateDescriptorSets(vk, &dai, &raymarch_set_) != VK_SUCCESS) return false;

    VkDescriptorBufferInfo ubi{}; ubi.buffer = raymarch_ubo_; ubi.offset = 0; ubi.range = sizeof(RaymarchUBO);
    VkImageLayout sr = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkImageLayout dr = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    std::array<VkDescriptorImageInfo, 4> ii{};
    ii[0] = { noise_sampler_, shape_.view,                sr };
    ii[1] = { noise_sampler_, detail_.view,               sr };
    ii[2] = { depth_sampler_, g_buffer_->get_depth_view(), dr };
    ii[3] = { VK_NULL_HANDLE, current_.view,              VK_IMAGE_LAYOUT_GENERAL };
    std::array<VkWriteDescriptorSet, 5> ws{};
    ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[0].dstSet = raymarch_set_; ws[0].dstBinding = 0; ws[0].descriptorCount = 1; ws[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; ws[0].pBufferInfo = &ubi;
    for (uint32_t i = 0; i < 3; ++i) {
        ws[i + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[i + 1].dstSet = raymarch_set_;
        ws[i + 1].dstBinding = i + 1; ws[i + 1].descriptorCount = 1;
        ws[i + 1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ws[i + 1].pImageInfo = &ii[i];
    }
    ws[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[4].dstSet = raymarch_set_; ws[4].dstBinding = 4; ws[4].descriptorCount = 1; ws[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; ws[4].pImageInfo = &ii[3];
    vkUpdateDescriptorSets(vk, 5, ws.data(), 0, nullptr);
    return true;
}

bool VulkanCloudPass::create_resolve_pipeline() {
    VkDevice vk = device_->get_device();
    std::array<VkDescriptorSetLayoutBinding, 4> b{};
    b[0] = { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    b[1] = { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    b[2] = { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    b[3] = { 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 4; li.pBindings = b.data();
    if (vkCreateDescriptorSetLayout(vk, &li, nullptr, &resolve_dsl_) != VK_SUCCESS) return false;

    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1; pli.pSetLayouts = &resolve_dsl_;
    if (vkCreatePipelineLayout(vk, &pli, nullptr, &resolve_layout_) != VK_SUCCESS) return false;

    VkShaderModuleCreateInfo smi{};
    smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = kCloudResolveSpv_size; smi.pCode = kCloudResolveSpv;
    VkShaderModule sm = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vk, &smi, nullptr, &sm) != VK_SUCCESS) return false;
    VkComputePipelineCreateInfo cpi{};
    cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; cpi.stage.module = sm; cpi.stage.pName = "main";
    cpi.layout = resolve_layout_;
    VkResult r = vkCreateComputePipelines(vk, VK_NULL_HANDLE, 1, &cpi, nullptr, &resolve_pipeline_);
    vkDestroyShaderModule(vk, sm, nullptr);
    if (r != VK_SUCCESS) return false;

    std::array<VkDescriptorPoolSize, 3> ps{};
    ps[0] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 };
    ps[1] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 };
    ps[2] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 };
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = 1; pi.poolSizeCount = 3; pi.pPoolSizes = ps.data();
    if (vkCreateDescriptorPool(vk, &pi, nullptr, &resolve_pool_) != VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo dai{};
    dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool = resolve_pool_; dai.descriptorSetCount = 1; dai.pSetLayouts = &resolve_dsl_;
    if (vkAllocateDescriptorSets(vk, &dai, &resolve_set_) != VK_SUCCESS) return false;

    VkDescriptorBufferInfo ubi{}; ubi.buffer = resolve_ubo_; ubi.offset = 0; ubi.range = sizeof(ResolveUBO);
    VkImageLayout sr = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    std::array<VkDescriptorImageInfo, 3> ii{};
    ii[0] = { cloud_sampler_, current_.view, sr };
    ii[1] = { cloud_sampler_, history_.view, sr };
    ii[2] = { VK_NULL_HANDLE, resolved_.view, VK_IMAGE_LAYOUT_GENERAL };
    std::array<VkWriteDescriptorSet, 4> ws{};
    ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[0].dstSet = resolve_set_; ws[0].dstBinding = 0; ws[0].descriptorCount = 1; ws[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; ws[0].pBufferInfo = &ubi;
    for (uint32_t i = 0; i < 2; ++i) {
        ws[i + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[i + 1].dstSet = resolve_set_;
        ws[i + 1].dstBinding = i + 1; ws[i + 1].descriptorCount = 1;
        ws[i + 1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ws[i + 1].pImageInfo = &ii[i];
    }
    ws[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[3].dstSet = resolve_set_; ws[3].dstBinding = 3; ws[3].descriptorCount = 1; ws[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; ws[3].pImageInfo = &ii[2];
    vkUpdateDescriptorSets(vk, 4, ws.data(), 0, nullptr);
    return true;
}

bool VulkanCloudPass::create_composite_pipeline(VkImageView hdr_color_view, VkFormat hdr_format) {
    VkDevice vk = device_->get_device();
    VkAttachmentDescription att{};
    att.format = hdr_format; att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference ref{}; ref.attachment = 0; ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription sub{}; sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount = 1; sub.pColorAttachments = &ref;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 1; rpi.pAttachments = &att;
    rpi.subpassCount = 1; rpi.pSubpasses = &sub;
    rpi.dependencyCount = 1; rpi.pDependencies = &dep;
    if (vkCreateRenderPass(vk, &rpi, nullptr, &composite_render_pass_) != VK_SUCCESS) return false;

    VkFramebufferCreateInfo fbi{};
    fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbi.renderPass = composite_render_pass_; fbi.attachmentCount = 1; fbi.pAttachments = &hdr_color_view;
    fbi.width = width_; fbi.height = height_; fbi.layers = 1;
    if (vkCreateFramebuffer(vk, &fbi, nullptr, &composite_framebuffer_) != VK_SUCCESS) return false;

    VkDescriptorSetLayoutBinding b{};
    b.binding = 0; b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; b.descriptorCount = 1; b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; li.bindingCount = 1; li.pBindings = &b;
    if (vkCreateDescriptorSetLayout(vk, &li, nullptr, &composite_dsl_) != VK_SUCCESS) return false;
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; pli.setLayoutCount = 1; pli.pSetLayouts = &composite_dsl_;
    if (vkCreatePipelineLayout(vk, &pli, nullptr, &composite_layout_) != VK_SUCCESS) return false;

    VkDescriptorPoolSize ps{}; ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps.descriptorCount = 1;
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO; pi.maxSets = 1; pi.poolSizeCount = 1; pi.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(vk, &pi, nullptr, &composite_pool_) != VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo dai{};
    dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; dai.descriptorPool = composite_pool_; dai.descriptorSetCount = 1; dai.pSetLayouts = &composite_dsl_;
    if (vkAllocateDescriptorSets(vk, &dai, &composite_set_) != VK_SUCCESS) return false;
    VkDescriptorImageInfo cii{}; cii.sampler = cloud_sampler_; cii.imageView = resolved_.view; cii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w.dstSet = composite_set_; w.dstBinding = 0; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &cii;
    vkUpdateDescriptorSets(vk, 1, &w, 0, nullptr);

    VkShaderModuleCreateInfo smvi{}; smvi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO; smvi.codeSize = kCloudCompositeVertSpv_size; smvi.pCode = kCloudCompositeVertSpv;
    VkShaderModule sv = VK_NULL_HANDLE; if (vkCreateShaderModule(vk, &smvi, nullptr, &sv) != VK_SUCCESS) return false;
    VkShaderModuleCreateInfo smfi{}; smfi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO; smfi.codeSize = kCloudCompositeFragSpv_size; smfi.pCode = kCloudCompositeFragSpv;
    VkShaderModule sf = VK_NULL_HANDLE; if (vkCreateShaderModule(vk, &smfi, nullptr, &sf) != VK_SUCCESS) { vkDestroyShaderModule(vk, sv, nullptr); return false; }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = sv; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = sf; stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    std::array<VkDynamicState, 2> dyns{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{}; dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; dyn.dynamicStateCount = 2; dyn.pDynamicStates = dyns.data();
    VkPipelineViewportStateCreateInfo vp{}; vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    // out = scattering*ONE + hdr*transmittance(src alpha).
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = 0xF; cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; cba.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE; cba.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo cb{}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cb.attachmentCount = 1; cb.pAttachments = &cba;
    VkGraphicsPipelineCreateInfo gpi{};
    gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpi.stageCount = 2; gpi.pStages = stages.data();
    gpi.pVertexInputState = &vi; gpi.pInputAssemblyState = &ia; gpi.pViewportState = &vp;
    gpi.pRasterizationState = &rs; gpi.pMultisampleState = &ms; gpi.pColorBlendState = &cb; gpi.pDynamicState = &dyn;
    gpi.layout = composite_layout_; gpi.renderPass = composite_render_pass_;
    VkResult res = vkCreateGraphicsPipelines(vk, VK_NULL_HANDLE, 1, &gpi, nullptr, &composite_pipeline_);
    vkDestroyShaderModule(vk, sv, nullptr); vkDestroyShaderModule(vk, sf, nullptr);
    return res == VK_SUCCESS;
}

void VulkanCloudPass::generate_noise(VkCommandBuffer cmd) {
    img_barrier(cmd, shape_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    img_barrier(cmd, detail_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, noise_pipeline_);
    struct NP { int resolution; int mode; float p0, p1; } np{};
    // shape
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, noise_layout_, 0, 1, &noise_set_shape_, 0, nullptr);
    np = { (int)kShapeRes, 0, 0, 0 };
    vkCmdPushConstants(cmd, noise_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(np), &np);
    vkCmdDispatch(cmd, (kShapeRes + 3) / 4, (kShapeRes + 3) / 4, (kShapeRes + 3) / 4);
    // detail
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, noise_layout_, 0, 1, &noise_set_detail_, 0, nullptr);
    np = { (int)kDetailRes, 1, 0, 0 };
    vkCmdPushConstants(cmd, noise_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(np), &np);
    vkCmdDispatch(cmd, (kDetailRes + 3) / 4, (kDetailRes + 3) / 4, (kDetailRes + 3) / 4);
    img_barrier(cmd, shape_.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    img_barrier(cmd, detail_.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

bool VulkanCloudPass::rebuild_targets() {
    if (device_ == nullptr) return false;
    VkDevice vk = device_->get_device();
    vkDeviceWaitIdle(vk);

    float s = config_.resolution_scale > 0.05f ? config_.resolution_scale : 0.5f;
    cw_ = (uint32_t)(width_ * s);  if (cw_ < 1) cw_ = 1;
    ch_ = (uint32_t)(height_ * s); if (ch_ < 1) ch_ = 1;

    auto del_tgt = [&](Target& t){
        if (t.view)  vkDestroyImageView(vk, t.view, nullptr);
        if (t.image) vkDestroyImage(vk, t.image, nullptr);
        if (t.mem)   vkFreeMemory(vk, t.mem, nullptr);
        t = {};
    };
    del_tgt(current_); del_tgt(history_); del_tgt(resolved_);
    if (!create_cloud_targets()) return false;

    auto write_img = [&](VkDescriptorSet set, uint32_t binding, VkSampler samp,
                         VkImageView view, VkImageLayout layout, VkDescriptorType type) {
        VkDescriptorImageInfo ii{}; ii.sampler = samp; ii.imageView = view; ii.imageLayout = layout;
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = set; w.dstBinding = binding; w.descriptorCount = 1;
        w.descriptorType = type; w.pImageInfo = &ii;
        vkUpdateDescriptorSets(vk, 1, &w, 0, nullptr);
    };
    VkImageLayout sr = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    write_img(raymarch_set_,  4, VK_NULL_HANDLE,  current_.view,  VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    write_img(resolve_set_,   1, cloud_sampler_,  current_.view,  sr, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    write_img(resolve_set_,   2, cloud_sampler_,  history_.view,  sr, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    write_img(resolve_set_,   3, VK_NULL_HANDLE,  resolved_.view, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    write_img(composite_set_, 0, cloud_sampler_,  resolved_.view, sr, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    history_initialized_ = false;
    return true;
}

void VulkanCloudPass::compute_shadow_map(VkCommandBuffer cmd, const glm::vec3& camera_position) {
    if (shadow_pipeline_ == VK_NULL_HANDLE) return;
    shadow_center_ = glm::vec2(camera_position.x, camera_position.z);

    // Fill the shadow UBO (RaymarchUBO layout; misc.w = map half-extent). Same
    // per-frame cloud state the raymarch uses, so the shadow matches the view.
    RaymarchUBO sb{};
    sb.inv_view_proj = glm::mat4(1.0f); // unused by the shadow shader
    sb.camera_pos = glm::vec4(camera_position, 1.0f);
    sb.sun_dir = glm::vec4(sun_direction_, 0.0f);
    sb.sun_color = glm::vec4(sun_color_, 0.0f);
    sb.ambient = glm::vec4(ambient_, config_.brightness);
    sb.layer = glm::vec4(config_.earth_radius, config_.cloud_bottom, config_.cloud_top, config_.coverage);
    sb.march = glm::vec4(config_.density, config_.extinction, (float)config_.march_steps, (float)config_.light_steps);
    sb.scales = glm::vec4(config_.shape_scale, config_.detail_scale, config_.detail_strength, config_.powder);
    sb.phase = glm::vec4(config_.hg_forward, config_.hg_back, config_.ambient_scale, config_.max_march);
    sb.wind = glm::vec4(1.0f, 0.0f, 0.3f, config_.wind_speed * time_);
    sb.misc = glm::vec4(0.0f, 0.0f, 0.0f, config_.shadow_extent);
    std::memcpy(shadow_ubo_mapped_, &sb, sizeof(sb));

    // Noise must exist before the shadow shader samples it (this runs before
    // the raymarch each frame, so it owns first-frame noise generation).
    if (!noise_generated_) { generate_noise(cmd); noise_generated_ = true; }

    img_barrier(cmd, cloud_shadow_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, shadow_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, shadow_layout_, 0, 1, &shadow_set_, 0, nullptr);
    vkCmdDispatch(cmd, (kShadowRes + 7) / 8, (kShadowRes + 7) / 8, 1);
    // Make it readable by the lighting pass (fragment) + shaft pass (compute).
    img_barrier(cmd, cloud_shadow_.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    shadow_initialized_ = true;

    // Cloud-sky lat-long map for reflections (shares shadow_ubo_). SSR samples
    // it on a sky-miss, so it must be ready before the SSR pass runs.
    img_barrier(cmd, cloud_sky_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cloud_sky_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, shadow_layout_, 0, 1, &cloud_sky_set_, 0, nullptr);
    vkCmdDispatch(cmd, (kSkyW + 7) / 8, (kSkyH + 7) / 8, 1);
    img_barrier(cmd, cloud_sky_.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

void VulkanCloudPass::execute(VkCommandBuffer cmd, const glm::mat4& view,
                              const glm::mat4& proj, const glm::vec3& camera_position) {
    if (!enabled_ || raymarch_pipeline_ == VK_NULL_HANDLE) return;
    const glm::mat4 vp = proj * view;

    // --- update raymarch UBO ---
    RaymarchUBO rb{};
    rb.inv_view_proj = glm::inverse(vp);
    rb.camera_pos = glm::vec4(camera_position, 1.0f);
    rb.sun_dir = glm::vec4(sun_direction_, 0.0f);
    rb.sun_color = glm::vec4(sun_color_, 0.0f);
    rb.ambient = glm::vec4(ambient_, config_.brightness);
    rb.layer = glm::vec4(config_.earth_radius, config_.cloud_bottom, config_.cloud_top, config_.coverage);
    rb.march = glm::vec4(config_.density, config_.extinction, (float)config_.march_steps, (float)config_.light_steps);
    rb.scales = glm::vec4(config_.shape_scale, config_.detail_scale, config_.detail_strength, config_.powder);
    rb.phase = glm::vec4(config_.hg_forward, config_.hg_back, config_.ambient_scale, config_.max_march);
    rb.wind = glm::vec4(1.0f, 0.0f, 0.3f, config_.wind_speed * time_);
    rb.misc = glm::vec4((float)cw_, (float)ch_, (float)frame_, 0.0f);
    std::memcpy(raymarch_ubo_mapped_, &rb, sizeof(rb));

    if (!noise_generated_) {
        generate_noise(cmd);
        noise_generated_ = true;
    }
    if (!history_initialized_) {
        // History starts UNDEFINED; transition it to a valid sampled layout
        // before the first resolve read (its garbage content is bounded by the
        // resolve's neighbourhood clamp). Re-run after a resolution rebuild.
        img_barrier(cmd, history_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    0, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        history_initialized_ = true;
    }

    // --- raymarch -> current ---
    img_barrier(cmd, current_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, raymarch_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, raymarch_layout_, 0, 1, &raymarch_set_, 0, nullptr);
    vkCmdDispatch(cmd, (cw_ + 7) / 8, (ch_ + 7) / 8, 1);
    img_barrier(cmd, current_.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    // --- update resolve UBO ---
    ResolveUBO rv{};
    rv.inv_view_proj = glm::inverse(vp);
    rv.prev_view_proj = prev_view_proj_;
    rv.camera_pos = glm::vec4(camera_position, 1.0f);
    rv.misc = glm::vec4((float)cw_, (float)ch_,
                        config_.temporal ? config_.temporal_blend : 1.0f,
                        config_.reproject_far);
    std::memcpy(resolve_ubo_mapped_, &rv, sizeof(rv));

    // --- resolve -> resolved ---
    img_barrier(cmd, resolved_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, resolve_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, resolve_layout_, 0, 1, &resolve_set_, 0, nullptr);
    vkCmdDispatch(cmd, (cw_ + 7) / 8, (ch_ + 7) / 8, 1);

    // --- copy resolved -> history (for next frame) ---
    img_barrier(cmd, resolved_.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    img_barrier(cmd, history_.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkImageCopy region{};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.extent = { cw_, ch_, 1 };
    vkCmdCopyImage(cmd, resolved_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   history_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    img_barrier(cmd, history_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    img_barrier(cmd, resolved_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    // --- composite into HDR ---
    VkRenderPassBeginInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass = composite_render_pass_; rpi.framebuffer = composite_framebuffer_;
    rpi.renderArea.offset = { 0, 0 }; rpi.renderArea.extent = { width_, height_ };
    vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vps{}; vps.width = (float)width_; vps.height = (float)height_; vps.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vps);
    VkRect2D sc{}; sc.extent = { width_, height_ };
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, composite_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, composite_layout_, 0, 1, &composite_set_, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    prev_view_proj_ = vp;
    frame_++;
    time_ += 1.0f / 60.0f;
}

} // namespace gws::renderer::gpu
