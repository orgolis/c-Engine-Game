/**
 * @file vulkan_volumetric_light_pass.cpp
 * @brief Implementation of the volumetric sun lighting / light-shaft pass.
 */

#include "vulkan_volumetric_light_pass.h"
#include "vulkan_device.h"
#include "vulkan_g_buffer.h"
#include "volumetric_light_spirv.h"

#include <spdlog/spdlog.h>
#include <array>
#include <cstring>
#include <cstdint>

namespace gws::renderer::gpu {

namespace {
constexpr VkFormat kScatterFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

// std140-compatible uniform block — mirrors `Params` in volumetric_light.comp.
struct VolUBO {
    glm::mat4 inv_view_proj;
    glm::mat4 shadow_matrix;
    glm::vec4 camera_pos;
    glm::vec4 sun_dir;    // xyz = direction, w = density
    glm::vec4 sun_color;  // rgb = colour*intensity, w = anisotropy g
    glm::vec4 params;     // x = max_distance, y = intensity, z = num_steps, w = bias
    glm::vec4 misc;       // x = scatter_w, y = scatter_h
    glm::vec4 cloud_params; // x = center.x, y = center.z, z = half_extent, w = enabled
};
} // namespace

std::unique_ptr<VulkanVolumetricLightPass> VulkanVolumetricLightPass::create(
    VulkanDevice* device, VulkanGBuffer* g_buffer,
    VkImageView hdr_color_view, VkFormat hdr_format,
    VkImageView shadow_view, VkSampler shadow_sampler,
    uint32_t width, uint32_t height) {
    if (device == nullptr || g_buffer == nullptr ||
        hdr_color_view == VK_NULL_HANDLE || shadow_view == VK_NULL_HANDLE) {
        spdlog::error("VulkanVolumetricLightPass::create: bad args");
        return nullptr;
    }
    if (width == 0 || height == 0) return nullptr;
    auto p = std::unique_ptr<VulkanVolumetricLightPass>(new VulkanVolumetricLightPass());
    if (!p->initialize(device, g_buffer, hdr_color_view, hdr_format,
                       shadow_view, shadow_sampler, width, height)) {
        return nullptr;
    }
    spdlog::info("VulkanVolumetricLightPass created ({}x{}, scatter {}x{})",
                 width, height, p->scatter_w_, p->scatter_h_);
    return p;
}

VulkanVolumetricLightPass::~VulkanVolumetricLightPass() { destroy(); }

void VulkanVolumetricLightPass::destroy() {
    if (device_ == nullptr) return;
    VkDevice vk = device_->get_device();
    if (composite_pipeline_    != VK_NULL_HANDLE) { vkDestroyPipeline(vk, composite_pipeline_, nullptr); composite_pipeline_ = VK_NULL_HANDLE; }
    if (composite_layout_      != VK_NULL_HANDLE) { vkDestroyPipelineLayout(vk, composite_layout_, nullptr); composite_layout_ = VK_NULL_HANDLE; }
    if (composite_pool_        != VK_NULL_HANDLE) { vkDestroyDescriptorPool(vk, composite_pool_, nullptr); composite_pool_ = VK_NULL_HANDLE; composite_set_ = VK_NULL_HANDLE; }
    if (composite_dsl_         != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(vk, composite_dsl_, nullptr); composite_dsl_ = VK_NULL_HANDLE; }
    if (composite_framebuffer_ != VK_NULL_HANDLE) { vkDestroyFramebuffer(vk, composite_framebuffer_, nullptr); composite_framebuffer_ = VK_NULL_HANDLE; }
    if (composite_render_pass_ != VK_NULL_HANDLE) { vkDestroyRenderPass(vk, composite_render_pass_, nullptr); composite_render_pass_ = VK_NULL_HANDLE; }
    if (compute_pipeline_      != VK_NULL_HANDLE) { vkDestroyPipeline(vk, compute_pipeline_, nullptr); compute_pipeline_ = VK_NULL_HANDLE; }
    if (compute_layout_        != VK_NULL_HANDLE) { vkDestroyPipelineLayout(vk, compute_layout_, nullptr); compute_layout_ = VK_NULL_HANDLE; }
    if (compute_pool_          != VK_NULL_HANDLE) { vkDestroyDescriptorPool(vk, compute_pool_, nullptr); compute_pool_ = VK_NULL_HANDLE; compute_set_ = VK_NULL_HANDLE; }
    if (compute_dsl_           != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(vk, compute_dsl_, nullptr); compute_dsl_ = VK_NULL_HANDLE; }
    if (scatter_view_          != VK_NULL_HANDLE) { vkDestroyImageView(vk, scatter_view_, nullptr); scatter_view_ = VK_NULL_HANDLE; }
    if (scatter_image_         != VK_NULL_HANDLE) { vkDestroyImage(vk, scatter_image_, nullptr); scatter_image_ = VK_NULL_HANDLE; }
    if (scatter_memory_        != VK_NULL_HANDLE) { vkFreeMemory(vk, scatter_memory_, nullptr); scatter_memory_ = VK_NULL_HANDLE; }
    if (ubo_mapped_ != nullptr) { vkUnmapMemory(vk, ubo_memory_); ubo_mapped_ = nullptr; }
    if (ubo_                   != VK_NULL_HANDLE) { vkDestroyBuffer(vk, ubo_, nullptr); ubo_ = VK_NULL_HANDLE; }
    if (ubo_memory_            != VK_NULL_HANDLE) { vkFreeMemory(vk, ubo_memory_, nullptr); ubo_memory_ = VK_NULL_HANDLE; }
    if (scatter_sampler_       != VK_NULL_HANDLE) { vkDestroySampler(vk, scatter_sampler_, nullptr); scatter_sampler_ = VK_NULL_HANDLE; }
    if (gbuffer_sampler_       != VK_NULL_HANDLE) { vkDestroySampler(vk, gbuffer_sampler_, nullptr); gbuffer_sampler_ = VK_NULL_HANDLE; }
    device_ = nullptr;
}

bool VulkanVolumetricLightPass::initialize(
    VulkanDevice* device, VulkanGBuffer* g_buffer,
    VkImageView hdr_color_view, VkFormat hdr_format,
    VkImageView shadow_view, VkSampler shadow_sampler,
    uint32_t width, uint32_t height) {
    device_   = device;
    g_buffer_ = g_buffer;
    width_    = width;
    height_   = height;
    scatter_w_ = (width  + 1) / 2;  // half resolution
    scatter_h_ = (height + 1) / 2;
    hdr_color_view_ = hdr_color_view;
    shadow_view_    = shadow_view;
    shadow_sampler_ = shadow_sampler;
    VkDevice vk = device_->get_device();

    auto make_sampler = [&](VkSampler& out, VkFilter f) {
        VkSamplerCreateInfo si{};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = f;
        si.minFilter    = f;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        return vkCreateSampler(vk, &si, nullptr, &out) == VK_SUCCESS;
    };
    if (!make_sampler(gbuffer_sampler_, VK_FILTER_NEAREST)) return false;
    if (!make_sampler(scatter_sampler_, VK_FILTER_LINEAR))  return false;

    if (!create_scatter_target())  return false;
    if (!create_uniform_buffer())  return false;
    if (!create_compute_pipeline()) return false;
    if (!create_composite_pipeline(hdr_color_view, hdr_format)) return false;
    return true;
}

bool VulkanVolumetricLightPass::create_scatter_target() {
    VkDevice vk = device_->get_device();
    VkImageCreateInfo ii{};
    ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType     = VK_IMAGE_TYPE_2D;
    ii.format        = kScatterFormat;
    ii.extent        = { scatter_w_, scatter_h_, 1 };
    ii.mipLevels     = 1;
    ii.arrayLayers   = 1;
    ii.samples       = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ii.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(vk, &ii, nullptr, &scatter_image_) != VK_SUCCESS) return false;

    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(vk, scatter_image_, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = device_->find_memory_type(
        mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vk, &ai, nullptr, &scatter_memory_) != VK_SUCCESS) return false;
    vkBindImageMemory(vk, scatter_image_, scatter_memory_, 0);

