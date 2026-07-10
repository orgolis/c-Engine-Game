/**
 * @file vulkan_froxel_fog_pass.cpp
 * @brief Froxel volumetric fog — see vulkan_froxel_fog_pass.h.
 */

#include "vulkan_froxel_fog_pass.h"
#include "vulkan_device.h"
#include "vulkan_g_buffer.h"
#include "froxel_fog_spirv.h"

#include <spdlog/spdlog.h>
#include <array>
#include <cstring>

namespace gws::renderer::gpu {

namespace {
constexpr uint32_t kFroxelX = 160;
constexpr uint32_t kFroxelY = 90;
constexpr uint32_t kFroxelZ = 64;

// Must match the shaders' FroxelUBO (std140).
struct FroxelUBO {
    glm::mat4 inv_view_proj;
    glm::mat4 shadow_matrix;
    glm::vec4 cam_pos;      // xyz + light_count
    glm::vec4 sun_dir;      // xyz + anisotropy
    glm::vec4 sun_color;    // rgb + sun_intensity
    glm::vec4 fog;          // density, height_base, height_falloff, far
    glm::vec4 ambient;      // rgb + local_intensity
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

VulkanFroxelFogPass::~VulkanFroxelFogPass() { destroy(); }

std::unique_ptr<VulkanFroxelFogPass> VulkanFroxelFogPass::create(
    VulkanDevice* device, VulkanGBuffer* g_buffer,
    VkImageView hdr_color_view, VkFormat hdr_format,
    VkImageView shadow_view, VkSampler shadow_sampler,
    VkBuffer light_buffer, uint32_t width, uint32_t height) {
    auto pass = std::make_unique<VulkanFroxelFogPass>();
    if (!pass->initialize(device, g_buffer, hdr_color_view, hdr_format,
                          shadow_view, shadow_sampler, light_buffer, width, height)) {
        spdlog::error("VulkanFroxelFogPass: initialization failed");
        return nullptr;
    }
    spdlog::info("VulkanFroxelFogPass created ({}x{}, froxels {}x{}x{})",
                 width, height, kFroxelX, kFroxelY, kFroxelZ);
    return pass;
}

bool VulkanFroxelFogPass::create_volume(VkImage& img, VkDeviceMemory& mem,
                                        VkImageView& view) {
    VkDevice vk = device_->get_device();
    VkImageCreateInfo ii{};
    ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType     = VK_IMAGE_TYPE_3D;
    ii.format        = VK_FORMAT_R16G16B16A16_SFLOAT;
    ii.extent        = {kFroxelX, kFroxelY, kFroxelZ};
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
    vi.viewType                    = VK_IMAGE_VIEW_TYPE_3D;
    vi.format                      = VK_FORMAT_R16G16B16A16_SFLOAT;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    return vkCreateImageView(vk, &vi, nullptr, &view) == VK_SUCCESS;
}

bool VulkanFroxelFogPass::initialize(VulkanDevice* device, VulkanGBuffer* g_buffer,
                                     VkImageView hdr_color_view, VkFormat hdr_format,
                                     VkImageView shadow_view, VkSampler shadow_sampler,
                                     VkBuffer light_buffer,
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
        if (vkCreateSampler(vk, &si, nullptr, &volume_sampler_) != VK_SUCCESS) return false;
    }

    // ---- UBO (persistently mapped) ----
    {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size  = sizeof(FroxelUBO);
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
        if (vkMapMemory(vk, ubo_memory_, 0, sizeof(FroxelUBO), 0, &ubo_mapped_) != VK_SUCCESS)
            return false;
    }

    if (!create_volume(scatter_image_, scatter_memory_, scatter_view_)) return false;
    if (!create_volume(integrated_image_, integrated_memory_, integrated_view_)) return false;

