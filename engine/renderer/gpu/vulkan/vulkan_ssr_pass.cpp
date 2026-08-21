/**
 * @file vulkan_ssr_pass.cpp
 * @brief Implementation of the screen-space reflection pass.
 */

#include "vulkan_ssr_pass.h"
#include "vulkan_device.h"
#include "vulkan_g_buffer.h"
#include "ssr_spirv.h"

#include <spdlog/spdlog.h>
#include <array>
#include <cstring>
#include <cstdint>

namespace gws::renderer::gpu {

namespace {
constexpr VkFormat kReflectionFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
}

std::unique_ptr<VulkanSsrPass> VulkanSsrPass::create(VulkanDevice* device,
                                                     VulkanGBuffer* g_buffer,
                                                     VkImageView hdr_color_view,
                                                     VkFormat    hdr_format,
                                                     VkImageView env_cubemap_view,
                                                     VkSampler   env_cubemap_sampler,
                                                     uint32_t    width,
                                                     uint32_t    height,
                                                     bool        use_rt) {
    if (device == nullptr || g_buffer == nullptr || hdr_color_view == VK_NULL_HANDLE) {
        spdlog::error("VulkanSsrPass::create: bad args"); return nullptr;
    }
    if (width == 0 || height == 0) return nullptr;
    auto p = std::unique_ptr<VulkanSsrPass>(new VulkanSsrPass());
    p->use_rt_ = use_rt && device->has_ray_tracing();
    if (!p->initialize(device, g_buffer, hdr_color_view, hdr_format,
                       env_cubemap_view, env_cubemap_sampler, width, height)) {
        return nullptr;
    }
    spdlog::info("VulkanSsrPass created ({}x{}, {})", width, height,
                 p->use_rt_ ? "ray-traced" : "screen-space");
    return p;
}

VulkanSsrPass::~VulkanSsrPass() { destroy(); }

void VulkanSsrPass::destroy() {
    if (device_ == nullptr) return;
    VkDevice vk = device_->get_device();
    if (composite_pipeline_   != VK_NULL_HANDLE) { vkDestroyPipeline(vk, composite_pipeline_, nullptr); composite_pipeline_ = VK_NULL_HANDLE; }
    if (composite_layout_     != VK_NULL_HANDLE) { vkDestroyPipelineLayout(vk, composite_layout_, nullptr); composite_layout_ = VK_NULL_HANDLE; }
    if (composite_pool_       != VK_NULL_HANDLE) { vkDestroyDescriptorPool(vk, composite_pool_, nullptr); composite_pool_ = VK_NULL_HANDLE; composite_set_ = VK_NULL_HANDLE; }
    if (composite_dsl_        != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(vk, composite_dsl_, nullptr); composite_dsl_ = VK_NULL_HANDLE; }
    if (composite_framebuffer_ != VK_NULL_HANDLE) { vkDestroyFramebuffer(vk, composite_framebuffer_, nullptr); composite_framebuffer_ = VK_NULL_HANDLE; }
    if (composite_render_pass_ != VK_NULL_HANDLE) { vkDestroyRenderPass(vk, composite_render_pass_, nullptr); composite_render_pass_ = VK_NULL_HANDLE; }
    if (compute_pipeline_     != VK_NULL_HANDLE) { vkDestroyPipeline(vk, compute_pipeline_, nullptr); compute_pipeline_ = VK_NULL_HANDLE; }
    if (compute_layout_       != VK_NULL_HANDLE) { vkDestroyPipelineLayout(vk, compute_layout_, nullptr); compute_layout_ = VK_NULL_HANDLE; }
    if (compute_pool_         != VK_NULL_HANDLE) { vkDestroyDescriptorPool(vk, compute_pool_, nullptr); compute_pool_ = VK_NULL_HANDLE; compute_set_ = VK_NULL_HANDLE; }
    if (compute_dsl_          != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(vk, compute_dsl_, nullptr); compute_dsl_ = VK_NULL_HANDLE; }
    if (reflection_view_      != VK_NULL_HANDLE) { vkDestroyImageView(vk, reflection_view_, nullptr); reflection_view_ = VK_NULL_HANDLE; }
    if (reflection_image_     != VK_NULL_HANDLE) { vkDestroyImage(vk, reflection_image_, nullptr); reflection_image_ = VK_NULL_HANDLE; }
    if (reflection_memory_    != VK_NULL_HANDLE) { vkFreeMemory(vk, reflection_memory_, nullptr); reflection_memory_ = VK_NULL_HANDLE; }
    if (hdr_sampler_          != VK_NULL_HANDLE) { vkDestroySampler(vk, hdr_sampler_, nullptr); hdr_sampler_ = VK_NULL_HANDLE; }
    if (gbuffer_sampler_      != VK_NULL_HANDLE) { vkDestroySampler(vk, gbuffer_sampler_, nullptr); gbuffer_sampler_ = VK_NULL_HANDLE; }
    device_ = nullptr;
}

bool VulkanSsrPass::initialize(VulkanDevice* device, VulkanGBuffer* g_buffer,
                                VkImageView hdr_color_view, VkFormat hdr_format,
                                VkImageView env_cubemap_view, VkSampler env_cubemap_sampler,
                                uint32_t width, uint32_t height) {
    device_   = device;
    g_buffer_ = g_buffer;
    width_    = width;
    height_   = height;
    hdr_color_view_     = hdr_color_view;
    env_cubemap_view_   = env_cubemap_view;
    env_cubemap_sampler_ = env_cubemap_sampler;
    VkDevice vk = device_->get_device();

    // Samplers.
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
    if (!make_sampler(hdr_sampler_,     VK_FILTER_LINEAR))  return false;

    if (!create_reflection_target()) return false;
    if (!create_compute_pipeline())  return false;
    if (!create_composite_pipeline(hdr_color_view, hdr_format)) return false;
    return true;
}

bool VulkanSsrPass::create_reflection_target() {
    VkDevice vk = device_->get_device();
    VkImageCreateInfo ii{};
    ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType     = VK_IMAGE_TYPE_2D;
    ii.format        = kReflectionFormat;
    ii.extent        = { width_, height_, 1 };
    ii.mipLevels     = 1;
    ii.arrayLayers   = 1;
    ii.samples       = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ii.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(vk, &ii, nullptr, &reflection_image_) != VK_SUCCESS) return false;

    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(vk, reflection_image_, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = device_->find_memory_type(
        mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vk, &ai, nullptr, &reflection_memory_) != VK_SUCCESS) return false;
    vkBindImageMemory(vk, reflection_image_, reflection_memory_, 0);

