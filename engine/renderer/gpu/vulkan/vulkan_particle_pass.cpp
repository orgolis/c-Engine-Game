// ============================================================================
// VulkanParticlePass — see vulkan_particle_pass.h.
// ============================================================================
#include "vulkan_particle_pass.h"

#include "vulkan_device.h"
#include "vulkan_shader_registry.h"
#include "particle_spirv.h"   // precompiled fallback: runtime GLSL is unavailable in this build

#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstring>

namespace gws::renderer::gpu {

namespace {

// Billboards arrive already in world space with their colour baked in, so the
// vertex stage is a transform and nothing else. Deliberately: the CPU
// simulation owns particle behaviour, and duplicating any of it here would give
// two places to change when behaviour changes.
constexpr const char* kVertSrc = R"(#version 450
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;

layout(push_constant) uniform Push { mat4 view_proj; } pc;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

void main() {
    v_uv    = in_uv;
    v_color = in_color;
    gl_Position = pc.view_proj * vec4(in_pos, 1.0);
}
)";

// A soft radial falloff instead of a hard-edged quad. Without it every particle
// is a visible square, which is the single thing that makes a particle system
// look broken rather than stylised.
constexpr const char* kFragSrc = R"(#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 out_color;

void main() {
    vec2  d = v_uv * 2.0 - 1.0;
    float r = dot(d, d);
    if (r > 1.0) discard;                 // outside the disc: no square corners
    float falloff = 1.0 - r;
    falloff *= falloff;                   // squared: a softer core-to-edge ramp
    out_color = vec4(v_color.rgb, v_color.a * falloff);
}
)";

}  // namespace

std::unique_ptr<VulkanParticlePass> VulkanParticlePass::create(
    VulkanDevice* device, VkImageView hdr_color_view, VkFormat hdr_format,
    VkImageView depth_view, VkFormat depth_format, uint32_t width, uint32_t height) {
    auto p = std::unique_ptr<VulkanParticlePass>(new VulkanParticlePass());
    if (!p->init(device, hdr_color_view, hdr_format, depth_view, depth_format, width, height))
        return nullptr;
    return p;
}

VulkanParticlePass::~VulkanParticlePass() {
    if (vk_ == VK_NULL_HANDLE) return;
    if (vbo_mapped_) { vkUnmapMemory(vk_, vbo_memory_); vbo_mapped_ = nullptr; }
    if (vbo_)        vkDestroyBuffer(vk_, vbo_, nullptr);
    if (vbo_memory_) vkFreeMemory(vk_, vbo_memory_, nullptr);
    if (ibo_mapped_) { vkUnmapMemory(vk_, ibo_memory_); ibo_mapped_ = nullptr; }
    if (ibo_)        vkDestroyBuffer(vk_, ibo_, nullptr);
    if (ibo_memory_) vkFreeMemory(vk_, ibo_memory_, nullptr);
    if (pipeline_)        vkDestroyPipeline(vk_, pipeline_, nullptr);
    if (pipeline_layout_) vkDestroyPipelineLayout(vk_, pipeline_layout_, nullptr);
    if (framebuffer_)     vkDestroyFramebuffer(vk_, framebuffer_, nullptr);
    if (render_pass_)     vkDestroyRenderPass(vk_, render_pass_, nullptr);
    shader_registry_.reset();
}

bool VulkanParticlePass::init(VulkanDevice* device, VkImageView hdr_color_view,
                              VkFormat hdr_format, VkImageView depth_view,
                              VkFormat depth_format, uint32_t width, uint32_t height) {
    if (!device || hdr_color_view == VK_NULL_HANDLE || depth_view == VK_NULL_HANDLE) {
        spdlog::error("[Particles] init: missing device or attachment views");
        return false;
    }
    device_ = device;
    vk_     = device->get_device();
    width_  = width;
    height_ = height;

    shader_registry_ = std::make_unique<VulkanShaderRegistry>();
    if (!shader_registry_->initialize(device_)) {
        spdlog::error("[Particles] shader registry init failed");
        return false;
    }
    if (!create_render_pass(hdr_format, depth_format)) return false;
    if (!create_framebuffer(hdr_color_view, depth_view)) return false;
    if (!create_pipeline()) return false;

    spdlog::info("[Particles] GPU billboard pass ready ({}x{})", width_, height_);
    return true;
}

