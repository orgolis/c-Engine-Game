/**
 * @file vulkan_transparent_pass.cpp
 * @brief Weighted-Blended Order-Independent Transparency implementation.
 *
 * Two render passes per frame:
 *   1. Accumulation: rasterise transparent geometry into two extra render
 *      targets (accum + revealage) with carefully chosen blend states.
 *      Reads G-Buffer depth as read-only attachment so opaque geometry
 *      still occludes transparent fragments.
 *   2. Composite: full-screen pass that samples accum + revealage and
 *      blends the weighted-average colour over the lit HDR target.
 *
 * Reference: McGuire & Bavoil, "Weighted Blended Order-Independent
 * Transparency", JCGT 2013.
 */

#include "vulkan_transparent_pass.h"

#include "vulkan_device.h"
#include "vulkan_scene_mesh.h"
#include "vulkan_scene_material.h"
#include "vulkan_shader_registry.h"
#include "vulkan_render_graph.h"  // DrawItem, CameraData
#include "transparent_pass_spirv.h"
#include "transparent_composite_spirv.h"

#include <spdlog/spdlog.h>
#include <array>
#include <cstring>

namespace gws::renderer::gpu {

namespace {

struct TransparentPushConstants {
    glm::mat4 mvp;
    glm::mat4 model;
};

struct LightEnvUniform {
    glm::vec4 ambient_color;
    glm::vec4 camera_position;  // w = light_count as float
};
static_assert(sizeof(LightEnvUniform) == 32, "LightEnvUniform must be 32 bytes");

// Vertex / accumulation fragment shader sources — see transparent_pass.{vert,frag}.
constexpr const char* kTransparentVertSrc = R"GLSL(
#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;
layout(push_constant) uniform PC { mat4 mvp; mat4 model; } pc;
layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec3 outTangent;
layout(location = 4) out vec3 outBitangent;
void main() {
    vec4 wp = pc.model * vec4(inPosition, 1.0);
    outWorldPos = wp.xyz;
    mat3 nrm = mat3(pc.model);
    outNormal    = normalize(nrm * inNormal);
    outTangent   = normalize(nrm * inTangent.xyz);
    outBitangent = normalize(cross(outNormal, outTangent) * inTangent.w);
    outUV = inUV;
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
}
)GLSL";

// Note: kTransparentFragSrc is too long to embed inline — relied on entirely
// via the SPIR-V fallback. Runtime glslang compilation on MSVC will skip
// this shader and use the pre-baked SPIR-V (which is fine since the source
// is checked into tools/transparent_pass.frag).
constexpr const char* kTransparentFragSrc = nullptr;

constexpr const char* kCompositeVertSrc = R"GLSL(
#version 450
layout(location = 0) out vec2 outUV;
void main() {
    outUV = vec2((gl_VertexIndex == 1) ? 2.0 : 0.0,
                 (gl_VertexIndex == 2) ? 2.0 : 0.0);
    gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

constexpr const char* kCompositeFragSrc = R"GLSL(
#version 450
layout(location = 0) in vec2 inUV;
layout(set = 0, binding = 0) uniform sampler2D accumTex;
layout(set = 0, binding = 1) uniform sampler2D revealTex;
layout(location = 0) out vec4 outColor;
void main() {
    vec4  accum     = texture(accumTex,  inUV);
    float revealage = texture(revealTex, inUV).r;
    if (revealage >= 1.0 - 1e-5) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec3 average = accum.rgb / max(accum.a, 1e-4);
    outColor = vec4(average, revealage);
}
)GLSL";

constexpr VkFormat kAccumFormat  = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kRevealFormat = VK_FORMAT_R16_SFLOAT;

} // namespace

std::unique_ptr<VulkanTransparentPass> VulkanTransparentPass::create(
    VulkanDevice* device,
    VkImageView   hdr_color_view,
    VkFormat      hdr_format,
    VkImageView   depth_view,
    VkFormat      depth_format,
    uint32_t      width,
    uint32_t      height,
    VkDescriptorSetLayout material_layout)
{
    if (device == nullptr) { spdlog::error("VulkanTransparentPass::create: null device"); return nullptr; }
    if (hdr_color_view == VK_NULL_HANDLE || depth_view == VK_NULL_HANDLE) {
        spdlog::error("VulkanTransparentPass::create: null view(s)"); return nullptr;
    }
    if (width == 0 || height == 0 || material_layout == VK_NULL_HANDLE) {
        spdlog::error("VulkanTransparentPass::create: bad params"); return nullptr;
    }
    auto pass = std::unique_ptr<VulkanTransparentPass>(new VulkanTransparentPass());
    if (!pass->initialize(device, hdr_color_view, hdr_format,
                          depth_view, depth_format,
                          width, height, material_layout)) {
        return nullptr;
    }
    spdlog::info("VulkanTransparentPass[WBOIT] created ({}x{})", width, height);
    return pass;
}

