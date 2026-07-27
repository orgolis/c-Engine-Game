/**
 * @file vulkan_ddgi_pass.cpp
 * @brief DDGI probe-grid GI — see vulkan_ddgi_pass.h.
 */

#include "vulkan_ddgi_pass.h"
#include "vulkan_device.h"
#include "vulkan_g_buffer.h"
#include "ddgi_spirv.h"

#include <spdlog/spdlog.h>
#include <array>
#include <cstring>
#include <vector>

namespace gws::renderer::gpu {

namespace {
constexpr int kIrrTile = 10;   // 8x8 interior + 1px border
constexpr int kDepTile = 18;   // 16x16 interior + 1px border

// Must match the shaders' DdgiUBO (std140).
struct DdgiUBO {
    glm::vec4  origin;     // xyz + intensity
    glm::vec4  spacing;    // xyz + hysteresis
    glm::ivec4 counts;     // xyz + frame index
    glm::vec4  sun_dir;    // xyz + normal bias
    glm::vec4  sun_color;  // rgb + max ray distance
    glm::vec4  ambient;    // rgb + enabled
};

VkShaderModule make_module(VkDevice dev, const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = bytes;
    ci.pCode    = code;
    VkShaderModule m = VK_NULL_HANDLE;
    return vkCreateShaderModule(dev, &ci, nullptr, &m) == VK_SUCCESS ? m : VK_NULL_HANDLE;
}
}  // namespace

VulkanDdgiPass::~VulkanDdgiPass() { destroy(); }

std::unique_ptr<VulkanDdgiPass> VulkanDdgiPass::create(
    VulkanDevice* device, VulkanGBuffer* g_buffer,
    VkImageView hdr_color_view, VkFormat hdr_format,
    VkImageView env_cubemap_view, VkSampler env_cubemap_sampler,
    uint32_t width, uint32_t height, const DdgiConfig& config) {
    if (!device || !device->has_ray_tracing()) {
        spdlog::info("VulkanDdgiPass: hardware ray tracing unavailable — DDGI disabled");
        return nullptr;
    }
    auto pass = std::make_unique<VulkanDdgiPass>();
    pass->config_ = config;
    if (!pass->initialize(device, g_buffer, hdr_color_view, hdr_format,
                          env_cubemap_view, env_cubemap_sampler, width, height)) {
        spdlog::error("VulkanDdgiPass: initialization failed");
        return nullptr;
    }
    spdlog::info("VulkanDdgiPass created ({} probes {}x{}x{}, irr atlas {}x{}, depth atlas {}x{})",
                 pass->probe_count(), config.counts.x, config.counts.y, config.counts.z,
                 config.counts.x * kIrrTile, config.counts.y * config.counts.z * kIrrTile,
                 config.counts.x * kDepTile, config.counts.y * config.counts.z * kDepTile);
    return pass;
}

bool VulkanDdgiPass::create_atlas(VkFormat format, uint32_t w, uint32_t h,
                                  VkImage& img, VkDeviceMemory& mem, VkImageView& view) {
    VkDevice vk = device_->get_device();
    VkImageCreateInfo ii{};
    ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType     = VK_IMAGE_TYPE_2D;
    ii.format        = format;
    ii.extent        = {w, h, 1};
    ii.mipLevels     = 1;
    ii.arrayLayers   = 1;
    ii.samples       = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ii.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(vk, &ii, nullptr, &img) != VK_SUCCESS) return false;

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(vk, img, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = device_->find_memory_type(
        req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vk, &ai, nullptr, &mem) != VK_SUCCESS) return false;
    vkBindImageMemory(vk, img, mem, 0);

    VkImageViewCreateInfo vi{};
    vi.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image                       = img;
    vi.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    vi.format                      = format;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    return vkCreateImageView(vk, &vi, nullptr, &view) == VK_SUCCESS;
}