bool VulkanParticlePass::create_render_pass(VkFormat hdr_format, VkFormat depth_format) {
    // Colour LOADs what Transparent composited -- this pass adds to the image,
    // it does not own it. Clearing here would erase the entire scene, which is
    // the loudest possible version of getting this wrong.
    VkAttachmentDescription color{};
    color.format         = hdr_format;
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Depth is read-only: LOAD it, and STORE_OP_NONE would be ideal but STORE
    // is portable and harmless since nothing writes it.
    VkAttachmentDescription depth{};
    depth.format         = depth_format;
    depth.samples        = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_ref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription sub{};
    sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount    = 1;
    sub.pColorAttachments       = &color_ref;
    sub.pDepthStencilAttachment = &depth_ref;

    // Wait for the transparent composite's colour writes before blending on
    // top of them.
    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    const VkAttachmentDescription attachments[2] = {color, depth};
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = 2;
    rp.pAttachments    = attachments;
    rp.subpassCount    = 1;
    rp.pSubpasses      = &sub;
    rp.dependencyCount = 1;
    rp.pDependencies   = &dep;

    if (vkCreateRenderPass(vk_, &rp, nullptr, &render_pass_) != VK_SUCCESS) {
        spdlog::error("[Particles] vkCreateRenderPass failed");
        return false;
    }
    return true;
}

bool VulkanParticlePass::create_framebuffer(VkImageView hdr_color_view, VkImageView depth_view) {
    const VkImageView views[2] = {hdr_color_view, depth_view};
    VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb.renderPass      = render_pass_;
    fb.attachmentCount = 2;
    fb.pAttachments    = views;
    fb.width           = width_;
    fb.height          = height_;
    fb.layers          = 1;
    if (vkCreateFramebuffer(vk_, &fb, nullptr, &framebuffer_) != VK_SUCCESS) {
        spdlog::error("[Particles] vkCreateFramebuffer failed");
        return false;
    }
    return true;
}

bool VulkanParticlePass::create_pipeline() {
    // Runtime glslang is NOT available in this build -- the registry says so at
    // startup and every other pass takes the same path. The GLSL above is the
    // readable source of truth; the SPIR-V header beside it is what actually
    // loads. Regenerate with tools/spv_to_header.py after editing either.
    auto vs = shader_registry_->compile_glsl(kVertSrc, ShaderStage::Vertex,   "particle.vert");
    if (!vs) vs = shader_registry_->create_from_spirv(kParticleVertSpv, kParticleVertSpv_size,
                                                      ShaderStage::Vertex, "particle.vert");
    auto fs = shader_registry_->compile_glsl(kFragSrc, ShaderStage::Fragment, "particle.frag");
    if (!fs) fs = shader_registry_->create_from_spirv(kParticleFragSpv, kParticleFragSpv_size,
                                                      ShaderStage::Fragment, "particle.frag");
    if (!vs || !fs) {
        spdlog::error("[Particles] shader load failed (GLSL and SPIR-V fallback both)");
        return false;
    }

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcr.offset     = 0;
    pcr.size       = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges    = &pcr;
    if (vkCreatePipelineLayout(vk_, &pl, nullptr, &pipeline_layout_) != VK_SUCCESS) {
        spdlog::error("[Particles] vkCreatePipelineLayout failed");
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs->handle;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs->handle;
    stages[1].pName  = "main";

    VkVertexInputBindingDescription bind{};
    bind.binding   = 0;
    bind.stride    = sizeof(schizo::vfx::BillboardVertex);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(schizo::vfx::BillboardVertex, pos)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(schizo::vfx::BillboardVertex, uv)};
    attrs[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(schizo::vfx::BillboardVertex, color)};

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &bind;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    // No culling. Billboards are built to face the camera, but the winding
    // depends on the camera basis -- and this codebase already has mixed
    // CW/CCW winding, so culling here would drop half the particles depending
    // on which way you were looking.
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Test against the scene, never write. Writing depth would make whichever
    // quad drew first occlude the ones behind it, which reads as flickering
    // squares rather than as a depth bug.
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_FALSE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    // Premultiplied-ish additive: src alpha in, destination kept. Particles
    // brighten what is behind them and never darken it.
    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable         = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.colorBlendOp        = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.alphaBlendOp        = VK_BLEND_OP_ADD;
    cba.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments    = &cba;

    const VkDynamicState dyn_states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates    = dyn_states;

    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.stageCount          = 2;
    gp.pStages             = stages;
    gp.pVertexInputState   = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState      = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState   = &ms;
    gp.pDepthStencilState  = &ds;
    gp.pColorBlendState    = &cb;
    gp.pDynamicState       = &dyn;
    gp.layout              = pipeline_layout_;
    gp.renderPass          = render_pass_;
    gp.subpass             = 0;

    if (vkCreateGraphicsPipelines(vk_, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeline_) != VK_SUCCESS) {
        spdlog::error("[Particles] vkCreateGraphicsPipelines failed");
        return false;
    }
    return true;
}

