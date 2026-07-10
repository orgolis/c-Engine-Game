/**
 * @file vulkan_water_pass.cpp
 * @brief Animated water surfaces — see vulkan_water_pass.h.
 */

#include "vulkan_water_pass.h"
#include "vulkan_device.h"
#include "vulkan_g_buffer.h"
#include "water_spirv.h"

#include <spdlog/spdlog.h>
#include <array>
#include <cstring>

namespace gws::renderer::gpu {

namespace {
constexpr int      kGrid     = 128;                      // must match water.vert
constexpr uint32_t kVertices = kGrid * kGrid * 6;

VkShaderModule make_module(VkDevice dev, const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = bytes;
    ci.pCode    = code;
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(dev, &ci, nullptr, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    return m;
}
}  // namespace

VulkanWaterPass::~VulkanWaterPass() { destroy(); }

std::unique_ptr<VulkanWaterPass> VulkanWaterPass::create(
    VulkanDevice* device, VulkanGBuffer* g_buffer,
    VkImageView hdr_color_view, VkFormat hdr_format,
    VkImageView env_view, VkSampler env_sampler,
    uint32_t width, uint32_t height) {
    auto pass = std::make_unique<VulkanWaterPass>();
    if (!pass->initialize(device, g_buffer, hdr_color_view, hdr_format,
                          env_view, env_sampler, width, height)) {
        spdlog::error("VulkanWaterPass: initialization failed");
        return nullptr;
    }
    spdlog::info("VulkanWaterPass created ({}x{}, grid {}x{})", width, height, kGrid, kGrid);
    return pass;
}

bool VulkanWaterPass::initialize(VulkanDevice* device, VulkanGBuffer* g_buffer,
                                 VkImageView hdr_color_view, VkFormat hdr_format,
                                 VkImageView env_view, VkSampler env_sampler,
                                 uint32_t width, uint32_t height) {
    device_   = device;
    g_buffer_ = g_buffer;
    width_    = width;
    height_   = height;
    VkDevice vk = device->get_device();

    // ---- nearest sampler for the position G-buffer tap ----
    {
        VkSamplerCreateInfo si{};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = VK_FILTER_NEAREST;
        si.minFilter    = VK_FILTER_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(vk, &si, nullptr, &gbuffer_sampler_) != VK_SUCCESS) return false;
    }

    // ---- persistently mapped frame UBO ----
    {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size  = sizeof(FrameUBO);
        bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
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
        if (vkMapMemory(vk, ubo_memory_, 0, sizeof(FrameUBO), 0, &ubo_mapped_) != VK_SUCCESS)
            return false;
    }