    VkImageViewCreateInfo vi{};
    vi.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image            = scatter_image_;
    vi.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    vi.format           = kScatterFormat;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    return vkCreateImageView(vk, &vi, nullptr, &scatter_view_) == VK_SUCCESS;
}

bool VulkanVolumetricLightPass::create_uniform_buffer() {
    VkDevice vk = device_->get_device();
    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = sizeof(VolUBO);
    bi.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vk, &bi, nullptr, &ubo_) != VK_SUCCESS) return false;

    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(vk, ubo_, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = device_->find_memory_type(
        mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(vk, &ai, nullptr, &ubo_memory_) != VK_SUCCESS) return false;
    vkBindBufferMemory(vk, ubo_, ubo_memory_, 0);
    return vkMapMemory(vk, ubo_memory_, 0, sizeof(VolUBO), 0, &ubo_mapped_) == VK_SUCCESS;
}

bool VulkanVolumetricLightPass::create_compute_pipeline() {
    VkDevice vk = device_->get_device();

    std::array<VkDescriptorSetLayoutBinding, 6> b{};
    b[0].binding = 0; // UBO
    b[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b[0].descriptorCount = 1;
    b[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    for (uint32_t i = 1; i <= 3; ++i) { // position, depth, shadow
        b[i].binding = i;
        b[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[i].descriptorCount = 1;
        b[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    b[4].binding = 4; // scatter output
    b[4].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    b[4].descriptorCount = 1;
    b[4].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    b[5].binding = 5; // cloud shadow map (sampled when cloud_params.w > 0.5)
    b[5].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[5].descriptorCount = 1;
    b[5].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = static_cast<uint32_t>(b.size());
    li.pBindings    = b.data();
    if (vkCreateDescriptorSetLayout(vk, &li, nullptr, &compute_dsl_) != VK_SUCCESS) return false;

    VkPipelineLayoutCreateInfo pli{};
    pli.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts    = &compute_dsl_;
    if (vkCreatePipelineLayout(vk, &pli, nullptr, &compute_layout_) != VK_SUCCESS) return false;

    VkShaderModuleCreateInfo smi{};
    smi.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = kVolLightComputeSpv_size;
    smi.pCode    = kVolLightComputeSpv;
    VkShaderModule sm = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vk, &smi, nullptr, &sm) != VK_SUCCESS) return false;
    VkComputePipelineCreateInfo cpi{};
    cpi.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpi.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.module = sm;
    cpi.stage.pName  = "main";
    cpi.layout       = compute_layout_;
    VkResult r = vkCreateComputePipelines(vk, VK_NULL_HANDLE, 1, &cpi, nullptr, &compute_pipeline_);
    vkDestroyShaderModule(vk, sm, nullptr);
    if (r != VK_SUCCESS) return false;

    std::array<VkDescriptorPoolSize, 3> ps{};
    ps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;         ps[0].descriptorCount = 1;
    ps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps[1].descriptorCount = 4;
    ps[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;          ps[2].descriptorCount = 1;
    VkDescriptorPoolCreateInfo pi{};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets       = 1;
    pi.poolSizeCount = static_cast<uint32_t>(ps.size());
    pi.pPoolSizes    = ps.data();
    if (vkCreateDescriptorPool(vk, &pi, nullptr, &compute_pool_) != VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo dai{};
    dai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool     = compute_pool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts        = &compute_dsl_;
    if (vkAllocateDescriptorSets(vk, &dai, &compute_set_) != VK_SUCCESS) return false;

    // Static descriptor writes — none of these handles change for the pass
    // lifetime (the UBO contents are updated in place each frame).
    VkDescriptorBufferInfo ubi{};
    ubi.buffer = ubo_;
    ubi.offset = 0;
    ubi.range  = sizeof(VolUBO);

    VkImageLayout sr = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkImageLayout dr = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    std::array<VkDescriptorImageInfo, 5> ii{};
    ii[0] = { gbuffer_sampler_, g_buffer_->get_position_view(), sr };
    ii[1] = { gbuffer_sampler_, g_buffer_->get_depth_view(),    dr };
    ii[2] = { shadow_sampler_,  shadow_view_,                   dr };
    ii[3] = { VK_NULL_HANDLE,   scatter_view_,                  VK_IMAGE_LAYOUT_GENERAL };
    // Binding 5 placeholder (position view) until set_cloud_shadow() supplies
    // the real map; harmless because the shader only samples it when
    // cloud_params.w > 0.5, which stays 0 until clouds bind a map.
    ii[4] = { gbuffer_sampler_, g_buffer_->get_position_view(), sr };

    std::array<VkWriteDescriptorSet, 6> ws{};
    ws[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ws[0].dstSet          = compute_set_;
    ws[0].dstBinding      = 0;
    ws[0].descriptorCount = 1;
    ws[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ws[0].pBufferInfo     = &ubi;
    for (uint32_t i = 0; i < 3; ++i) { // bindings 1,2,3 -> ii[0,1,2]
        ws[i + 1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[i + 1].dstSet          = compute_set_;
        ws[i + 1].dstBinding      = i + 1;
        ws[i + 1].descriptorCount = 1;
        ws[i + 1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ws[i + 1].pImageInfo      = &ii[i];
    }
    ws[4].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ws[4].dstSet          = compute_set_;
    ws[4].dstBinding      = 4;
    ws[4].descriptorCount = 1;
    ws[4].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ws[4].pImageInfo      = &ii[3];
    ws[5].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ws[5].dstSet          = compute_set_;
    ws[5].dstBinding      = 5;
    ws[5].descriptorCount = 1;
    ws[5].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ws[5].pImageInfo      = &ii[4];
    vkUpdateDescriptorSets(vk, static_cast<uint32_t>(ws.size()), ws.data(), 0, nullptr);
    return true;
}

bool VulkanVolumetricLightPass::create_composite_pipeline(VkImageView hdr_color_view,
                                                          VkFormat hdr_format) {
    VkDevice vk = device_->get_device();

    // HDR colour LOAD (keep lit content), STORE — we add the shafts on top.
    VkAttachmentDescription att{};
    att.format         = hdr_format;
    att.samples        = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
    att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    att.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference ref{};
    ref.attachment = 0; ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription sub{};
    sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments    = &ref;
    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rpi{};
    rpi.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 1; rpi.pAttachments  = &att;
    rpi.subpassCount    = 1; rpi.pSubpasses    = &sub;
    rpi.dependencyCount = 1; rpi.pDependencies = &dep;
    if (vkCreateRenderPass(vk, &rpi, nullptr, &composite_render_pass_) != VK_SUCCESS) return false;

    VkFramebufferCreateInfo fbi{};
    fbi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbi.renderPass      = composite_render_pass_;
    fbi.attachmentCount = 1;
    fbi.pAttachments    = &hdr_color_view;
    fbi.width           = width_; fbi.height = height_; fbi.layers = 1;
    if (vkCreateFramebuffer(vk, &fbi, nullptr, &composite_framebuffer_) != VK_SUCCESS) return false;

    // Binding 0 = half-res scatter, binding 1 = full-res G-Buffer position
    // (the bilateral-upsample edge-stop guide).
    std::array<VkDescriptorSetLayoutBinding, 2> cbnd{};
    for (uint32_t i = 0; i < 2; ++i) {
        cbnd[i].binding         = i;
        cbnd[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        cbnd[i].descriptorCount = 1;
        cbnd[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 2; li.pBindings = cbnd.data();
    if (vkCreateDescriptorSetLayout(vk, &li, nullptr, &composite_dsl_) != VK_SUCCESS) return false;

    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1; pli.pSetLayouts = &composite_dsl_;
    if (vkCreatePipelineLayout(vk, &pli, nullptr, &composite_layout_) != VK_SUCCESS) return false;

    VkDescriptorPoolSize ps{};
    ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps.descriptorCount = 2;
    VkDescriptorPoolCreateInfo pi{};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets       = 1;
    pi.poolSizeCount = 1; pi.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(vk, &pi, nullptr, &composite_pool_) != VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo dai{};
    dai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool     = composite_pool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts        = &composite_dsl_;
    if (vkAllocateDescriptorSets(vk, &dai, &composite_set_) != VK_SUCCESS) return false;

    // Both sampled with NEAREST (gbuffer_sampler_) — the bilateral upsample
    // does its own controlled weighting, so we want distinct texel taps.
    std::array<VkDescriptorImageInfo, 2> cii{};
    cii[0] = { gbuffer_sampler_, scatter_view_,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    cii[1] = { gbuffer_sampler_, g_buffer_->get_position_view(),
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    std::array<VkWriteDescriptorSet, 2> cw{};
    for (uint32_t i = 0; i < 2; ++i) {
        cw[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        cw[i].dstSet          = composite_set_;
        cw[i].dstBinding      = i;
        cw[i].descriptorCount = 1;
        cw[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        cw[i].pImageInfo      = &cii[i];
    }
    vkUpdateDescriptorSets(vk, 2, cw.data(), 0, nullptr);

    VkShaderModuleCreateInfo smvi{};
    smvi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smvi.codeSize = kVolCompositeVertSpv_size; smvi.pCode = kVolCompositeVertSpv;
    VkShaderModule sv = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vk, &smvi, nullptr, &sv) != VK_SUCCESS) return false;
    VkShaderModuleCreateInfo smfi{};
    smfi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smfi.codeSize = kVolCompositeFragSpv_size; smfi.pCode = kVolCompositeFragSpv;
    VkShaderModule sf = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vk, &smfi, nullptr, &sf) != VK_SUCCESS) { vkDestroyShaderModule(vk, sv, nullptr); return false; }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = sv; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = sf; stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    std::array<VkDynamicState, 2> dyns{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2; dyn.pDynamicStates = dyns.data();
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Additive blend: out = hdr + scatter.
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask      = 0xF;
    cba.blendEnable         = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.colorBlendOp        = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.alphaBlendOp        = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;

    VkGraphicsPipelineCreateInfo gpi{};
    gpi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpi.stageCount          = static_cast<uint32_t>(stages.size());
    gpi.pStages             = stages.data();
    gpi.pVertexInputState   = &vi;
    gpi.pInputAssemblyState = &ia;
    gpi.pViewportState      = &vp;
    gpi.pRasterizationState = &rs;
    gpi.pMultisampleState   = &ms;
    gpi.pColorBlendState    = &cb;
    gpi.pDynamicState       = &dyn;
    gpi.layout              = composite_layout_;
    gpi.renderPass          = composite_render_pass_;
    VkResult res = vkCreateGraphicsPipelines(vk, VK_NULL_HANDLE, 1, &gpi, nullptr, &composite_pipeline_);
    vkDestroyShaderModule(vk, sv, nullptr);
    vkDestroyShaderModule(vk, sf, nullptr);
    return res == VK_SUCCESS;
}

void VulkanVolumetricLightPass::set_cloud_shadow(VkImageView view, VkSampler sampler) {
    cloud_shadow_view_ = view;
    cloud_shadow_sampler_ = sampler;
    if (view == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE || compute_set_ == VK_NULL_HANDLE) return;
    VkDescriptorImageInfo ii{};
    ii.sampler = sampler; ii.imageView = view;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = compute_set_; w.dstBinding = 5; w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &ii;
    vkUpdateDescriptorSets(device_->get_device(), 1, &w, 0, nullptr);
}

void VulkanVolumetricLightPass::execute(VkCommandBuffer cmd,
                                        const glm::mat4& view,
                                        const glm::mat4& proj,
                                        const glm::vec3& camera_position) {
    if (!enabled_ || compute_pipeline_ == VK_NULL_HANDLE ||
        composite_pipeline_ == VK_NULL_HANDLE || ubo_mapped_ == nullptr) return;

    // Update the per-frame uniform block.
    VolUBO ub{};
    ub.inv_view_proj = glm::inverse(proj * view);
    ub.shadow_matrix = shadow_matrix_;
    ub.camera_pos    = glm::vec4(camera_position, 1.0f);
    ub.sun_dir       = glm::vec4(sun_direction_, config_.density);
    ub.sun_color     = glm::vec4(sun_color_, config_.anisotropy);
    ub.params        = glm::vec4(config_.max_distance, config_.intensity,
                                 static_cast<float>(config_.num_steps), config_.shadow_bias);
    ub.misc          = glm::vec4(static_cast<float>(scatter_w_),
                                 static_cast<float>(scatter_h_), 0.0f, 0.0f);
    ub.cloud_params  = cloud_params_;
    std::memcpy(ubo_mapped_, &ub, sizeof(ub));

    // Transition scatter target to GENERAL for the compute write.
    VkImageMemoryBarrier b{};
    b.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout                   = VK_IMAGE_LAYOUT_GENERAL;
    b.image                       = scatter_image_;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            compute_layout_, 0, 1, &compute_set_, 0, nullptr);
    const uint32_t gx = (scatter_w_ + 7) / 8;
    const uint32_t gy = (scatter_h_ + 7) / 8;
    vkCmdDispatch(cmd, gx, gy, 1);

    // Transition scatter target to SHADER_READ_ONLY for the composite.
    b.oldLayout     = VK_IMAGE_LAYOUT_GENERAL;
    b.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);

    VkRenderPassBeginInfo rpi{};
    rpi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass        = composite_render_pass_;
    rpi.framebuffer       = composite_framebuffer_;
    rpi.renderArea.offset = {0, 0};
    rpi.renderArea.extent = {width_, height_};
    vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp_state{};
    vp_state.width    = static_cast<float>(width_);
    vp_state.height   = static_cast<float>(height_);
    vp_state.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp_state);
    VkRect2D sc{}; sc.extent = {width_, height_};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, composite_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            composite_layout_, 0, 1, &composite_set_, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

} // namespace gws::renderer::gpu