// Allocate a host-visible, host-coherent buffer and leave it mapped.
// Shared by the vertex and index paths so the memory-type search exists once.
static bool make_mapped_buffer(VkDevice vk, VkPhysicalDevice pdev,
                               VkBufferUsageFlags usage, size_t bytes,
                               VkBuffer& out_buf, VkDeviceMemory& out_mem,
                               void*& out_mapped) {
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size        = bytes;
    bi.usage       = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vk, &bi, nullptr, &out_buf) != VK_SUCCESS) return false;

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(vk, out_buf, &req);

    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(pdev, &mp);

    const VkMemoryPropertyFlags want =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t type_index = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((req.memoryTypeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & want) == want) { type_index = i; break; }
    if (type_index == UINT32_MAX) return false;

    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = type_index;
    if (vkAllocateMemory(vk, &ai, nullptr, &out_mem) != VK_SUCCESS) return false;
    vkBindBufferMemory(vk, out_buf, out_mem, 0);
    return vkMapMemory(vk, out_mem, 0, bytes, 0, &out_mapped) == VK_SUCCESS;
}

// Two triangles per quad, from the four corners build_billboards emits.
//
// This is the bug that a "it drew something" check misses entirely. The
// simulation emits FOUR corner vertices per particle and no indices; drawing
// them as a triangle list stitches triangles across DIFFERENT particles, which
// passes validation, reports a healthy vertex count, and renders garbage. The
// log gave it away -- the counts were multiples of four, not six.
//
// Corner order from build_billboards is c0(0,0) c1(1,0) c2(1,1) c3(0,1), so the
// two triangles are 0-1-2 and 0-2-3.
bool VulkanParticlePass::ensure_index_capacity(uint32_t quads) {
    if (quads <= ibo_quads_ && ibo_ != VK_NULL_HANDLE) return true;

    const uint32_t want_quads = quads + quads / 2 + 256;
    const size_t   bytes      = size_t(want_quads) * 6 * sizeof(uint32_t);

    if (ibo_mapped_) { vkUnmapMemory(vk_, ibo_memory_); ibo_mapped_ = nullptr; }
    if (ibo_)        { vkDestroyBuffer(vk_, ibo_, nullptr); ibo_ = VK_NULL_HANDLE; }
    if (ibo_memory_) { vkFreeMemory(vk_, ibo_memory_, nullptr); ibo_memory_ = VK_NULL_HANDLE; }

    if (!make_mapped_buffer(vk_, device_->get_physical_device(),
                            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, bytes,
                            ibo_, ibo_memory_, ibo_mapped_)) {
        spdlog::error("[Particles] index buffer allocation failed ({} quads)", want_quads);
        return false;
    }

    // The pattern is fixed, so it is written once on growth rather than every
    // frame -- particle counts change constantly and this never does.
    uint32_t* idx = static_cast<uint32_t*>(ibo_mapped_);
    for (uint32_t q = 0; q < want_quads; ++q) {
        const uint32_t base = q * 4;
        idx[q * 6 + 0] = base + 0;
        idx[q * 6 + 1] = base + 1;
        idx[q * 6 + 2] = base + 2;
        idx[q * 6 + 3] = base + 0;
        idx[q * 6 + 4] = base + 2;
        idx[q * 6 + 5] = base + 3;
    }
    ibo_quads_ = want_quads;
    return true;
}