VulkanTransparentPass::~VulkanTransparentPass() { destroy(); }

void VulkanTransparentPass::destroy_wboit_targets() {
    if (device_ == nullptr) return;
    VkDevice vk = device_->get_device();
    if (accum_view_    != VK_NULL_HANDLE) { vkDestroyImageView(vk, accum_view_,  nullptr); accum_view_  = VK_NULL_HANDLE; }
    if (accum_image_   != VK_NULL_HANDLE) { vkDestroyImage    (vk, accum_image_, nullptr); accum_image_ = VK_NULL_HANDLE; }
    if (accum_memory_  != VK_NULL_HANDLE) { vkFreeMemory      (vk, accum_memory_,nullptr); accum_memory_= VK_NULL_HANDLE; }
    if (reveal_view_   != VK_NULL_HANDLE) { vkDestroyImageView(vk, reveal_view_, nullptr); reveal_view_ = VK_NULL_HANDLE; }
    if (reveal_image_  != VK_NULL_HANDLE) { vkDestroyImage    (vk, reveal_image_,nullptr); reveal_image_= VK_NULL_HANDLE; }
    if (reveal_memory_ != VK_NULL_HANDLE) { vkFreeMemory      (vk, reveal_memory_,nullptr);reveal_memory_=VK_NULL_HANDLE; }
}

void VulkanTransparentPass::destroy() {
    if (device_ == nullptr) return;
    VkDevice vk = device_->get_device();

    if (accum_pipeline_         != VK_NULL_HANDLE) { vkDestroyPipeline      (vk, accum_pipeline_,         nullptr); accum_pipeline_         = VK_NULL_HANDLE; }
    if (accum_pipeline_layout_  != VK_NULL_HANDLE) { vkDestroyPipelineLayout(vk, accum_pipeline_layout_,  nullptr); accum_pipeline_layout_  = VK_NULL_HANDLE; }
    if (accum_framebuffer_      != VK_NULL_HANDLE) { vkDestroyFramebuffer   (vk, accum_framebuffer_,      nullptr); accum_framebuffer_      = VK_NULL_HANDLE; }
    if (accum_render_pass_      != VK_NULL_HANDLE) { vkDestroyRenderPass    (vk, accum_render_pass_,      nullptr); accum_render_pass_      = VK_NULL_HANDLE; }

    if (composite_pipeline_         != VK_NULL_HANDLE) { vkDestroyPipeline      (vk, composite_pipeline_,         nullptr); composite_pipeline_         = VK_NULL_HANDLE; }
    if (composite_pipeline_layout_  != VK_NULL_HANDLE) { vkDestroyPipelineLayout(vk, composite_pipeline_layout_,  nullptr); composite_pipeline_layout_  = VK_NULL_HANDLE; }
    if (composite_framebuffer_      != VK_NULL_HANDLE) { vkDestroyFramebuffer   (vk, composite_framebuffer_,      nullptr); composite_framebuffer_      = VK_NULL_HANDLE; }
    if (composite_render_pass_      != VK_NULL_HANDLE) { vkDestroyRenderPass    (vk, composite_render_pass_,      nullptr); composite_render_pass_      = VK_NULL_HANDLE; }
    if (composite_pool_             != VK_NULL_HANDLE) { vkDestroyDescriptorPool(vk, composite_pool_,             nullptr); composite_pool_             = VK_NULL_HANDLE; composite_set_ = VK_NULL_HANDLE; }
    if (composite_set_layout_       != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(vk, composite_set_layout_,   nullptr); composite_set_layout_       = VK_NULL_HANDLE; }
    if (composite_sampler_          != VK_NULL_HANDLE) { vkDestroySampler        (vk, composite_sampler_,          nullptr); composite_sampler_          = VK_NULL_HANDLE; }

    destroy_wboit_targets();

    if (light_env_pool_       != VK_NULL_HANDLE) { vkDestroyDescriptorPool(vk, light_env_pool_,       nullptr); light_env_pool_       = VK_NULL_HANDLE; light_env_set_ = VK_NULL_HANDLE; }
    if (light_env_layout_     != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(vk, light_env_layout_, nullptr); light_env_layout_     = VK_NULL_HANDLE; }
    if (light_env_buffer_     != VK_NULL_HANDLE) {
        if (light_env_mapped_ != nullptr) { vkUnmapMemory(vk, light_env_memory_); light_env_mapped_ = nullptr; }
        vkDestroyBuffer(vk, light_env_buffer_, nullptr); light_env_buffer_ = VK_NULL_HANDLE;
    }
    if (light_env_memory_     != VK_NULL_HANDLE) { vkFreeMemory(vk, light_env_memory_, nullptr); light_env_memory_ = VK_NULL_HANDLE; }

    shader_registry_.reset();
    device_ = nullptr;
}