    // ---- shared descriptor pool ----
    {
        std::array<VkDescriptorPoolSize, 3> ps{};
        ps[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3};
        ps[1] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3};
        ps[2] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3};
        VkDescriptorPoolCreateInfo pi{};
        pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets       = 3;
        pi.poolSizeCount = static_cast<uint32_t>(ps.size());
        pi.pPoolSizes    = ps.data();
        // Storage-buffer slot for the light list.
        VkDescriptorPoolSize sb{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
        std::array<VkDescriptorPoolSize, 4> all = {ps[0], ps[1], ps[2], sb};
        pi.poolSizeCount = static_cast<uint32_t>(all.size());
        pi.pPoolSizes    = all.data();
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
                   binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, CS),
                   binding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, CS)},
                  scatter_dsl_, scatter_set_)) return false;
    if (!make_set({binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, CS),
                   binding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, CS),
                   binding(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, CS)},
                  integrate_dsl_, integrate_set_)) return false;
    if (!make_set({binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, FS),
                   binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FS),
                   binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FS)},
                  composite_dsl_, composite_set_)) return false;

    // ---- descriptor writes ----
    {
        VkDescriptorBufferInfo ubo_bi{ubo_, 0, sizeof(FroxelUBO)};
        VkDescriptorBufferInfo light_bi{light_buffer, 0, VK_WHOLE_SIZE};
        VkDescriptorImageInfo scatter_st{VK_NULL_HANDLE, scatter_view_, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo integr_st{VK_NULL_HANDLE, integrated_view_, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo shadow_ci{shadow_sampler, shadow_view,
                                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo pos_ci{gbuffer_sampler_, g_buffer_->get_position_view(),
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo integr_ci{volume_sampler_, integrated_view_,
                                        VK_IMAGE_LAYOUT_GENERAL};

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
        add(scatter_set_, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &ubo_bi);
        add(scatter_set_, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &scatter_st, nullptr);
        add(scatter_set_, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &shadow_ci, nullptr);
        add(scatter_set_, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &light_bi);
        add(integrate_set_, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &ubo_bi);
        add(integrate_set_, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &scatter_st, nullptr);
        add(integrate_set_, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &integr_st, nullptr);
        add(composite_set_, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &ubo_bi);
        add(composite_set_, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &pos_ci, nullptr);
        add(composite_set_, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &integr_ci, nullptr);
        vkUpdateDescriptorSets(vk, static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);
    }

    // ---- compute pipelines ----
    auto make_compute = [&](const uint32_t* spv, size_t bytes,
                            VkDescriptorSetLayout dsl,
                            VkPipelineLayout& layout, VkPipeline& pipe) -> bool {
        VkShaderModule sm = make_module(vk, spv, bytes);
        if (!sm) return false;
        VkPipelineLayoutCreateInfo pli{};
        pli.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1;
        pli.pSetLayouts    = &dsl;
        if (vkCreatePipelineLayout(vk, &pli, nullptr, &layout) != VK_SUCCESS) {
            vkDestroyShaderModule(vk, sm, nullptr);
            return false;
        }
        VkComputePipelineCreateInfo ci{};
        ci.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        ci.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ci.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        ci.stage.module = sm;
        ci.stage.pName  = "main";
        ci.layout       = layout;
        const VkResult r = vkCreateComputePipelines(vk, VK_NULL_HANDLE, 1, &ci,
                                                    nullptr, &pipe);
        vkDestroyShaderModule(vk, sm, nullptr);
        return r == VK_SUCCESS;
    };
    if (!make_compute(kFroxelScatterSpv, kFroxelScatterSpv_size,
                      scatter_dsl_, scatter_layout_, scatter_pipeline_)) return false;
    if (!make_compute(kFroxelIntegrateSpv, kFroxelIntegrateSpv_size,
                      integrate_dsl_, integrate_layout_, integrate_pipeline_)) return false;

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

    // ---- composite pipeline (fullscreen, out = src*1 + hdr*srcAlpha) ----
    {
        VkShaderModule vs = make_module(vk, kFroxelCompositeVertSpv, kFroxelCompositeVertSpv_size);
        VkShaderModule fs = make_module(vk, kFroxelCompositeFragSpv, kFroxelCompositeFragSpv_size);
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
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;            // + in-scatter
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;      // hdr * transmittance
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

void VulkanFroxelFogPass::execute(VkCommandBuffer cmd,
                                  const glm::mat4& view,
                                  const glm::mat4& proj,
                                  const glm::vec3& camera_position) {
    if (!enabled_ || scatter_pipeline_ == VK_NULL_HANDLE) return;

    FroxelUBO u{};
    u.inv_view_proj = glm::inverse(proj * view);
    u.shadow_matrix = shadow_matrix_;
    u.cam_pos       = glm::vec4(camera_position, static_cast<float>(light_count_));
    u.sun_dir       = glm::vec4(sun_direction_, config_.anisotropy);
    u.sun_color     = glm::vec4(sun_color_, config_.sun_intensity);
    u.fog           = glm::vec4(config_.density, config_.height_base,
                                config_.height_falloff, config_.max_distance);
    u.ambient       = glm::vec4(config_.ambient, config_.local_intensity);
    std::memcpy(ubo_mapped_, &u, sizeof(u));

    auto volume_barrier = [&](VkImage img, VkImageLayout old_layout,
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

    // First use: UNDEFINED -> GENERAL; afterwards make prior reads visible.
    const VkImageLayout old_l =
        volumes_initialized_ ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
    volume_barrier(scatter_image_, old_l,
                   VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    volume_barrier(integrated_image_, old_l,
                   VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    volumes_initialized_ = true;

    // Pass 1: per-froxel scattering.
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, scatter_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, scatter_layout_,
                            0, 1, &scatter_set_, 0, nullptr);
    vkCmdDispatch(cmd, (kFroxelX + 7) / 8, (kFroxelY + 7) / 8, kFroxelZ);

    // Scatter results -> integrate reads.
    volume_barrier(scatter_image_, VK_IMAGE_LAYOUT_GENERAL,
                   VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    // Pass 2: front-to-back integration.
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, integrate_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, integrate_layout_,
                            0, 1, &integrate_set_, 0, nullptr);
    vkCmdDispatch(cmd, (kFroxelX + 7) / 8, (kFroxelY + 7) / 8, 1);

    // Integration results -> composite fragment reads.
    volume_barrier(integrated_image_, VK_IMAGE_LAYOUT_GENERAL,
                   VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    // Pass 3: composite onto HDR.
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

void VulkanFroxelFogPass::destroy() {
    if (!device_) return;
    VkDevice vk = device_->get_device();
    auto destroy_pipe = [&](VkPipeline& p, VkPipelineLayout& l, VkDescriptorSetLayout& d) {
        if (p) vkDestroyPipeline(vk, p, nullptr);
        if (l) vkDestroyPipelineLayout(vk, l, nullptr);
        if (d) vkDestroyDescriptorSetLayout(vk, d, nullptr);
        p = VK_NULL_HANDLE; l = VK_NULL_HANDLE; d = VK_NULL_HANDLE;
    };
    destroy_pipe(scatter_pipeline_, scatter_layout_, scatter_dsl_);
    destroy_pipe(integrate_pipeline_, integrate_layout_, integrate_dsl_);
    destroy_pipe(composite_pipeline_, composite_layout_, composite_dsl_);
    if (composite_framebuffer_) vkDestroyFramebuffer(vk, composite_framebuffer_, nullptr);
    if (composite_render_pass_) vkDestroyRenderPass(vk, composite_render_pass_, nullptr);
    if (pool_) vkDestroyDescriptorPool(vk, pool_, nullptr);
    if (scatter_view_)      vkDestroyImageView(vk, scatter_view_, nullptr);
    if (scatter_image_)     vkDestroyImage(vk, scatter_image_, nullptr);
    if (scatter_memory_)    vkFreeMemory(vk, scatter_memory_, nullptr);
    if (integrated_view_)   vkDestroyImageView(vk, integrated_view_, nullptr);
    if (integrated_image_)  vkDestroyImage(vk, integrated_image_, nullptr);
    if (integrated_memory_) vkFreeMemory(vk, integrated_memory_, nullptr);
    if (ubo_mapped_)        vkUnmapMemory(vk, ubo_memory_);
    if (ubo_)               vkDestroyBuffer(vk, ubo_, nullptr);
    if (ubo_memory_)        vkFreeMemory(vk, ubo_memory_, nullptr);
    if (gbuffer_sampler_)   vkDestroySampler(vk, gbuffer_sampler_, nullptr);
    if (volume_sampler_)    vkDestroySampler(vk, volume_sampler_, nullptr);
    composite_framebuffer_ = VK_NULL_HANDLE;
    composite_render_pass_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    scatter_view_ = VK_NULL_HANDLE; scatter_image_ = VK_NULL_HANDLE; scatter_memory_ = VK_NULL_HANDLE;
    integrated_view_ = VK_NULL_HANDLE; integrated_image_ = VK_NULL_HANDLE; integrated_memory_ = VK_NULL_HANDLE;
    ubo_mapped_ = nullptr; ubo_ = VK_NULL_HANDLE; ubo_memory_ = VK_NULL_HANDLE;
    gbuffer_sampler_ = VK_NULL_HANDLE; volume_sampler_ = VK_NULL_HANDLE;
    device_ = nullptr;
}

} // namespace gws::renderer::gpu