bool VulkanParticlePass::ensure_vertex_capacity(size_t bytes) {
    if (bytes <= vbo_bytes_) return true;

    // Grow generously and stay mapped. Particle counts fluctuate every frame;
    // reallocating on each small increase would churn device memory during
    // exactly the frames that are already the most expensive.
    const size_t want = bytes + bytes / 2 + 4096;

    if (vbo_mapped_) { vkUnmapMemory(vk_, vbo_memory_); vbo_mapped_ = nullptr; }
    if (vbo_)        { vkDestroyBuffer(vk_, vbo_, nullptr); vbo_ = VK_NULL_HANDLE; }
    if (vbo_memory_) { vkFreeMemory(vk_, vbo_memory_, nullptr); vbo_memory_ = VK_NULL_HANDLE; }

    if (!make_mapped_buffer(vk_, device_->get_physical_device(),
                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, want,
                            vbo_, vbo_memory_, vbo_mapped_)) {
        spdlog::error("[Particles] vertex buffer allocation failed ({} bytes)", want);
        return false;
    }

    vbo_bytes_ = want;
    return true;
}

void VulkanParticlePass::execute(VkCommandBuffer cmd,
                                 const std::vector<schizo::vfx::BillboardVertex>& verts,
                                 const glm::mat4& view_proj) {
    last_vertex_count_ = 0;
    last_index_count_  = 0;
    draw_calls_        = 0;

    if (cmd == VK_NULL_HANDLE || pipeline_ == VK_NULL_HANDLE) return;
    if (verts.empty()) return;                     // nothing to draw: no pass at all

    // Four corners per particle. A partial quad would index past the end of the
    // vertex buffer, so the remainder is dropped rather than trusted.
    const uint32_t quads = static_cast<uint32_t>(verts.size() / 4);
    if (quads == 0) return;

    const size_t bytes = verts.size() * sizeof(schizo::vfx::BillboardVertex);
    if (!ensure_vertex_capacity(bytes)) return;
    if (!ensure_index_capacity(quads)) return;
    std::memcpy(vbo_mapped_, verts.data(), bytes);

    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass        = render_pass_;
    rp.framebuffer       = framebuffer_;
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = {width_, height_};
    rp.clearValueCount   = 0;                      // both attachments LOAD
    rp.pClearValues      = nullptr;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.width    = static_cast<float>(width_);
    vp.height   = static_cast<float>(height_);
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D sc{};
    sc.extent = {width_, height_};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(glm::mat4), &view_proj);

    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vbo_, &offset);
    vkCmdBindIndexBuffer(cmd, ibo_, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, quads * 6, 1, 0, 0, 0);

    vkCmdEndRenderPass(cmd);

    last_vertex_count_ = static_cast<uint32_t>(verts.size());
    last_index_count_  = quads * 6;
    draw_calls_        = 1;

    static uint32_t last_logged = UINT32_MAX;
    if (last_vertex_count_ != last_logged) {
        spdlog::info("[Particles] drew {} quads ({} vertices, {} indices)",
                     quads, last_vertex_count_, last_index_count_);
        last_logged = last_vertex_count_;
    }
}

}  // namespace gws::renderer::gpu