    VkImageViewCreateInfo vi{};
    vi.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image            = reflection_image_;
    vi.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    vi.format           = kReflectionFormat;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    return vkCreateImageView(vk, &vi, nullptr, &reflection_view_) == VK_SUCCESS;
}

bool VulkanSsrPass::create_compute_pipeline() {
    VkDevice vk = device_->get_device();

    // 7 bindings in screen-space mode, +2 (TLAS, instance SSBO) in RT mode.
    std::array<VkDescriptorSetLayoutBinding, 10> b{};
    for (uint32_t i = 0; i < 5; ++i) {
        b[i].binding = i;
        b[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[i].descriptorCount = 1;
        b[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    b[5].binding = 5;
    b[5].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    b[5].descriptorCount = 1;
    b[5].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    b[6].binding = 6; // env cubemap — sampled on ray miss for sky reflection
    b[6].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[6].descriptorCount = 1;
    b[6].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    b[7].binding = 7; // scene TLAS (RT mode only)
    b[7].descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    b[7].descriptorCount = 1;
    b[7].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    b[8].binding = 8; // per-instance shading data SSBO (RT mode only)
    b[8].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b[8].descriptorCount = 1;
    b[8].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    b[9].binding = 9; // cloud-sky lat-long map for reflected clouds (RT only)
    b[9].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[9].descriptorCount = 1;
    b[9].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    const uint32_t binding_count = use_rt_ ? 10u : 7u;
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = binding_count;
    li.pBindings    = b.data();
    if (vkCreateDescriptorSetLayout(vk, &li, nullptr, &compute_dsl_) != VK_SUCCESS) return false;

    VkPushConstantRange pr{};
    pr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    // mat4(64) + camera vec4(16) + ivec2+4floats(24, padded to 32 for the
    // struct's _pad) + sun_dir vec4(16) + sun_color vec4(16) + ambient
    // vec4(16) = 160. Round to 160.
    pr.size       = 160;
    VkPipelineLayoutCreateInfo pli{};
    pli.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount         = 1;
    pli.pSetLayouts            = &compute_dsl_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges    = &pr;
    if (vkCreatePipelineLayout(vk, &pli, nullptr, &compute_layout_) != VK_SUCCESS) return false;

    VkShaderModuleCreateInfo smi{};
    smi.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = use_rt_ ? kSsrRtComputeSpv_size : kSsrComputeSpv_size;
    smi.pCode    = use_rt_ ? kSsrRtComputeSpv      : kSsrComputeSpv;
    VkShaderModule sm = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vk, &smi, nullptr, &sm) != VK_SUCCESS) return false;
    VkComputePipelineCreateInfo cpi{};
    cpi.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpi.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.module = sm;
    cpi.stage.pName  = "main";
    cpi.layout       = compute_layout_;
    VkResult r = vkCreateComputePipelines(vk, device_->get_pipeline_cache(), 1, &cpi, nullptr, &compute_pipeline_);
    vkDestroyShaderModule(vk, sm, nullptr);
    if (r != VK_SUCCESS) return false;

    // Descriptor pool + set.
    std::array<VkDescriptorPoolSize, 4> ps{};
    ps[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;      ps[0].descriptorCount = 7; // 5 gbuffer/hdr + cubemap + cloudSky
    ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;              ps[1].descriptorCount = 1;
    ps[2].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR; ps[2].descriptorCount = 1;
    ps[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;             ps[3].descriptorCount = 1;
    VkDescriptorPoolCreateInfo pi{};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets       = 1;
    pi.poolSizeCount = use_rt_ ? 4u : 2u;
    pi.pPoolSizes    = ps.data();
    if (vkCreateDescriptorPool(vk, &pi, nullptr, &compute_pool_) != VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo dai{};
    dai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool     = compute_pool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts        = &compute_dsl_;
    if (vkAllocateDescriptorSets(vk, &dai, &compute_set_) != VK_SUCCESS) return false;

    // Write all bindings now — none of these views change for the
    // lifetime of the pass. 0..3 = G-Buffer reads, 4 = lit HDR (hit
    // colour), 5 = reflection storage image, 6 = env cubemap (sky on miss).
    VkImageLayout sr = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkImageLayout dr = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    std::array<VkDescriptorImageInfo, 7> ii{};
    ii[0] = { gbuffer_sampler_, g_buffer_->get_position_view(), sr };
    ii[1] = { gbuffer_sampler_, g_buffer_->get_normal_view(),   sr };
    ii[2] = { gbuffer_sampler_, g_buffer_->get_depth_view(),    dr };
    ii[3] = { gbuffer_sampler_, g_buffer_->get_albedo_view(),   sr };
    ii[4] = { hdr_sampler_,     hdr_color_view_,                sr };
    ii[5] = { VK_NULL_HANDLE,   reflection_view_,               VK_IMAGE_LAYOUT_GENERAL };
    ii[6] = { env_cubemap_sampler_, env_cubemap_view_,          sr };
    std::array<VkWriteDescriptorSet, 7> ws{};
    for (uint32_t i = 0; i < 5; ++i) {
        ws[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[i].dstSet          = compute_set_;
        ws[i].dstBinding      = i;
        ws[i].descriptorCount = 1;
        ws[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ws[i].pImageInfo      = &ii[i];
    }
    ws[5].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ws[5].dstSet          = compute_set_;
    ws[5].dstBinding      = 5;
    ws[5].descriptorCount = 1;
    ws[5].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ws[5].pImageInfo      = &ii[5];
    ws[6].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ws[6].dstSet          = compute_set_;
    ws[6].dstBinding      = 6;
    ws[6].descriptorCount = 1;
    ws[6].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ws[6].pImageInfo      = &ii[6];
    vkUpdateDescriptorSets(vk, static_cast<uint32_t>(ws.size()), ws.data(), 0, nullptr);

    // Binding 9 (cloudSky, RT only) — placeholder (the lit HDR view) until
    // set_cloud_sky() supplies the real map. Harmless: the shader samples it
    // only when pc.ambient.w > 0.5, which stays 0 until clouds bind a map.
    if (use_rt_) {
        VkDescriptorImageInfo cloud_ph{ hdr_sampler_, hdr_color_view_,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet cw{};
        cw.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        cw.dstSet = compute_set_; cw.dstBinding = 9; cw.descriptorCount = 1;
        cw.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; cw.pImageInfo = &cloud_ph;
        vkUpdateDescriptorSets(vk, 1, &cw, 0, nullptr);
    }

    return true;
}

bool VulkanSsrPass::create_composite_pipeline(VkImageView hdr_color_view, VkFormat hdr_format) {
    VkDevice vk = device_->get_device();

    // Render pass: HDR colour LOAD_OP_LOAD (keep lit content), STORE.
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
    rpi.attachmentCount = 1; rpi.pAttachments = &att;
    rpi.subpassCount    = 1; rpi.pSubpasses = &sub;
    rpi.dependencyCount = 1; rpi.pDependencies = &dep;
    if (vkCreateRenderPass(vk, &rpi, nullptr, &composite_render_pass_) != VK_SUCCESS) return false;

    VkFramebufferCreateInfo fbi{};
    fbi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbi.renderPass      = composite_render_pass_;
    fbi.attachmentCount = 1;
    fbi.pAttachments    = &hdr_color_view;
    fbi.width           = width_; fbi.height = height_; fbi.layers = 1;
    if (vkCreateFramebuffer(vk, &fbi, nullptr, &composite_framebuffer_) != VK_SUCCESS) return false;

    // Descriptor + pipeline.
    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 1; li.pBindings = &b;
    if (vkCreateDescriptorSetLayout(vk, &li, nullptr, &composite_dsl_) != VK_SUCCESS) return false;

    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1; pli.pSetLayouts = &composite_dsl_;
    if (vkCreatePipelineLayout(vk, &pli, nullptr, &composite_layout_) != VK_SUCCESS) return false;

    VkDescriptorPoolSize ps{};
    ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps.descriptorCount = 1;
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

    VkDescriptorImageInfo ii{};
    ii.sampler     = hdr_sampler_;
    ii.imageView   = reflection_view_;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = composite_set_;
    w.dstBinding      = 0;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo      = &ii;
    vkUpdateDescriptorSets(vk, 1, &w, 0, nullptr);

    // Pipeline.
    VkShaderModuleCreateInfo smvi{};
    smvi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smvi.codeSize = kSsrCompositeVertSpv_size; smvi.pCode = kSsrCompositeVertSpv;
    VkShaderModule sv = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vk, &smvi, nullptr, &sv) != VK_SUCCESS) return false;
    VkShaderModuleCreateInfo smfi{};
    smfi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smfi.codeSize = kSsrCompositeFragSpv_size; smfi.pCode = kSsrCompositeFragSpv;
    VkShaderModule sf = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vk, &smfi, nullptr, &sf) != VK_SUCCESS) { vkDestroyShaderModule(vk, sv, nullptr); return false; }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = sv; stages[0].pName = "main";
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

    // Alpha-blend the reflection onto HDR.
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask     = 0xF;
    cba.blendEnable        = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
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
    VkResult res = vkCreateGraphicsPipelines(vk, device_->get_pipeline_cache(), 1, &gpi, nullptr, &composite_pipeline_);
    vkDestroyShaderModule(vk, sv, nullptr);
    vkDestroyShaderModule(vk, sf, nullptr);
    return res == VK_SUCCESS;
}

void VulkanSsrPass::set_tlas(VkAccelerationStructureKHR tlas) {
    if (!use_rt_) return;
    if (tlas == tlas_ || tlas == VK_NULL_HANDLE) { tlas_ = tlas; return; }
    tlas_ = tlas;
    VkWriteDescriptorSetAccelerationStructureKHR as_write{};
    as_write.sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    as_write.accelerationStructureCount = 1;
    as_write.pAccelerationStructures    = &tlas_;
    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.pNext           = &as_write;
    w.dstSet          = compute_set_;
    w.dstBinding      = 7;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    vkUpdateDescriptorSets(device_->get_device(), 1, &w, 0, nullptr);
}

void VulkanSsrPass::set_instance_data_buffer(VkBuffer buffer) {
    if (!use_rt_) return;
    if (buffer == instance_data_buffer_ || buffer == VK_NULL_HANDLE) {
        instance_data_buffer_ = buffer; return;
    }
    instance_data_buffer_ = buffer;
    VkDescriptorBufferInfo bi{};
    bi.buffer = instance_data_buffer_;
    bi.offset = 0;
    bi.range  = VK_WHOLE_SIZE;
    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = compute_set_;
    w.dstBinding      = 8;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w.pBufferInfo     = &bi;
    vkUpdateDescriptorSets(device_->get_device(), 1, &w, 0, nullptr);
}

void VulkanSsrPass::set_cloud_sky(VkImageView view, VkSampler sampler) {
    if (!use_rt_ || view == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE ||
        compute_set_ == VK_NULL_HANDLE) return;
    cloud_sky_view_ = view;
    cloud_sky_sampler_ = sampler;
    VkDescriptorImageInfo ii{ sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = compute_set_; w.dstBinding = 9; w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &ii;
    vkUpdateDescriptorSets(device_->get_device(), 1, &w, 0, nullptr);
}

void VulkanSsrPass::execute(VkCommandBuffer cmd,
                             const glm::mat4& view,
                             const glm::mat4& proj,
                             const glm::vec3& camera_position) {
    if (!enabled_ || compute_pipeline_ == VK_NULL_HANDLE ||
        composite_pipeline_ == VK_NULL_HANDLE) return;
    // In RT mode, don't dispatch until both the TLAS (binding 7) and the
    // instance-data SSBO (binding 8) are bound — an unbound descriptor
    // referenced by the pipeline is a driver hang.
    if (use_rt_ && (tlas_ == VK_NULL_HANDLE || instance_data_buffer_ == VK_NULL_HANDLE)) return;

    // Transition reflection target to GENERAL for compute write.
    VkImageMemoryBarrier b{};
    b.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout                   = VK_IMAGE_LAYOUT_GENERAL;
    b.image                       = reflection_image_;
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

    struct PC {
        float view_proj[16];
        float camera_pos[4];
        int32_t output_w, output_h;
        float max_distance;
        float thickness;
        int32_t num_steps;
        float fresnel_power;
        float _pad0, _pad1;
        float sun_dir[4];     // xyz = direction, w unused
        float sun_color[4];   // rgb = color*intensity, w unused
        float ambient[4];     // rgb = color*intensity, w unused
    } pc{};
    const glm::mat4 vp = proj * view;
    std::memcpy(pc.view_proj, &vp[0][0], sizeof(pc.view_proj));
    pc.camera_pos[0] = camera_position.x;
    pc.camera_pos[1] = camera_position.y;
    pc.camera_pos[2] = camera_position.z;
    pc.camera_pos[3] = 0.0f;
    pc.output_w  = static_cast<int32_t>(width_);
    pc.output_h  = static_cast<int32_t>(height_);
    pc.max_distance  = config_.max_distance;
    pc.thickness     = config_.thickness;
    pc.num_steps     = config_.num_steps;
    pc.fresnel_power = config_.fresnel_power;
    pc.sun_dir[0] = sun_direction_.x; pc.sun_dir[1] = sun_direction_.y; pc.sun_dir[2] = sun_direction_.z;
    pc.sun_color[0] = sun_color_.x; pc.sun_color[1] = sun_color_.y; pc.sun_color[2] = sun_color_.z;
    pc.ambient[0] = ambient_color_.x * ambient_intensity_;
    pc.ambient[1] = ambient_color_.y * ambient_intensity_;
    pc.ambient[2] = ambient_color_.z * ambient_intensity_;
    pc.ambient[3] = cloud_sky_enabled_ ? 1.0f : 0.0f; // reflected-clouds gate
    vkCmdPushConstants(cmd, compute_layout_, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(pc), &pc);

    const uint32_t gx = (width_ + 7) / 8;
    const uint32_t gy = (height_ + 7) / 8;
    vkCmdDispatch(cmd, gx, gy, 1);

    // Transition reflection target to SHADER_READ_ONLY for the composite pass.
    b.oldLayout     = VK_IMAGE_LAYOUT_GENERAL;
    b.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);

    // Composite pass.
    VkRenderPassBeginInfo rpi{};
    rpi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass        = composite_render_pass_;
    rpi.framebuffer       = composite_framebuffer_;
    rpi.renderArea.offset = {0, 0};
    rpi.renderArea.extent = {width_, height_};
    vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp_state{};
    vp_state.width  = static_cast<float>(width_);
    vp_state.height = static_cast<float>(height_);
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