    // ---- descriptor set: 0 = UBO, 1 = position tex, 2 = env cubemap ----
    {
        std::array<VkDescriptorSetLayoutBinding, 3> b{};
        b[0].binding = 0; b[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        b[0].descriptorCount = 1;
        b[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        b[1].binding = 1; b[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[1].descriptorCount = 1; b[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        b[2].binding = 2; b[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[2].descriptorCount = 1; b[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = static_cast<uint32_t>(b.size());
        li.pBindings    = b.data();
        if (vkCreateDescriptorSetLayout(vk, &li, nullptr, &dsl_) != VK_SUCCESS) return false;

        std::array<VkDescriptorPoolSize, 2> ps{};
        ps[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
        ps[1] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
        VkDescriptorPoolCreateInfo pi{};
        pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets       = 1;
        pi.poolSizeCount = static_cast<uint32_t>(ps.size());
        pi.pPoolSizes    = ps.data();
        if (vkCreateDescriptorPool(vk, &pi, nullptr, &pool_) != VK_SUCCESS) return false;

        VkDescriptorSetAllocateInfo si{};
        si.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        si.descriptorPool     = pool_;
        si.descriptorSetCount = 1;
        si.pSetLayouts        = &dsl_;
        if (vkAllocateDescriptorSets(vk, &si, &set_) != VK_SUCCESS) return false;

        VkDescriptorBufferInfo bi{ubo_, 0, sizeof(FrameUBO)};
        VkDescriptorImageInfo  pos{gbuffer_sampler_, g_buffer_->get_position_view(),
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo  env{env_sampler, env_view,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        std::array<VkWriteDescriptorSet, 3> w{};
        for (auto& x : w) x.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[0].dstSet = set_; w[0].dstBinding = 0; w[0].descriptorCount = 1;
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;  w[0].pBufferInfo = &bi;
        w[1].dstSet = set_; w[1].dstBinding = 1; w[1].descriptorCount = 1;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[1].pImageInfo = &pos;
        w[2].dstSet = set_; w[2].dstBinding = 2; w[2].descriptorCount = 1;
        w[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[2].pImageInfo = &env;
        vkUpdateDescriptorSets(vk, static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);
    }

    // ---- render pass over the borrowed HDR view (LOAD, keep lit content) ----
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

        VkAttachmentReference ref{};
        ref.attachment = 0;
        ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

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
        if (vkCreateRenderPass(vk, &rp, nullptr, &render_pass_) != VK_SUCCESS) return false;

        VkFramebufferCreateInfo fb{};
        fb.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass      = render_pass_;
        fb.attachmentCount = 1;
        fb.pAttachments    = &hdr_color_view;
        fb.width           = width;
        fb.height          = height;
        fb.layers          = 1;
        if (vkCreateFramebuffer(vk, &fb, nullptr, &framebuffer_) != VK_SUCCESS) return false;
    }

    // ---- pipeline: procedural grid, alpha blend, no depth attachment ----
    {
        VkShaderModule vs = make_module(vk, kWaterVertSpv, kWaterVertSpv_size);
        VkShaderModule fs = make_module(vk, kWaterFragSpv, kWaterFragSpv_size);
        if (!vs || !fs) {
            if (vs) vkDestroyShaderModule(vk, vs, nullptr);
            if (fs) vkDestroyShaderModule(vk, fs, nullptr);
            return false;
        }

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset     = 0;
        pcr.size       = sizeof(WaterDraw);

        VkPipelineLayoutCreateInfo pli{};
        pli.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount         = 1;
        pli.pSetLayouts            = &dsl_;
        pli.pushConstantRangeCount = 1;
        pli.pPushConstantRanges    = &pcr;
        if (vkCreatePipelineLayout(vk, &pli, nullptr, &layout_) != VK_SUCCESS) {
            vkDestroyShaderModule(vk, vs, nullptr);
            vkDestroyShaderModule(vk, fs, nullptr);
            return false;
        }

        std::array<VkPipelineShaderStageCreateInfo, 2> st{};
        st[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   st[0].module = vs; st[0].pName = "main";
        st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = fs; st[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vi{};   // no vertex buffers
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
        rs.cullMode    = VK_CULL_MODE_NONE;      // water visible from below too
        rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};   // manual depth test in FS
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        cba.blendEnable         = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp        = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
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
        gp.layout              = layout_;
        gp.renderPass          = render_pass_;
        gp.subpass             = 0;
        const VkResult r = vkCreateGraphicsPipelines(vk, VK_NULL_HANDLE, 1, &gp, nullptr,
                                                     &pipeline_);
        vkDestroyShaderModule(vk, vs, nullptr);
        vkDestroyShaderModule(vk, fs, nullptr);
        if (r != VK_SUCCESS) return false;
    }
    return true;
}

void VulkanWaterPass::execute(VkCommandBuffer cmd,
                              const glm::mat4& view,
                              const glm::mat4& proj,
                              const glm::vec3& camera_position,
                              float time_seconds) {
    if (!enabled_ || draws_.empty() || pipeline_ == VK_NULL_HANDLE) return;

    FrameUBO u{};
    u.view       = view;
    u.proj       = proj;
    u.cam_time   = glm::vec4(camera_position, time_seconds);
    u.sun_dir    = glm::vec4(sun_direction_, 0.0f);
    u.sun_color  = glm::vec4(sun_color_, 0.0f);
    u.resolution = glm::vec4(static_cast<float>(width_), static_cast<float>(height_), 0, 0);
    std::memcpy(ubo_mapped_, &u, sizeof(u));

    VkRenderPassBeginInfo rb{};
    rb.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rb.renderPass        = render_pass_;
    rb.framebuffer       = framebuffer_;
    rb.renderArea.extent = {width_, height_};
    vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.width    = static_cast<float>(width_);
    vp.height   = static_cast<float>(height_);
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, {width_, height_}};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_,
                            0, 1, &set_, 0, nullptr);
    for (const WaterDraw& d : draws_) {
        vkCmdPushConstants(cmd, layout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(WaterDraw), &d);
        vkCmdDraw(cmd, kVertices, 1, 0, 0);
    }
    vkCmdEndRenderPass(cmd);
}

void VulkanWaterPass::destroy() {
    if (!device_) return;
    VkDevice vk = device_->get_device();
    if (pipeline_)        vkDestroyPipeline(vk, pipeline_, nullptr);
    if (layout_)          vkDestroyPipelineLayout(vk, layout_, nullptr);
    if (pool_)            vkDestroyDescriptorPool(vk, pool_, nullptr);
    if (dsl_)             vkDestroyDescriptorSetLayout(vk, dsl_, nullptr);
    if (framebuffer_)     vkDestroyFramebuffer(vk, framebuffer_, nullptr);
    if (render_pass_)     vkDestroyRenderPass(vk, render_pass_, nullptr);
    if (ubo_mapped_)      vkUnmapMemory(vk, ubo_memory_);
    if (ubo_)             vkDestroyBuffer(vk, ubo_, nullptr);
    if (ubo_memory_)      vkFreeMemory(vk, ubo_memory_, nullptr);
    if (gbuffer_sampler_) vkDestroySampler(vk, gbuffer_sampler_, nullptr);
    pipeline_ = VK_NULL_HANDLE; layout_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE; dsl_ = VK_NULL_HANDLE;
    framebuffer_ = VK_NULL_HANDLE; render_pass_ = VK_NULL_HANDLE;
    ubo_mapped_ = nullptr; ubo_ = VK_NULL_HANDLE; ubo_memory_ = VK_NULL_HANDLE;
    gbuffer_sampler_ = VK_NULL_HANDLE;
    device_ = nullptr;
}

} // namespace gws::renderer::gpu