bool VulkanDdgiPass::initialize(VulkanDevice* device, VulkanGBuffer* g_buffer,
                                VkImageView hdr_color_view, VkFormat hdr_format,
                                VkImageView env_cubemap_view, VkSampler env_cubemap_sampler,
                                uint32_t width, uint32_t height) {
    device_   = device;
    g_buffer_ = g_buffer;
    width_    = width;
    height_   = height;
    VkDevice vk = device->get_device();

    // ---- samplers ----
    {
        VkSamplerCreateInfo si{};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = VK_FILTER_NEAREST;
        si.minFilter    = VK_FILTER_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(vk, &si, nullptr, &gbuffer_sampler_) != VK_SUCCESS) return false;
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        if (vkCreateSampler(vk, &si, nullptr, &atlas_sampler_) != VK_SUCCESS) return false;
    }

    // ---- UBO (persistently mapped) ----
    {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size  = sizeof(DdgiUBO);
        bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if (vkCreateBuffer(vk, &bi, nullptr, &ubo_) != VK_SUCCESS) return false;
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(vk, ubo_, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = req.size;
        ai.memoryTypeIndex = device->find_memory_type(
            req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(vk, &ai, nullptr, &ubo_memory_) != VK_SUCCESS) return false;
        vkBindBufferMemory(vk, ubo_, ubo_memory_, 0);
        if (vkMapMemory(vk, ubo_memory_, 0, sizeof(DdgiUBO), 0, &ubo_mapped_) != VK_SUCCESS)
            return false;
    }

    // ---- probe atlases ----
    const uint32_t rows = static_cast<uint32_t>(config_.counts.y * config_.counts.z);
    if (!create_atlas(VK_FORMAT_R16G16B16A16_SFLOAT,
                      config_.counts.x * kIrrTile, rows * kIrrTile,
                      irr_image_, irr_memory_, irr_view_)) return false;
    if (!create_atlas(VK_FORMAT_R16G16_SFLOAT,
                      config_.counts.x * kDepTile, rows * kDepTile,
                      dep_image_, dep_memory_, dep_view_)) return false;

    // ---- descriptor pool (trace + composite sets) ----
    {
        std::array<VkDescriptorPoolSize, 5> ps{};
        ps[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2};
        ps[1] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2};
        ps[2] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 7};
        ps[3] = {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1};
        ps[4] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
        VkDescriptorPoolCreateInfo pi{};
        pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets       = 2;
        pi.poolSizeCount = static_cast<uint32_t>(ps.size());
        pi.pPoolSizes    = ps.data();
        if (vkCreateDescriptorPool(vk, &pi, nullptr, &pool_) != VK_SUCCESS) return false;
    }

    auto make_set = [&](std::initializer_list<VkDescriptorSetLayoutBinding> binds,
                        VkDescriptorSetLayout& dsl, VkDescriptorSet& set) -> bool {
        std::vector<VkDescriptorSetLayoutBinding> b(binds);
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = static_cast<uint32_t>(b.size());
        li.pBindings    = b.data();
        if (vkCreateDescriptorSetLayout(vk, &li, nullptr, &dsl) != VK_SUCCESS) return false;
        VkDescriptorSetAllocateInfo si{};
        si.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        si.descriptorPool     = pool_;
        si.descriptorSetCount = 1;
        si.pSetLayouts        = &dsl;
        return vkAllocateDescriptorSets(vk, &si, &set) == VK_SUCCESS;
    };
    auto binding = [](uint32_t i, VkDescriptorType t, VkShaderStageFlags s) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = i; b.descriptorType = t; b.descriptorCount = 1; b.stageFlags = s;
        return b;
    };

    const VkShaderStageFlags CS = VK_SHADER_STAGE_COMPUTE_BIT;
    const VkShaderStageFlags FS = VK_SHADER_STAGE_FRAGMENT_BIT;
    if (!make_set({binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, CS),
                   binding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, CS),
                   binding(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, CS),
                   binding(3, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, CS),
                   binding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, CS),
                   binding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, CS)},
                  trace_dsl_, trace_set_)) return false;
    if (!make_set({binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, FS),
                   binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FS),
                   binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FS),
                   binding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FS),
                   binding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FS),
                   binding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FS),
                   binding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FS)},
                  composite_dsl_, composite_set_)) return false;

    // ---- static descriptor writes (TLAS + instance SSBO come via set_*) ----
    {
        const VkImageLayout sr = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        const VkImageLayout dr = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        VkDescriptorBufferInfo ubo_bi{ubo_, 0, sizeof(DdgiUBO)};
        VkDescriptorImageInfo irr_st{VK_NULL_HANDLE, irr_view_, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo dep_st{VK_NULL_HANDLE, dep_view_, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo env_ci{env_cubemap_sampler, env_cubemap_view, sr};
        VkDescriptorImageInfo pos_ci{gbuffer_sampler_, g_buffer_->get_position_view(), sr};
        VkDescriptorImageInfo nrm_ci{gbuffer_sampler_, g_buffer_->get_normal_view(), sr};
        VkDescriptorImageInfo alb_ci{gbuffer_sampler_, g_buffer_->get_albedo_view(), sr};
        VkDescriptorImageInfo dpt_ci{gbuffer_sampler_, g_buffer_->get_depth_view(), dr};
        VkDescriptorImageInfo irr_ci{atlas_sampler_, irr_view_, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo dep_ci{atlas_sampler_, dep_view_, VK_IMAGE_LAYOUT_GENERAL};

        std::vector<VkWriteDescriptorSet> w;
        auto add = [&](VkDescriptorSet set, uint32_t bind, VkDescriptorType t,
                       const VkDescriptorImageInfo* img, const VkDescriptorBufferInfo* buf) {
            VkWriteDescriptorSet x{};
            x.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            x.dstSet          = set;
            x.dstBinding      = bind;
            x.descriptorCount = 1;
            x.descriptorType  = t;
            x.pImageInfo      = img;
            x.pBufferInfo     = buf;
            w.push_back(x);
        };
        add(trace_set_, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &ubo_bi);
        add(trace_set_, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &irr_st, nullptr);
        add(trace_set_, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dep_st, nullptr);
        add(trace_set_, 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &env_ci, nullptr);
        add(composite_set_, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &ubo_bi);
        add(composite_set_, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &pos_ci, nullptr);
        add(composite_set_, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &nrm_ci, nullptr);
        add(composite_set_, 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &alb_ci, nullptr);
        add(composite_set_, 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &dpt_ci, nullptr);
        add(composite_set_, 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &irr_ci, nullptr);
        add(composite_set_, 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &dep_ci, nullptr);
        vkUpdateDescriptorSets(vk, static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);
    }

    // ---- trace compute pipeline ----
    {
        VkShaderModule sm = make_module(vk, kDdgiTraceSpv, kDdgiTraceSpv_size);
        if (!sm) return false;
        VkPipelineLayoutCreateInfo pli{};
        pli.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1;
        pli.pSetLayouts    = &trace_dsl_;
        if (vkCreatePipelineLayout(vk, &pli, nullptr, &trace_layout_) != VK_SUCCESS) {
            vkDestroyShaderModule(vk, sm, nullptr);
            return false;
        }
        VkComputePipelineCreateInfo ci{};
        ci.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        ci.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ci.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        ci.stage.module = sm;
        ci.stage.pName  = "main";
        ci.layout       = trace_layout_;
        const VkResult r = vkCreateComputePipelines(vk, VK_NULL_HANDLE, 1, &ci,
                                                    nullptr, &trace_pipeline_);
        vkDestroyShaderModule(vk, sm, nullptr);
        if (r != VK_SUCCESS) return false;
    }

    // ---- composite render pass over the borrowed HDR view (LOAD) ----
    {
        VkAttachmentDescription att{};
        att.format         = hdr_format;
        att.samples        = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
        att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        att.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sp{};
        sp.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sp.colorAttachmentCount = 1;
        sp.pColorAttachments    = &ref;
        VkSubpassDependency dep{};
        dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass    = 0;
        dep.srcStageMask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo rp{};
        rp.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.attachmentCount = 1;
        rp.pAttachments    = &att;
        rp.subpassCount    = 1;
        rp.pSubpasses      = &sp;
        rp.dependencyCount = 1;
        rp.pDependencies   = &dep;
        if (vkCreateRenderPass(vk, &rp, nullptr, &composite_render_pass_) != VK_SUCCESS)
            return false;
        VkFramebufferCreateInfo fb{};
        fb.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass      = composite_render_pass_;
        fb.attachmentCount = 1;
        fb.pAttachments    = &hdr_color_view;
        fb.width           = width;
        fb.height          = height;
        fb.layers          = 1;
        if (vkCreateFramebuffer(vk, &fb, nullptr, &composite_framebuffer_) != VK_SUCCESS)
            return false;
    }

    // ---- composite pipeline (fullscreen, additive ONE/ONE) ----
    {
        VkShaderModule vs = make_module(vk, kDdgiCompositeVertSpv, kDdgiCompositeVertSpv_size);
        VkShaderModule fs = make_module(vk, kDdgiCompositeFragSpv, kDdgiCompositeFragSpv_size);
        if (!vs || !fs) {
            if (vs) vkDestroyShaderModule(vk, vs, nullptr);
            if (fs) vkDestroyShaderModule(vk, fs, nullptr);
            return false;
        }
        VkPipelineLayoutCreateInfo pli{};
        pli.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1;
        pli.pSetLayouts    = &composite_dsl_;
        if (vkCreatePipelineLayout(vk, &pli, nullptr, &composite_layout_) != VK_SUCCESS) {
            vkDestroyShaderModule(vk, vs, nullptr);
            vkDestroyShaderModule(vk, fs, nullptr);
            return false;
        }
        std::array<VkPipelineShaderStageCreateInfo, 2> st{};
        st[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   st[0].module = vs; st[0].pName = "main";
        st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = fs; st[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        std::array<VkDynamicState, 2> dyn{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dsi{};
        dsi.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dsi.dynamicStateCount = static_cast<uint32_t>(dyn.size());
        dsi.pDynamicStates    = dyn.data();
        VkPipelineViewportStateCreateInfo vp{};
        vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1;
        vp.scissorCount  = 1;
        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode    = VK_CULL_MODE_NONE;
        rs.lineWidth   = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        cba.blendEnable         = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;   // additive GI
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.colorBlendOp        = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.alphaBlendOp        = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1;
        cb.pAttachments    = &cba;

        VkGraphicsPipelineCreateInfo gp{};
        gp.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.stageCount          = static_cast<uint32_t>(st.size());
        gp.pStages             = st.data();
        gp.pVertexInputState   = &vi;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState      = &vp;
        gp.pRasterizationState = &rs;
        gp.pMultisampleState   = &ms;
        gp.pDepthStencilState  = &ds;
        gp.pColorBlendState    = &cb;
        gp.pDynamicState       = &dsi;
        gp.layout              = composite_layout_;
        gp.renderPass          = composite_render_pass_;
        gp.subpass             = 0;
        const VkResult r = vkCreateGraphicsPipelines(vk, VK_NULL_HANDLE, 1, &gp,
                                                     nullptr, &composite_pipeline_);
        vkDestroyShaderModule(vk, vs, nullptr);
        vkDestroyShaderModule(vk, fs, nullptr);
        if (r != VK_SUCCESS) return false;
    }
    return true;
}

void VulkanDdgiPass::set_tlas(VkAccelerationStructureKHR tlas) {
    if (tlas == tlas_ || tlas == VK_NULL_HANDLE) { tlas_ = tlas; return; }
    tlas_ = tlas;
    VkWriteDescriptorSetAccelerationStructureKHR as_write{};
    as_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    as_write.accelerationStructureCount = 1;
    as_write.pAccelerationStructures    = &tlas_;
    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.pNext           = &as_write;
    w.dstSet          = trace_set_;
    w.dstBinding      = 3;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    vkUpdateDescriptorSets(device_->get_device(), 1, &w, 0, nullptr);
}

void VulkanDdgiPass::set_instance_data_buffer(VkBuffer buffer) {
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
    w.dstSet          = trace_set_;
    w.dstBinding      = 4;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w.pBufferInfo     = &bi;
    vkUpdateDescriptorSets(device_->get_device(), 1, &w, 0, nullptr);
}

void VulkanDdgiPass::execute_trace(VkCommandBuffer cmd) {
    if (!enabled_ || trace_pipeline_ == VK_NULL_HANDLE) return;
    if (tlas_ == VK_NULL_HANDLE || instance_data_buffer_ == VK_NULL_HANDLE) return;

    DdgiUBO u{};
    u.origin    = glm::vec4(config_.origin, config_.intensity);
    u.spacing   = glm::vec4(config_.spacing, config_.hysteresis);
    u.counts    = glm::ivec4(config_.counts, static_cast<int>(frame_index_));
    u.sun_dir   = glm::vec4(sun_direction_, config_.normal_bias);
    u.sun_color = glm::vec4(sun_color_, config_.max_ray_dist);
    u.ambient   = glm::vec4(config_.ambient, 1.0f);   // w = enabled
    std::memcpy(ubo_mapped_, &u, sizeof(u));
    ++frame_index_;

    auto atlas_barrier = [&](VkImage img, VkImageLayout old_layout,
                             VkAccessFlags src, VkAccessFlags dst,
                             VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage) {
        VkImageMemoryBarrier b{};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout           = old_layout;
        b.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = img;
        b.srcAccessMask       = src;
        b.dstAccessMask       = dst;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &b);
    };

    // First use: UNDEFINED -> GENERAL; afterwards make prior composite reads
    // (and the previous frame's trace writes) visible to this trace.
    const VkImageLayout old_l =
        atlases_initialized_ ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
    atlas_barrier(irr_image_, old_l,
                  VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                  VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    atlas_barrier(dep_image_, old_l,
                  VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                  VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    atlases_initialized_ = true;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, trace_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, trace_layout_,
                            0, 1, &trace_set_, 0, nullptr);
    vkCmdDispatch(cmd, probe_count(), 1, 1);

    // Trace writes -> composite fragment reads.
    atlas_barrier(irr_image_, VK_IMAGE_LAYOUT_GENERAL,
                  VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    atlas_barrier(dep_image_, VK_IMAGE_LAYOUT_GENERAL,
                  VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

void VulkanDdgiPass::execute_composite(VkCommandBuffer cmd) {
    if (!enabled_ || composite_pipeline_ == VK_NULL_HANDLE) return;
    if (!atlases_initialized_) return;   // nothing traced yet

    VkRenderPassBeginInfo rb{};
    rb.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rb.renderPass        = composite_render_pass_;
    rb.framebuffer       = composite_framebuffer_;
    rb.renderArea.extent = {width_, height_};
    vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{};
    vp.width    = static_cast<float>(width_);
    vp.height   = static_cast<float>(height_);
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, {width_, height_}};
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, composite_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, composite_layout_,
                            0, 1, &composite_set_, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

void VulkanDdgiPass::destroy() {
    if (!device_) return;
    VkDevice vk = device_->get_device();
    if (trace_pipeline_)   vkDestroyPipeline(vk, trace_pipeline_, nullptr);
    if (trace_layout_)     vkDestroyPipelineLayout(vk, trace_layout_, nullptr);
    if (trace_dsl_)        vkDestroyDescriptorSetLayout(vk, trace_dsl_, nullptr);
    if (composite_pipeline_) vkDestroyPipeline(vk, composite_pipeline_, nullptr);
    if (composite_layout_)   vkDestroyPipelineLayout(vk, composite_layout_, nullptr);
    if (composite_dsl_)      vkDestroyDescriptorSetLayout(vk, composite_dsl_, nullptr);
    if (composite_framebuffer_) vkDestroyFramebuffer(vk, composite_framebuffer_, nullptr);
    if (composite_render_pass_) vkDestroyRenderPass(vk, composite_render_pass_, nullptr);
    if (pool_)             vkDestroyDescriptorPool(vk, pool_, nullptr);
    if (irr_view_)         vkDestroyImageView(vk, irr_view_, nullptr);
    if (irr_image_)        vkDestroyImage(vk, irr_image_, nullptr);
    if (irr_memory_)       vkFreeMemory(vk, irr_memory_, nullptr);
    if (dep_view_)         vkDestroyImageView(vk, dep_view_, nullptr);
    if (dep_image_)        vkDestroyImage(vk, dep_image_, nullptr);
    if (dep_memory_)       vkFreeMemory(vk, dep_memory_, nullptr);
    if (ubo_mapped_)       vkUnmapMemory(vk, ubo_memory_);
    if (ubo_)              vkDestroyBuffer(vk, ubo_, nullptr);
    if (ubo_memory_)       vkFreeMemory(vk, ubo_memory_, nullptr);
    if (gbuffer_sampler_)  vkDestroySampler(vk, gbuffer_sampler_, nullptr);
    if (atlas_sampler_)    vkDestroySampler(vk, atlas_sampler_, nullptr);
    trace_pipeline_ = VK_NULL_HANDLE; trace_layout_ = VK_NULL_HANDLE; trace_dsl_ = VK_NULL_HANDLE;
    composite_pipeline_ = VK_NULL_HANDLE; composite_layout_ = VK_NULL_HANDLE;
    composite_dsl_ = VK_NULL_HANDLE;
    composite_framebuffer_ = VK_NULL_HANDLE; composite_render_pass_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    irr_view_ = VK_NULL_HANDLE; irr_image_ = VK_NULL_HANDLE; irr_memory_ = VK_NULL_HANDLE;
    dep_view_ = VK_NULL_HANDLE; dep_image_ = VK_NULL_HANDLE; dep_memory_ = VK_NULL_HANDLE;
    ubo_mapped_ = nullptr; ubo_ = VK_NULL_HANDLE; ubo_memory_ = VK_NULL_HANDLE;
    gbuffer_sampler_ = VK_NULL_HANDLE; atlas_sampler_ = VK_NULL_HANDLE;
    device_ = nullptr;
}

} // namespace gws::renderer::gpu