bool VulkanTransparentPass::create_wboit_targets() {
    VkDevice vk = device_->get_device();

    auto make_image = [&](VkFormat fmt,
                          VkImage& img, VkImageView& view, VkDeviceMemory& mem) -> bool {
        VkImageCreateInfo ii{};
        ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType     = VK_IMAGE_TYPE_2D;
        ii.format        = fmt;
        ii.extent.width  = width_;
        ii.extent.height = height_;
        ii.extent.depth  = 1;
        ii.mipLevels     = 1;
        ii.arrayLayers   = 1;
        ii.samples       = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ii.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(vk, &ii, nullptr, &img) != VK_SUCCESS) return false;

        VkMemoryRequirements mr{};
        vkGetImageMemoryRequirements(vk, img, &mr);
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = mr.size;
        ai.memoryTypeIndex = device_->find_memory_type(
            mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(vk, &ai, nullptr, &mem) != VK_SUCCESS) return false;
        vkBindImageMemory(vk, img, mem, 0);

        VkImageViewCreateInfo vi{};
        vi.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image                       = img;
        vi.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
        vi.format                      = fmt;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        return vkCreateImageView(vk, &vi, nullptr, &view) == VK_SUCCESS;
    };

    if (!make_image(kAccumFormat,  accum_image_,  accum_view_,  accum_memory_))  return false;
    if (!make_image(kRevealFormat, reveal_image_, reveal_view_, reveal_memory_)) return false;
    return true;
}

