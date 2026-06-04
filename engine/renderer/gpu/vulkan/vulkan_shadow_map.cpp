/**
 * @file vulkan_shadow_map.cpp
 * @brief Vulkan shadow map implementation
 */

#include "vulkan_shadow_map.h"
#include "vulkan_device.h"
#include "vulkan_shader_registry.h"
#include "vulkan_scene_mesh.h"
#include "vulkan_scene_material.h" // Material::bind for alpha-tested caster
#include "vulkan_render_graph.h" // for DrawItem
#include "shadow_caster_spirv.h" // pre-compiled SPIR-V fallback for GCC builds
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <array>
#include <glm/gtc/matrix_transform.hpp>

namespace gws::renderer::gpu {

VulkanShadowMap::~VulkanShadowMap() {
    cleanup();
}

std::unique_ptr<VulkanShadowMap> VulkanShadowMap::create(VulkanDevice* device, 
                                                         const ShadowMapConfig& config) {
    auto shadow_map = std::make_unique<VulkanShadowMap>();
    shadow_map->device_ = device;
    shadow_map->config_ = config;
    
    try {
        shadow_map->create_shadow_image();
        shadow_map->create_shadow_sampler();
        shadow_map->create_render_pass();
        shadow_map->create_framebuffer();
        
        if (config.type == ShadowMapType::Cascaded2D) {
            shadow_map->create_cascade_matrices();
        }

        // Depth-only caster pipeline so the shadow stage can rasterise
        // the same draw list as the geometry stage.
        shadow_map->shader_registry_ = std::make_unique<VulkanShaderRegistry>();
        if (!shadow_map->shader_registry_->initialize(device)) {
            throw std::runtime_error("Failed to initialise shadow shader registry");
        }
        shadow_map->create_caster_pipeline();

        spdlog::info("VulkanShadowMap created: {}x{}, type={}",
                    config.width, config.height, static_cast<int>(config.type));
        return shadow_map;
    } catch (const std::exception& e) {
        spdlog::error("Failed to create VulkanShadowMap: {}", e.what());
        shadow_map->cleanup();
        return nullptr;
    }
}

void VulkanShadowMap::create_shadow_image() {
    VkDevice vk_device = device_->get_device();
    VkPhysicalDevice physical_device = device_->get_physical_device();
    
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_D32_SFLOAT;
    image_info.extent = {config_.width, config_.height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = config_.type == ShadowMapType::Cascaded2D ? config_.cascade_count : 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (config_.type == ShadowMapType::Cubemap) {
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.arrayLayers = 6;
        image_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }
    
    if (vkCreateImage(vk_device, &image_info, nullptr, &shadow_image_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow image");
    }
    
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(vk_device, shadow_image_, &mem_reqs);
    
    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = device_->find_memory_type(
        mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(vk_device, &alloc_info, nullptr, &shadow_memory_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate shadow image memory");
    }
    
    vkBindImageMemory(vk_device, shadow_image_, shadow_memory_, 0);
    
    // Create image view
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = shadow_image_;
    view_info.viewType = config_.type == ShadowMapType::Cascaded2D ? 
        VK_IMAGE_VIEW_TYPE_2D_ARRAY : 
        (config_.type == ShadowMapType::Cubemap ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D);
    view_info.format = VK_FORMAT_D32_SFLOAT;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = config_.type == ShadowMapType::Cascaded2D ? 
        config_.cascade_count : (config_.type == ShadowMapType::Cubemap ? 6 : 1);
    
    if (vkCreateImageView(vk_device, &view_info, nullptr, &shadow_view_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow image view");
    }
}

void VulkanShadowMap::create_shadow_sampler() {
    VkDevice vk_device = device_->get_device();
    
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.anisotropyEnable = VK_TRUE;
    sampler_info.maxAnisotropy = 1.0f;
    sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable = config_.use_pcf ? VK_TRUE : VK_FALSE;
    sampler_info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.mipLodBias = 0.0f;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = 1.0f;
    
    if (vkCreateSampler(vk_device, &sampler_info, nullptr, &shadow_sampler_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow sampler");
    }
}

void VulkanShadowMap::create_render_pass() {
    VkDevice vk_device = device_->get_device();
    
    VkAttachmentDescription attachment{};
    attachment.format = VK_FORMAT_D32_SFLOAT;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    
    VkAttachmentReference depth_ref{};
    depth_ref.attachment = 0;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depth_ref;
    
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dependencyFlags = 0;
    
    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;
    
    if (vkCreateRenderPass(vk_device, &render_pass_info, nullptr, &render_pass_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow render pass");
    }
}

void VulkanShadowMap::create_framebuffer() {
    VkDevice vk_device = device_->get_device();

    // Create depth view in `shadow_view_` so the framebuffer holds a live
    // reference for its lifetime. Destroying this view immediately after
    // vkCreateFramebuffer (as the original code did) leaves the framebuffer
    // with a dangling internal handle and segfaults when any later
    // vkCmdBeginRenderPass dereferences it.
    VkImageViewCreateInfo view_info{};
    view_info.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image    = shadow_image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format   = VK_FORMAT_D32_SFLOAT;
    view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(vk_device, &view_info, nullptr, &shadow_view_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth view for shadow framebuffer");
    }

    VkFramebufferCreateInfo framebuffer_info{};
    framebuffer_info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass      = render_pass_;
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.pAttachments    = &shadow_view_;
    framebuffer_info.width           = config_.width;
    framebuffer_info.height          = config_.height;
    framebuffer_info.layers          = 1;

    if (vkCreateFramebuffer(vk_device, &framebuffer_info, nullptr, &framebuffer_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow framebuffer");
    }
}

void VulkanShadowMap::create_cascade_matrices() {
    cascade_view_matrices_.resize(config_.cascade_count);
    cascade_proj_matrices_.resize(config_.cascade_count);
    cascade_splits_.resize(config_.cascade_count);
    
    // Initialize cascade splits (0.0 to 1.0)
    for (uint32_t i = 0; i < config_.cascade_count; ++i) {
        float split = (static_cast<float>(i) + 1.0f) / config_.cascade_count;
        cascade_splits_[i] = split;
    }
}

void VulkanShadowMap::begin_directional_pass(VkCommandBuffer cmd, uint32_t cascade_index) {
    if (cascade_index >= config_.cascade_count) {
        spdlog::warn("Cascade index {} out of range", cascade_index);
        return;
    }
    
    VkRenderPassBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin_info.renderPass = render_pass_;
    begin_info.framebuffer = framebuffer_;
    begin_info.renderArea.offset = {0, 0};
    begin_info.renderArea.extent = {config_.width, config_.height};
    
    VkClearValue clear_value;
    clear_value.depthStencil = {1.0f, 0};
    
    begin_info.clearValueCount = 1;
    begin_info.pClearValues = &clear_value;
    
    vkCmdBeginRenderPass(cmd, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanShadowMap::end_pass(VkCommandBuffer cmd) {
    vkCmdEndRenderPass(cmd);
}

void VulkanShadowMap::begin_cubemap_pass(VkCommandBuffer cmd, uint32_t face_index) {
    begin_directional_pass(cmd, face_index);
}

void VulkanShadowMap::begin_spot_pass(VkCommandBuffer cmd) {
    begin_directional_pass(cmd, 0);
}

glm::mat4 VulkanShadowMap::get_light_vp_matrix(uint32_t cascade_index) const {
    if (cascade_index >= config_.cascade_count) {
        return glm::mat4(1.0f);
    }
    return cascade_proj_matrices_[cascade_index] * cascade_view_matrices_[cascade_index];
}

void VulkanShadowMap::update_cascade_splits(const std::vector<float>& splits) {
    if (splits.size() != config_.cascade_count) {
        spdlog::warn("Cascade splits count mismatch");
        return;
    }
    cascade_splits_ = splits;
}

// ----------------------------------------------------------------------------
// Caster (depth-only) pipeline
// ----------------------------------------------------------------------------

namespace {

// Caster shader now does an alpha test (cutoff packed into material's
// emissive_factor.a, just like the G-Buffer fragment shader). Opaque
// materials use cutoff=0 → discard branch never triggers → identical perf
// to a pure depth-only pipeline modulo one extra texture sample. Cutout
// materials use the user's cutoff. Blend materials get cutoff=0.5 (set on
// the CPU side) so transparent objects cast binarised shadows instead of
// solid ones.
constexpr const char* kCasterVertSrc = R"GLSL(
#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;
layout(push_constant) uniform PC { mat4 mvp; } pc;
layout(location = 0) out vec2 outUV;
void main() {
    outUV = inUV;
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
}
)GLSL";

constexpr const char* kCasterFragSrc = R"GLSL(
#version 450
layout(location = 0) in vec2 inUV;
layout(set = 0, binding = 0) uniform MaterialUBO {
    vec4  base_color_factor;
    float metallic_factor;
    float roughness_factor;
    float occlusion_strength;
    float normal_scale;
    vec4  emissive_factor;
} mat;
layout(set = 0, binding = 1) uniform sampler2D albedoMap;
void main() {
    float alpha   = texture(albedoMap, inUV).a * mat.base_color_factor.a;
    float cutoff  = mat.emissive_factor.a;
    if (cutoff > 0.0 && alpha < cutoff) discard;
}
)GLSL";

} // namespace

void VulkanShadowMap::create_caster_pipeline() {
    VkDevice vk = device_->get_device();

    auto vert = shader_registry_->compile_glsl(kCasterVertSrc, ShaderStage::Vertex,   "shadow_caster.vert");
    if (!vert) {
        vert = shader_registry_->create_from_spirv(kShadowCasterVertSpv, kShadowCasterVertSpv_size,
                                                   ShaderStage::Vertex, "shadow_caster.vert");
    }
    auto frag = shader_registry_->compile_glsl(kCasterFragSrc, ShaderStage::Fragment, "shadow_caster.frag");
    if (!frag) {
        frag = shader_registry_->create_from_spirv(kShadowCasterFragSpv, kShadowCasterFragSpv_size,
                                                   ShaderStage::Fragment, "shadow_caster.frag");
    }
    if (!vert || !frag) throw std::runtime_error("Failed to compile shadow caster shaders");

    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pc.offset     = 0;
    pc.size       = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo plinfo{};
    plinfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plinfo.pushConstantRangeCount = 1;
    plinfo.pPushConstantRanges    = &pc;
    // If the caller supplied a material descriptor-set layout, bind it at
    // set=0 so the alpha-test fragment shader can sample the albedo + read
    // the cutoff. Otherwise build a layout with no sets (pure depth-only —
    // shadow shader's discard branch is never reached).
    if (config_.material_set_layout != VK_NULL_HANDLE) {
        plinfo.setLayoutCount = 1;
        plinfo.pSetLayouts    = &config_.material_set_layout;
    }
    if (vkCreatePipelineLayout(vk, &plinfo, nullptr, &caster_pipeline_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow caster pipeline layout");
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert->handle;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag->handle;
    stages[1].pName  = "main";

    auto binding = Mesh::vertex_binding();
    auto attrs   = Mesh::vertex_attributes();

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &binding;
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vi.pVertexAttributeDescriptions    = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    std::array<VkDynamicState, 2> dyn_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = static_cast<uint32_t>(dyn_states.size());
    dyn.pDynamicStates    = dyn_states.data();

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode             = VK_POLYGON_MODE_FILL;
    rs.cullMode                = VK_CULL_MODE_FRONT_BIT; // standard shadow trick to avoid acne
    rs.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth               = 1.0f;
    rs.depthBiasEnable         = VK_TRUE;
    rs.depthBiasConstantFactor = 1.25f;
    rs.depthBiasSlopeFactor    = 1.75f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS;

    // No color attachments — the shadow render pass is depth-only.
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 0;

    VkGraphicsPipelineCreateInfo info{};
    info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount          = static_cast<uint32_t>(stages.size());
    info.pStages             = stages.data();
    info.pVertexInputState   = &vi;
    info.pInputAssemblyState = &ia;
    info.pViewportState      = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState   = &ms;
    info.pDepthStencilState  = &ds;
    info.pColorBlendState    = &cb;
    info.pDynamicState       = &dyn;
    info.layout              = caster_pipeline_layout_;
    info.renderPass          = render_pass_;
    info.subpass             = 0;
    if (vkCreateGraphicsPipelines(vk, VK_NULL_HANDLE, 1, &info, nullptr,
                                  &caster_pipeline_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow caster pipeline");
    }
}

void VulkanShadowMap::draw_items(VkCommandBuffer cmd,
                                 const glm::mat4& view,
                                 const glm::mat4& proj,
                                 const glm::vec3& light_position,
                                 const DrawItem* draws,
                                 size_t draw_count,
                                 uint32_t* out_draw_calls,
                                 uint32_t* out_triangles) {
    if (out_draw_calls) *out_draw_calls = 0;
    if (out_triangles)  *out_triangles  = 0;
    if (caster_pipeline_ == VK_NULL_HANDLE || draws == nullptr || draw_count == 0) {
        return;
    }

    VkViewport vp{};
    vp.width    = static_cast<float>(config_.width);
    vp.height   = static_cast<float>(config_.height);
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor{};
    scissor.extent = {config_.width, config_.height};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, caster_pipeline_);

    const bool   has_material_layout = (config_.material_set_layout != VK_NULL_HANDLE);
    const glm::mat4 view_proj         = proj * view;
    const Mesh*     last_mesh         = nullptr;
    const Material* last_material     = nullptr;

    for (size_t i = 0; i < draw_count; ++i) {
        const DrawItem& d = draws[i];
        if (!d.mesh) continue;

        // Bind material descriptor set=0 so the alpha-test shader can read
        // base_color_factor + emissive_factor.a + sample albedoMap. Skip when
        // the pipeline layout doesn't include the material set (depth-only
        // fallback for callers that didn't pass a material_set_layout).
        if (has_material_layout && d.material != nullptr && d.material != last_material) {
            d.material->bind(cmd, caster_pipeline_layout_, /*set=*/0);
            last_material = d.material;
        }

        if (d.mesh != last_mesh) {
            d.mesh->bind(cmd);
            last_mesh = d.mesh;
        }
        glm::mat4 mvp = view_proj * d.model;
        vkCmdPushConstants(cmd, caster_pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(mvp), &mvp);

        // Shadow LOD = base LOD + 1, capped at the last available tier.
        // Shadow silhouettes hide LOD pops far better than the primary view.
        const glm::vec3 origin = glm::vec3(d.model[3]);
        const float distance = glm::length(origin - light_position);
        const size_t base_lod = d.mesh->select_lod(d.submesh_index, distance);
        const size_t shadow_lod = base_lod + 1; // draw_submesh clamps internally

        d.mesh->draw_submesh(cmd, d.submesh_index, shadow_lod);

        if (out_draw_calls) ++(*out_draw_calls);
        if (out_triangles && d.submesh_index < d.mesh->submeshes().size()) {
            const auto& sm = d.mesh->submeshes()[d.submesh_index];
            const size_t li = std::min(shadow_lod,
                                       sm.lods.empty() ? 0 : sm.lods.size() - 1);
            if (!sm.lods.empty()) {
                *out_triangles += sm.lods[li].index_count / 3;
            }
        }
    }
}

void VulkanShadowMap::cleanup() {
    if (!device_) return;

    VkDevice vk_device = device_->get_device();

    if (caster_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(vk_device, caster_pipeline_, nullptr);
        caster_pipeline_ = VK_NULL_HANDLE;
    }
    if (caster_pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vk_device, caster_pipeline_layout_, nullptr);
        caster_pipeline_layout_ = VK_NULL_HANDLE;
    }
    shader_registry_.reset();

    if (framebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(vk_device, framebuffer_, nullptr);
        framebuffer_ = VK_NULL_HANDLE;
    }

    if (render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vk_device, render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }

    if (shadow_sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(vk_device, shadow_sampler_, nullptr);
        shadow_sampler_ = VK_NULL_HANDLE;
    }

    if (shadow_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vk_device, shadow_view_, nullptr);
        shadow_view_ = VK_NULL_HANDLE;
    }

    if (shadow_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(vk_device, shadow_image_, nullptr);
        shadow_image_ = VK_NULL_HANDLE;
    }

    if (shadow_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, shadow_memory_, nullptr);
        shadow_memory_ = VK_NULL_HANDLE;
    }
}

} // namespace gws::renderer::gpu