bool VulkanTransparentPass::create_light_env_resources() {
    VkDevice vk = device_->get_device();

    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = sizeof(LightEnvUniform);
    bi.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vk, &bi, nullptr, &light_env_buffer_) != VK_SUCCESS) return false;

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(vk, light_env_buffer_, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = device_->find_memory_type(
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(vk, &ai, nullptr, &light_env_memory_) != VK_SUCCESS) return false;
    vkBindBufferMemory(vk, light_env_buffer_, light_env_memory_, 0);
    if (vkMapMemory(vk, light_env_memory_, 0, sizeof(LightEnvUniform), 0, &light_env_mapped_) != VK_SUCCESS) return false;

    std::array<VkDescriptorSetLayoutBinding, 4> bs{};
    bs[0] = { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    bs[1] = { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    bs[2] = { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    bs[3] = { 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = static_cast<uint32_t>(bs.size());
    li.pBindings    = bs.data();
    if (vkCreateDescriptorSetLayout(vk, &li, nullptr, &light_env_layout_) != VK_SUCCESS) return false;

    std::array<VkDescriptorPoolSize, 3> ps{};
    ps[0] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1 };
    ps[1] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 };
    ps[2] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1 };
    VkDescriptorPoolCreateInfo pi{};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets       = 1;
    pi.poolSizeCount = static_cast<uint32_t>(ps.size());
    pi.pPoolSizes    = ps.data();
    if (vkCreateDescriptorPool(vk, &pi, nullptr, &light_env_pool_) != VK_SUCCESS) return false;

    VkDescriptorSetAllocateInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    si.descriptorPool     = light_env_pool_;
    si.descriptorSetCount = 1;
    si.pSetLayouts        = &light_env_layout_;
    if (vkAllocateDescriptorSets(vk, &si, &light_env_set_) != VK_SUCCESS) return false;

    VkDescriptorBufferInfo binfo{};
    binfo.buffer = light_env_buffer_;
    binfo.range  = sizeof(LightEnvUniform);
    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = light_env_set_;
    w.dstBinding      = 0;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w.pBufferInfo     = &binfo;
    vkUpdateDescriptorSets(vk, 1, &w, 0, nullptr);
    return true;
}

bool VulkanTransparentPass::create_composite_resources(VkImageView hdr_color_view, VkFormat hdr_format) {
    VkDevice vk = device_->get_device();

    // Sampler for accum + reveal (linear filter, clamp-to-edge).
    VkSamplerCreateInfo sa{};
    sa.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sa.magFilter    = VK_FILTER_LINEAR;
    sa.minFilter    = VK_FILTER_LINEAR;
    sa.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sa.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sa.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sa.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(vk, &sa, nullptr, &composite_sampler_) != VK_SUCCESS) return false;

    // Composite descriptor set layout: 2 combined-image-sampler bindings.
    std::array<VkDescriptorSetLayoutBinding, 2> cb{};
    cb[0] = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    cb[1] = { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo cli{};
    cli.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    cli.bindingCount = static_cast<uint32_t>(cb.size());
    cli.pBindings    = cb.data();
    if (vkCreateDescriptorSetLayout(vk, &cli, nullptr, &composite_set_layout_) != VK_SUCCESS) return false;

    VkDescriptorPoolSize cps{};
    cps.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    cps.descriptorCount = 2;
    VkDescriptorPoolCreateInfo cpi{};
    cpi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    cpi.maxSets       = 1;
    cpi.poolSizeCount = 1;
    cpi.pPoolSizes    = &cps;
    if (vkCreateDescriptorPool(vk, &cpi, nullptr, &composite_pool_) != VK_SUCCESS) return false;

    VkDescriptorSetAllocateInfo csi{};
    csi.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    csi.descriptorPool     = composite_pool_;
    csi.descriptorSetCount = 1;
    csi.pSetLayouts        = &composite_set_layout_;
    if (vkAllocateDescriptorSets(vk, &csi, &composite_set_) != VK_SUCCESS) return false;

    // Write accum + reveal views into the composite set. Layout will be
    // SHADER_READ_ONLY by the time we sample (accum render pass leaves them
    // in that layout via finalLayout).
    VkDescriptorImageInfo ai_info{};
    ai_info.sampler     = composite_sampler_;
    ai_info.imageView   = accum_view_;
    ai_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorImageInfo ri_info{};
    ri_info.sampler     = composite_sampler_;
    ri_info.imageView   = reveal_view_;
    ri_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 2> ws{};
    ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ws[0].dstSet = composite_set_; ws[0].dstBinding = 0;
    ws[0].descriptorCount = 1; ws[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ws[0].pImageInfo = &ai_info;
    ws[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ws[1].dstSet = composite_set_; ws[1].dstBinding = 1;
    ws[1].descriptorCount = 1; ws[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ws[1].pImageInfo = &ri_info;
    vkUpdateDescriptorSets(vk, static_cast<uint32_t>(ws.size()), ws.data(), 0, nullptr);

    // Composite render pass: one HDR color attachment, LOAD_OP_LOAD,
    // SHADER_READ_ONLY both in and out (lighting left it in that layout,
    // post-process will read it back).
    VkAttachmentDescription hdr_att{};
    hdr_att.format         = hdr_format;
    hdr_att.samples        = VK_SAMPLE_COUNT_1_BIT;
    hdr_att.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
    hdr_att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    hdr_att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    hdr_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    hdr_att.initialLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    hdr_att.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference hdr_ref{};
    hdr_ref.attachment = 0;
    hdr_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription sub{};
    sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments    = &hdr_ref;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
                        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpi{};
    rpi.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 1;
    rpi.pAttachments    = &hdr_att;
    rpi.subpassCount    = 1;
    rpi.pSubpasses      = &sub;
    rpi.dependencyCount = 1;
    rpi.pDependencies   = &dep;
    if (vkCreateRenderPass(vk, &rpi, nullptr, &composite_render_pass_) != VK_SUCCESS) return false;

    VkFramebufferCreateInfo fbi{};
    fbi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbi.renderPass      = composite_render_pass_;
    fbi.attachmentCount = 1;
    fbi.pAttachments    = &hdr_color_view;
    fbi.width           = width_;
    fbi.height          = height_;
    fbi.layers          = 1;
    if (vkCreateFramebuffer(vk, &fbi, nullptr, &composite_framebuffer_) != VK_SUCCESS) return false;

    // Composite pipeline.
    auto cvert = shader_registry_->compile_glsl(kCompositeVertSrc, ShaderStage::Vertex,   "transparent_composite.vert");
    if (!cvert) cvert = shader_registry_->create_from_spirv(kTransparentCompositeVertSpv, kTransparentCompositeVertSpv_size,
                                                            ShaderStage::Vertex, "transparent_composite.vert");
    auto cfrag = shader_registry_->compile_glsl(kCompositeFragSrc, ShaderStage::Fragment, "transparent_composite.frag");
    if (!cfrag) cfrag = shader_registry_->create_from_spirv(kTransparentCompositeFragSpv, kTransparentCompositeFragSpv_size,
                                                            ShaderStage::Fragment, "transparent_composite.frag");
    if (!cvert || !cfrag) return false;

    VkPipelineLayoutCreateInfo cpl{};
    cpl.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    cpl.setLayoutCount = 1;
    cpl.pSetLayouts    = &composite_set_layout_;
    if (vkCreatePipelineLayout(vk, &cpl, nullptr, &composite_pipeline_layout_) != VK_SUCCESS) return false;

    std::array<VkPipelineShaderStageCreateInfo, 2> ss{};
    ss[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ss[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;   ss[0].module = cvert->handle; ss[0].pName = "main";
    ss[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ss[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT; ss[1].module = cfrag->handle; ss[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    std::array<VkDynamicState, 2> dyn_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = static_cast<uint32_t>(dyn_states.size());
    dyn.pDynamicStates    = dyn_states.data();

    VkPipelineViewportStateCreateInfo vp_state{};
    vp_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp_state.viewportCount = 1; vp_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Composite: blend over HDR.
    //   HDR_new.rgb = src.rgb * 1 + HDR_old.rgb * src.a
    //   HDR_new.a   = HDR_old.a (alpha unchanged)
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cba.blendEnable         = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.colorBlendOp        = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.alphaBlendOp        = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo cb_state{};
    cb_state.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb_state.attachmentCount = 1;
    cb_state.pAttachments    = &cba;

    VkGraphicsPipelineCreateInfo gpi{};
    gpi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpi.stageCount          = static_cast<uint32_t>(ss.size());
    gpi.pStages             = ss.data();
    gpi.pVertexInputState   = &vi;
    gpi.pInputAssemblyState = &ia;
    gpi.pViewportState      = &vp_state;
    gpi.pRasterizationState = &rs;
    gpi.pMultisampleState   = &ms;
    gpi.pColorBlendState    = &cb_state;
    gpi.pDynamicState       = &dyn;
    gpi.layout              = composite_pipeline_layout_;
    gpi.renderPass          = composite_render_pass_;
    gpi.subpass             = 0;
    if (vkCreateGraphicsPipelines(vk, VK_NULL_HANDLE, 1, &gpi, nullptr, &composite_pipeline_) != VK_SUCCESS) {
        spdlog::error("VulkanTransparentPass: failed to create composite pipeline"); return false;
    }
    return true;
}

void VulkanTransparentPass::set_lighting_resources(
    VkBuffer    light_buffer,
    uint32_t    max_lights,
    VkImageView dir_view,
    VkSampler   dir_sampler,
    VkImageView pt_view,
    VkSampler   pt_sampler)
{
    if (device_ == nullptr || light_env_set_ == VK_NULL_HANDLE) return;
    if (light_buffer == VK_NULL_HANDLE || max_lights == 0) return;
    if (dir_view == VK_NULL_HANDLE || dir_sampler == VK_NULL_HANDLE ||
        pt_view  == VK_NULL_HANDLE || pt_sampler  == VK_NULL_HANDLE) return;

    VkDescriptorImageInfo di{};
    di.sampler = dir_sampler; di.imageView = dir_view;
    di.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorImageInfo pi{};
    pi.sampler = pt_sampler;  pi.imageView = pt_view;
    pi.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorBufferInfo lb{};
    lb.buffer = light_buffer; lb.offset = 0; lb.range = VK_WHOLE_SIZE;

    std::array<VkWriteDescriptorSet, 3> ws{};
    ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[0].dstSet = light_env_set_; ws[0].dstBinding = 1; ws[0].descriptorCount = 1; ws[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ws[0].pImageInfo = &di;
    ws[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[1].dstSet = light_env_set_; ws[1].dstBinding = 2; ws[1].descriptorCount = 1; ws[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ws[1].pImageInfo = &pi;
    ws[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[2].dstSet = light_env_set_; ws[2].dstBinding = 3; ws[2].descriptorCount = 1; ws[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;         ws[2].pBufferInfo = &lb;
    vkUpdateDescriptorSets(device_->get_device(), static_cast<uint32_t>(ws.size()), ws.data(), 0, nullptr);
    spdlog::info("VulkanTransparentPass[WBOIT]: lighting resources wired (max_lights={})", max_lights);
}

void VulkanTransparentPass::upload_light_env(const glm::vec3& camera_position) {
    if (light_env_mapped_ == nullptr) return;
    LightEnvUniform u{};
    u.ambient_color   = glm::vec4(ambient_color_, ambient_intensity_);
    u.camera_position = glm::vec4(camera_position, static_cast<float>(light_count_));
    std::memcpy(light_env_mapped_, &u, sizeof(u));
}

void VulkanTransparentPass::set_ambient(const glm::vec3& color, float intensity) {
    ambient_color_ = color; ambient_intensity_ = intensity;
}

void VulkanTransparentPass::resize(VkImageView hdr_color_view,
                                   VkImageView depth_view,
                                   uint32_t width, uint32_t height) {
    if (device_ == nullptr) return;
    VkDevice vk = device_->get_device();

    if (composite_framebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(vk, composite_framebuffer_, nullptr);
        composite_framebuffer_ = VK_NULL_HANDLE;
    }
    if (accum_framebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(vk, accum_framebuffer_, nullptr);
        accum_framebuffer_ = VK_NULL_HANDLE;
    }
    destroy_wboit_targets();

    width_ = width; height_ = height;
    if (!create_wboit_targets()) {
        spdlog::error("VulkanTransparentPass::resize: failed to create WBOIT targets");
        return;
    }

    // Recreate the accum framebuffer.
    std::array<VkImageView, 3> aatt = { accum_view_, reveal_view_, depth_view };
    VkFramebufferCreateInfo fbi{};
    fbi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbi.renderPass      = accum_render_pass_;
    fbi.attachmentCount = static_cast<uint32_t>(aatt.size());
    fbi.pAttachments    = aatt.data();
    fbi.width           = width_;
    fbi.height          = height_;
    fbi.layers          = 1;
    vkCreateFramebuffer(vk, &fbi, nullptr, &accum_framebuffer_);

    // Recreate the composite framebuffer onto the new HDR view.
    fbi.renderPass      = composite_render_pass_;
    fbi.attachmentCount = 1;
    fbi.pAttachments    = &hdr_color_view;
    vkCreateFramebuffer(vk, &fbi, nullptr, &composite_framebuffer_);

    // Rewrite the composite descriptor set to point at the new accum/reveal views.
    VkDescriptorImageInfo ai{};
    ai.sampler = composite_sampler_; ai.imageView = accum_view_;
    ai.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorImageInfo ri{};
    ri.sampler = composite_sampler_; ri.imageView = reveal_view_;
    ri.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    std::array<VkWriteDescriptorSet, 2> ws{};
    ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[0].dstSet = composite_set_; ws[0].dstBinding = 0; ws[0].descriptorCount = 1; ws[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ws[0].pImageInfo = &ai;
    ws[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[1].dstSet = composite_set_; ws[1].dstBinding = 1; ws[1].descriptorCount = 1; ws[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ws[1].pImageInfo = &ri;
    vkUpdateDescriptorSets(vk, static_cast<uint32_t>(ws.size()), ws.data(), 0, nullptr);
}

bool VulkanTransparentPass::initialize(VulkanDevice* device,
                                       VkImageView hdr_color_view, VkFormat hdr_format,
                                       VkImageView depth_view,     VkFormat depth_format,
                                       uint32_t width, uint32_t height,
                                       VkDescriptorSetLayout material_layout) {
    device_       = device;
    width_        = width;
    height_       = height;
    hdr_format_   = hdr_format;
    depth_format_ = depth_format;
    VkDevice vk   = device_->get_device();

    if (!create_wboit_targets()) {
        spdlog::error("VulkanTransparentPass: failed to create WBOIT targets"); return false;
    }

    // --- Accumulation render pass ---
    // attachment 0: accum (CLEAR -> SHADER_READ_ONLY)
    // attachment 1: reveal (CLEAR -> SHADER_READ_ONLY)
    // attachment 2: depth (LOAD readonly -> readonly)
    std::array<VkAttachmentDescription, 3> atts{};
    atts[0].format         = kAccumFormat;
    atts[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    atts[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    atts[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[0].finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    atts[1].format         = kRevealFormat;
    atts[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    atts[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[1].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    atts[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[1].finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    atts[2].format         = depth_format;
    atts[2].samples        = VK_SAMPLE_COUNT_1_BIT;
    atts[2].loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
    atts[2].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[2].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[2].initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    atts[2].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    std::array<VkAttachmentReference, 2> color_refs{};
    color_refs[0].attachment = 0; color_refs[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_refs[1].attachment = 1; color_refs[1].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depth_ref{};
    depth_ref.attachment = 2; depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkSubpassDescription sub{};
    sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount    = static_cast<uint32_t>(color_refs.size());
    sub.pColorAttachments       = color_refs.data();
    sub.pDepthStencilAttachment = &depth_ref;

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

    VkRenderPassCreateInfo rpi{};
    rpi.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = static_cast<uint32_t>(atts.size());
    rpi.pAttachments    = atts.data();
    rpi.subpassCount    = 1;
    rpi.pSubpasses      = &sub;
    rpi.dependencyCount = 1;
    rpi.pDependencies   = &dep;
    if (vkCreateRenderPass(vk, &rpi, nullptr, &accum_render_pass_) != VK_SUCCESS) {
        spdlog::error("VulkanTransparentPass: failed to create accum render pass"); return false;
    }

    std::array<VkImageView, 3> fb_atts = { accum_view_, reveal_view_, depth_view };
    VkFramebufferCreateInfo fbi{};
    fbi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbi.renderPass      = accum_render_pass_;
    fbi.attachmentCount = static_cast<uint32_t>(fb_atts.size());
    fbi.pAttachments    = fb_atts.data();
    fbi.width           = width_;
    fbi.height          = height_;
    fbi.layers          = 1;
    if (vkCreateFramebuffer(vk, &fbi, nullptr, &accum_framebuffer_) != VK_SUCCESS) {
        spdlog::error("VulkanTransparentPass: failed to create accum framebuffer"); return false;
    }

    if (!create_light_env_resources()) return false;

    shader_registry_ = std::make_unique<VulkanShaderRegistry>();
    if (!shader_registry_->initialize(device_)) return false;

    auto vert = shader_registry_->compile_glsl(kTransparentVertSrc, ShaderStage::Vertex, "transparent.vert");
    if (!vert) vert = shader_registry_->create_from_spirv(kTransparentVertSpv, kTransparentVertSpv_size,
                                                          ShaderStage::Vertex, "transparent.vert");
    std::shared_ptr<ShaderModule> frag;
    if (kTransparentFragSrc != nullptr) {
        frag = shader_registry_->compile_glsl(kTransparentFragSrc, ShaderStage::Fragment, "transparent.frag");
    }
    if (!frag) frag = shader_registry_->create_from_spirv(kTransparentFragSpv, kTransparentFragSpv_size,
                                                          ShaderStage::Fragment, "transparent.frag");
    if (!vert || !frag) {
        spdlog::error("VulkanTransparentPass: failed to load accum shaders"); return false;
    }

    std::array<VkDescriptorSetLayout, 2> set_layouts = { light_env_layout_, material_layout };
    VkPushConstantRange pr{};
    pr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pr.size       = sizeof(TransparentPushConstants);
    VkPipelineLayoutCreateInfo pli{};
    pli.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount         = static_cast<uint32_t>(set_layouts.size());
    pli.pSetLayouts            = set_layouts.data();
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges    = &pr;
    if (vkCreatePipelineLayout(vk, &pli, nullptr, &accum_pipeline_layout_) != VK_SUCCESS) return false;

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = vert->handle; stages[0].pName = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = frag->handle; stages[1].pName = "main";

    auto binding = Mesh::vertex_binding();
    auto attrs   = Mesh::vertex_attributes();
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 1; vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vi.pVertexAttributeDescriptions    = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    std::array<VkDynamicState, 2> dyn_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2; dyn.pDynamicStates = dyn_states.data();

    VkPipelineViewportStateCreateInfo vp_state{};
    vp_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp_state.viewportCount = 1; vp_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_FALSE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS;

    // Accum target: additive blend.
    VkPipelineColorBlendAttachmentState accum_blend{};
    accum_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    accum_blend.blendEnable         = VK_TRUE;
    accum_blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; accum_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE; accum_blend.colorBlendOp = VK_BLEND_OP_ADD;
    accum_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; accum_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE; accum_blend.alphaBlendOp = VK_BLEND_OP_ADD;

    // Revealage target: multiplicative — out_new = 0 + out_old * (1 - srcColor).
    VkPipelineColorBlendAttachmentState reveal_blend{};
    reveal_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    reveal_blend.blendEnable         = VK_TRUE;
    reveal_blend.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO; reveal_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR; reveal_blend.colorBlendOp = VK_BLEND_OP_ADD;
    reveal_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; reveal_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE; reveal_blend.alphaBlendOp = VK_BLEND_OP_ADD;

    std::array<VkPipelineColorBlendAttachmentState, 2> blends = { accum_blend, reveal_blend };
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = static_cast<uint32_t>(blends.size());
    cb.pAttachments    = blends.data();

    VkGraphicsPipelineCreateInfo gpi{};
    gpi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpi.stageCount          = static_cast<uint32_t>(stages.size());
    gpi.pStages             = stages.data();
    gpi.pVertexInputState   = &vi;
    gpi.pInputAssemblyState = &ia;
    gpi.pViewportState      = &vp_state;
    gpi.pRasterizationState = &rs;
    gpi.pMultisampleState   = &ms;
    gpi.pDepthStencilState  = &ds;
    gpi.pColorBlendState    = &cb;
    gpi.pDynamicState       = &dyn;
    gpi.layout              = accum_pipeline_layout_;
    gpi.renderPass          = accum_render_pass_;
    gpi.subpass             = 0;
    if (vkCreateGraphicsPipelines(vk, VK_NULL_HANDLE, 1, &gpi, nullptr, &accum_pipeline_) != VK_SUCCESS) {
        spdlog::error("VulkanTransparentPass: failed to create accum pipeline"); return false;
    }

    if (!create_composite_resources(hdr_color_view, hdr_format)) {
        spdlog::error("VulkanTransparentPass: failed to create composite resources"); return false;
    }
    return true;
}

void VulkanTransparentPass::execute(VkCommandBuffer cmd,
                                    const std::vector<DrawItem>& items,
                                    const CameraData& camera) {
    if (cmd == VK_NULL_HANDLE || accum_render_pass_ == VK_NULL_HANDLE) return;
    upload_light_env(camera.position);

    static size_t last_logged_count = SIZE_MAX;
    if (items.size() != last_logged_count) {
        spdlog::info("[Transparent/WBOIT] {} draw item(s) this frame", items.size());
        last_logged_count = items.size();
    }

    // ---- Accumulation pass ----
    std::array<VkClearValue, 3> clears{};
    clears[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // accum: zero
    clears[1].color = {{1.0f, 0.0f, 0.0f, 0.0f}};  // revealage: start at 1 (fully revealed)
    // clears[2] is depth — LOAD_OP_LOAD so cleared value is ignored, but we
    // still pass an entry to keep array index alignment with attachments.

    VkRenderPassBeginInfo arp{};
    arp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    arp.renderPass        = accum_render_pass_;
    arp.framebuffer       = accum_framebuffer_;
    arp.renderArea.offset = {0, 0};
    arp.renderArea.extent = {width_, height_};
    arp.clearValueCount   = static_cast<uint32_t>(clears.size());
    arp.pClearValues      = clears.data();
    vkCmdBeginRenderPass(cmd, &arp, VK_SUBPASS_CONTENTS_INLINE);

    if (!items.empty() && accum_pipeline_ != VK_NULL_HANDLE) {
        VkViewport vp{};
        vp.width = static_cast<float>(width_); vp.height = static_cast<float>(height_); vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{}; sc.extent = {width_, height_};
        vkCmdSetScissor(cmd, 0, 1, &sc);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, accum_pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                accum_pipeline_layout_, /*set=*/0, 1, &light_env_set_, 0, nullptr);

        const glm::mat4 view_proj = camera.proj * camera.view;
        const Mesh*     last_mesh = nullptr;
        const Material* last_mat  = nullptr;
        for (const auto& d : items) {
            if (!d.mesh || !d.material) continue;
            if (d.material != last_mat) {
                d.material->bind(cmd, accum_pipeline_layout_, /*set=*/1);
                last_mat = d.material;
            }
            if (d.mesh != last_mesh) {
                d.mesh->bind(cmd);
                last_mesh = d.mesh;
            }
            TransparentPushConstants pc{};
            pc.mvp   = view_proj * d.model;
            pc.model = d.model;
            vkCmdPushConstants(cmd, accum_pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(pc), &pc);
            d.mesh->draw_submesh(cmd, d.submesh_index, /*lod=*/0);
        }
    }
    vkCmdEndRenderPass(cmd);

    // ---- Composite pass ----
    VkRenderPassBeginInfo crp{};
    crp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    crp.renderPass        = composite_render_pass_;
    crp.framebuffer       = composite_framebuffer_;
    crp.renderArea.offset = {0, 0};
    crp.renderArea.extent = {width_, height_};
    crp.clearValueCount   = 0;
    crp.pClearValues      = nullptr;
    vkCmdBeginRenderPass(cmd, &crp, VK_SUBPASS_CONTENTS_INLINE);

    if (composite_pipeline_ != VK_NULL_HANDLE) {
        VkViewport vp{};
        vp.width = static_cast<float>(width_); vp.height = static_cast<float>(height_); vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{}; sc.extent = {width_, height_};
        vkCmdSetScissor(cmd, 0, 1, &sc);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, composite_pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                composite_pipeline_layout_, /*set=*/0, 1, &composite_set_, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    vkCmdEndRenderPass(cmd);
}

} // namespace gws::renderer::gpu
