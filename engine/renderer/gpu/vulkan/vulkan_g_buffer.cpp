/**
 * @file vulkan_g_buffer.cpp
 * @brief Vulkan G-Buffer implementation
 */

#include "vulkan_g_buffer.h"
#include "vulkan_device.h"
#include "vulkan_shader_registry.h"
#include "vulkan_scene_mesh.h"
#include "vulkan_scene_material.h"
#include "vulkan_render_graph.h" // for DrawItem
#include "gbuffer_demo_spirv.h"   // pre-compiled SPIR-V fallback for GCC builds
#include "gbuffer_scene_spirv.h"  // pre-compiled SPIR-V fallback for GCC builds
#include "vulkan_occlusion_culler.h"
#include "vulkan_hzb_culler.h"
#include "culling.h" // Frustum for per-meshlet culling
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <array>
#include <cstring>

namespace gws::renderer::gpu {

VulkanGBuffer::~VulkanGBuffer() {
    cleanup();
}

std::unique_ptr<VulkanGBuffer> VulkanGBuffer::create(VulkanDevice* device, 
                                                     const GBufferConfig& config) {
    auto gbuffer = std::make_unique<VulkanGBuffer>();
    gbuffer->device_ = device;
    gbuffer->config_ = config;
    
    VkDevice vk_device = device->get_device();
    VkPhysicalDevice physical_device = device->get_physical_device();
    
    try {
        // Create position image (RGBA16F)
        VkImageCreateInfo pos_info{};
        pos_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        pos_info.imageType = VK_IMAGE_TYPE_2D;
        pos_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        pos_info.extent = {config.width, config.height, 1};
        pos_info.mipLevels = 1;
        pos_info.arrayLayers = 1;
        pos_info.samples = VK_SAMPLE_COUNT_1_BIT;
        pos_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        pos_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        pos_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateImage(vk_device, &pos_info, nullptr, &gbuffer->position_image_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create position image");
        }
        
        // Allocate memory for position image
        VkMemoryRequirements pos_mem_reqs;
        vkGetImageMemoryRequirements(vk_device, gbuffer->position_image_, &pos_mem_reqs);
        
        VkMemoryAllocateInfo pos_alloc{};
        pos_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        pos_alloc.allocationSize = pos_mem_reqs.size;
        pos_alloc.memoryTypeIndex = device->find_memory_type(
            pos_mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        if (vkAllocateMemory(vk_device, &pos_alloc, nullptr, &gbuffer->position_memory_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate memory for position image");
        }
        
        vkBindImageMemory(vk_device, gbuffer->position_image_, gbuffer->position_memory_, 0);
        
        // Create image view for position
        VkImageViewCreateInfo pos_view_info{};
        pos_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        pos_view_info.image = gbuffer->position_image_;
        pos_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        pos_view_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        pos_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        pos_view_info.subresourceRange.baseMipLevel = 0;
        pos_view_info.subresourceRange.levelCount = 1;
        pos_view_info.subresourceRange.baseArrayLayer = 0;
        pos_view_info.subresourceRange.layerCount = 1;
        
        if (vkCreateImageView(vk_device, &pos_view_info, nullptr, &gbuffer->position_view_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create position image view");
        }
        
        // Create normal image (RGBA16F)
        VkImageCreateInfo norm_info = pos_info;
        if (vkCreateImage(vk_device, &norm_info, nullptr, &gbuffer->normal_image_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create normal image");
        }
        
        VkMemoryRequirements norm_mem_reqs;
        vkGetImageMemoryRequirements(vk_device, gbuffer->normal_image_, &norm_mem_reqs);
        
        VkMemoryAllocateInfo norm_alloc{};
        norm_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        norm_alloc.allocationSize = norm_mem_reqs.size;
        norm_alloc.memoryTypeIndex = device->find_memory_type(
            norm_mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        if (vkAllocateMemory(vk_device, &norm_alloc, nullptr, &gbuffer->normal_memory_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate memory for normal image");
        }
        
        vkBindImageMemory(vk_device, gbuffer->normal_image_, gbuffer->normal_memory_, 0);
        
        VkImageViewCreateInfo norm_view_info = pos_view_info;
        norm_view_info.image = gbuffer->normal_image_;
        
        if (vkCreateImageView(vk_device, &norm_view_info, nullptr, &gbuffer->normal_view_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create normal image view");
        }
        
        // Create albedo image (RGBA8)
        VkImageCreateInfo alb_info = pos_info;
        alb_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        
        if (vkCreateImage(vk_device, &alb_info, nullptr, &gbuffer->albedo_image_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create albedo image");
        }
        
        VkMemoryRequirements alb_mem_reqs;
        vkGetImageMemoryRequirements(vk_device, gbuffer->albedo_image_, &alb_mem_reqs);
        
        VkMemoryAllocateInfo alb_alloc{};
        alb_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alb_alloc.allocationSize = alb_mem_reqs.size;
        alb_alloc.memoryTypeIndex = device->find_memory_type(
            alb_mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        if (vkAllocateMemory(vk_device, &alb_alloc, nullptr, &gbuffer->albedo_memory_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate memory for albedo image");
        }
        
        vkBindImageMemory(vk_device, gbuffer->albedo_image_, gbuffer->albedo_memory_, 0);
        
        VkImageViewCreateInfo alb_view_info = pos_view_info;
        alb_view_info.image = gbuffer->albedo_image_;
        alb_view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        
        if (vkCreateImageView(vk_device, &alb_view_info, nullptr, &gbuffer->albedo_view_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create albedo image view");
        }
        
        // Create material image (RGBA8)
        VkImageCreateInfo mat_info = alb_info;
        
        if (vkCreateImage(vk_device, &mat_info, nullptr, &gbuffer->material_image_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create material image");
        }
        
        VkMemoryRequirements mat_mem_reqs;
        vkGetImageMemoryRequirements(vk_device, gbuffer->material_image_, &mat_mem_reqs);
        
        VkMemoryAllocateInfo mat_alloc{};
        mat_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mat_alloc.allocationSize = mat_mem_reqs.size;
        mat_alloc.memoryTypeIndex = device->find_memory_type(
            mat_mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        if (vkAllocateMemory(vk_device, &mat_alloc, nullptr, &gbuffer->material_memory_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate memory for material image");
        }
        
        vkBindImageMemory(vk_device, gbuffer->material_image_, gbuffer->material_memory_, 0);
        
        VkImageViewCreateInfo mat_view_info = alb_view_info;
        mat_view_info.image = gbuffer->material_image_;
        
        if (vkCreateImageView(vk_device, &mat_view_info, nullptr, &gbuffer->material_view_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create material image view");
        }
        
        // Create depth image
        VkImageCreateInfo depth_info = pos_info;
        depth_info.format = VK_FORMAT_D32_SFLOAT;
        depth_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        
        if (vkCreateImage(vk_device, &depth_info, nullptr, &gbuffer->depth_image_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create depth image");
        }
        
        VkMemoryRequirements depth_mem_reqs;
        vkGetImageMemoryRequirements(vk_device, gbuffer->depth_image_, &depth_mem_reqs);
        
        VkMemoryAllocateInfo depth_alloc{};
        depth_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        depth_alloc.allocationSize = depth_mem_reqs.size;
        depth_alloc.memoryTypeIndex = device->find_memory_type(
            depth_mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        if (vkAllocateMemory(vk_device, &depth_alloc, nullptr, &gbuffer->depth_memory_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate memory for depth image");
        }
        
        vkBindImageMemory(vk_device, gbuffer->depth_image_, gbuffer->depth_memory_, 0);
        
        VkImageViewCreateInfo depth_view_info = pos_view_info;
        depth_view_info.image = gbuffer->depth_image_;
        depth_view_info.format = VK_FORMAT_D32_SFLOAT;
        depth_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        
        if (vkCreateImageView(vk_device, &depth_view_info, nullptr, &gbuffer->depth_view_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create depth image view");
        }
        
        // Create render pass
        gbuffer->create_render_pass();

        // Create framebuffer
        gbuffer->create_framebuffer();

        // Geometry pipelines + demo vertex buffer.
        gbuffer->shader_registry_ = std::make_unique<VulkanShaderRegistry>();
        if (!gbuffer->shader_registry_->initialize(device)) {
            throw std::runtime_error("Failed to initialize geometry shader registry");
        }
        gbuffer->create_demo_pipeline();
        gbuffer->create_demo_vertex_buffer();

        // Build the textured scene pipeline only when the caller supplied a
        // material descriptor set layout — without it the pipeline can't
        // bind material textures, so there's no point creating it.
        if (config.material_set_layout != VK_NULL_HANDLE) {
            gbuffer->create_scene_pipeline();
        }

        spdlog::info("VulkanGBuffer created: {}x{} (scene pipeline: {})",
                     config.width, config.height,
                     config.material_set_layout != VK_NULL_HANDLE ? "yes" : "no");
        return gbuffer;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to create VulkanGBuffer: {}", e.what());
        gbuffer->cleanup();
        return nullptr;
    }
}

void VulkanGBuffer::create_render_pass() {
    VkDevice vk_device = device_->get_device();
    
    // Attachment descriptions
    std::vector<VkAttachmentDescription> attachments(5);
    
    // Position attachment
    attachments[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    // Normal attachment
    attachments[1] = attachments[0];
    
    // Albedo attachment
    attachments[2] = attachments[0];
    attachments[2].format = VK_FORMAT_R8G8B8A8_UNORM;
    
    // Material attachment
    attachments[3] = attachments[2];
    
    // Depth attachment
    attachments[4].format = VK_FORMAT_D32_SFLOAT;
    attachments[4].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[4].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[4].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[4].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[4].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[4].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[4].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    
    // Color attachment references
    std::vector<VkAttachmentReference> color_refs(4);
    for (int i = 0; i < 4; ++i) {
        color_refs[i].attachment = i;
        color_refs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    
    // Depth attachment reference
    VkAttachmentReference depth_ref{};
    depth_ref.attachment = 4;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    // Subpass description
    VkAttachmentReference color_attachment_refs[4];
    for (int i = 0; i < 4; ++i) {
        color_attachment_refs[i].attachment = i;
        color_attachment_refs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 4;
    subpass.pColorAttachments = color_attachment_refs;
    subpass.pDepthStencilAttachment = &depth_ref;
    
    // Subpass dependency
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    
    // Create render pass
    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = static_cast<uint32_t>(attachments.size());
    render_pass_info.pAttachments = attachments.data();
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;
    
    if (vkCreateRenderPass(vk_device, &render_pass_info, nullptr, &render_pass_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create G-Buffer render pass");
    }
}

void VulkanGBuffer::create_framebuffer() {
    VkDevice vk_device = device_->get_device();
    
    std::vector<VkImageView> attachments = {
        position_view_,
        normal_view_,
        albedo_view_,
        material_view_,
        depth_view_
    };
    
    VkFramebufferCreateInfo framebuffer_info{};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = render_pass_;
    framebuffer_info.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebuffer_info.pAttachments = attachments.data();
    framebuffer_info.width = config_.width;
    framebuffer_info.height = config_.height;
    framebuffer_info.layers = 1;
    
    if (vkCreateFramebuffer(vk_device, &framebuffer_info, nullptr, &framebuffer_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create G-Buffer framebuffer");
    }
}

void VulkanGBuffer::begin_geometry_pass(VkCommandBuffer cmd) {
    VkRenderPassBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin_info.renderPass = render_pass_;
    begin_info.framebuffer = framebuffer_;
    begin_info.renderArea.offset = {0, 0};
    begin_info.renderArea.extent = {config_.width, config_.height};
    
    std::vector<VkClearValue> clear_values(5);
    clear_values[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clear_values[1].color = {{0.0f, 0.0f, 1.0f, 0.0f}};  // Normal default (0, 0, 1)
    clear_values[2].color = {{0.04f, 0.04f, 0.04f, 0.0f}};  // Dark gray background
    // Layout per the scene fragment shader: (emissive.rgb, ao). emissive
    // must clear to 0 so the lighting pass doesn't add a phantom glow on
    // background pixels; ao clears to 1 so ambient * albedo still shows
    // through the un-drawn region (instead of multiplying to black).
    clear_values[3].color = {{0.0f, 0.0f, 0.0f, 1.0f}};  // (emissive=0, ao=1)
    clear_values[4].depthStencil = {1.0f, 0};
    
    begin_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
    begin_info.pClearValues = clear_values.data();
    
    vkCmdBeginRenderPass(cmd, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanGBuffer::end_geometry_pass(VkCommandBuffer cmd) {
    vkCmdEndRenderPass(cmd);
}

namespace {

// Geometry-pass shaders. Vertex layout: pos (vec3), normal (vec3),
// albedo (vec3) — interleaved, packed tightly. The shader writes to all
// 4 G-Buffer color attachments; depth is implicit.
constexpr const char* kGeomVertSrc = R"GLSL(
#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inAlbedo;
layout(push_constant) uniform Constants {
    mat4 mvp;
    vec4 albedoMetallic; // overrides per-vertex albedo with this if alpha > 0; metallic = .a
} pc;
layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outAlbedo;
layout(location = 3) out float outMetallic;
void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    outWorldPos = inPosition;
    outNormal   = inNormal;
    outAlbedo   = (pc.albedoMetallic.r + pc.albedoMetallic.g + pc.albedoMetallic.b > 0.0)
                     ? pc.albedoMetallic.rgb : inAlbedo;
    outMetallic = pc.albedoMetallic.a;
}
)GLSL";

constexpr const char* kGeomFragSrc = R"GLSL(
#version 450
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inAlbedo;
layout(location = 3) in float inMetallic;
layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outAlbedo;
layout(location = 3) out vec4 outMaterial;
void main() {
    outPosition = vec4(inWorldPos, 1.0);
    outNormal   = vec4(normalize(inNormal), 0.5); // .a = roughness 0.5
    outAlbedo   = vec4(inAlbedo, inMetallic);
    outMaterial = vec4(0.0, 1.0, 0.0, 0.0);        // ID, AO, emission, reserved
}
)GLSL";

// Hardcoded triangle: position + normal (facing camera) + per-vertex
// color so the smoke test gets a recognisable RGB pattern in the G-Buffer.
struct DemoVertex {
    float position[3];
    float normal[3];
    float albedo[3];
};

constexpr DemoVertex kDemoTriangle[3] = {
    {{ 0.0f,  0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},
    {{-0.6f, -0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
    {{ 0.6f, -0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
};

// ---------------- Scene pipeline shaders ----------------
// Vertex: position (vec3) + normal (vec3) + uv (vec2) + tangent (vec4).
// Fragment samples 5 per-material textures (set=1) and writes them into
// the G-Buffer attachments. World position comes through unchanged from
// the model matrix multiplication of the input position.
constexpr const char* kSceneVertSrc = R"GLSL(
#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;
layout(push_constant) uniform PC {
    mat4 mvp;
    mat4 model;
} pc;
layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec3 outTangent;
layout(location = 4) out vec3 outBitangent;
void main() {
    vec4 wp     = pc.model * vec4(inPosition, 1.0);
    outWorldPos = wp.xyz;
    // Cheap normal-matrix approximation: use the model matrix's rotational
    // part. Fine for uniformly-scaled meshes; non-uniform scales need a
    // proper inverse-transpose, which we'll add when the scene system
    // tracks transform hierarchies.
    mat3 nrm    = mat3(pc.model);
    outNormal   = normalize(nrm * inNormal);
    outTangent  = normalize(nrm * inTangent.xyz);
    outBitangent = normalize(cross(outNormal, outTangent) * inTangent.w);
    outUV       = inUV;
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
}
)GLSL";

constexpr const char* kSceneFragSrc = R"GLSL(
#version 450
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;

layout(set = 1, binding = 0) uniform MaterialUBO {
    vec4  base_color_factor;
    float metallic_factor;
    float roughness_factor;
    float occlusion_strength;
    float normal_scale;
    vec4  emissive_factor;
} mat;
layout(set = 1, binding = 1) uniform sampler2D albedoMap;
layout(set = 1, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 3) uniform sampler2D mrMap; // green=R, blue=M (glTF)
layout(set = 1, binding = 4) uniform sampler2D aoMap;
layout(set = 1, binding = 5) uniform sampler2D emissiveMap;

layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outNormalRoughness;
layout(location = 2) out vec4 outAlbedoMetallic;
layout(location = 3) out vec4 outMaterial;

void main() {
    vec4 base = texture(albedoMap, inUV) * mat.base_color_factor;

    // Alpha cutout. emissive_factor.a doubles as alpha_cutoff (set on the
    // CPU side via EntityMaterialCache when the MeshRendererComponent has
    // its "Transparent" flag on). Cutoff == 0 means "opaque, no discard"
    // so opaque materials are unaffected.
    float alpha_cutoff = mat.emissive_factor.a;
    if (alpha_cutoff > 0.0 && base.a < alpha_cutoff) discard;

    vec3 nmap = texture(normalMap, inUV).xyz * 2.0 - 1.0;
    nmap.xy *= mat.normal_scale;
    mat3 TBN = mat3(normalize(inTangent), normalize(inBitangent), normalize(inNormal));
    vec3 N   = normalize(TBN * nmap);

    vec3 mr  = texture(mrMap, inUV).rgb;
    float roughness = clamp(mr.g * mat.roughness_factor, 0.04, 1.0);
    float metallic  = clamp(mr.b * mat.metallic_factor, 0.0, 1.0);

    // AO and emissive use additive / multiplier combine so the per-material
    // factors are visible without an asset texture (default textures are
    // white / black respectively, which would otherwise zero either signal
    // out via the strict glTF combine).
    float ao   = texture(aoMap, inUV).r * mat.occlusion_strength;
    vec3  emis = mat.emissive_factor.rgb + texture(emissiveMap, inUV).rgb;

    outPosition         = vec4(inWorldPos, 1.0);
    outNormalRoughness  = vec4(N, roughness);
    outAlbedoMetallic   = vec4(base.rgb, metallic);
    // Pack emissive RGB + AO into the 4th attachment. Previously this
    // stored emissive *luminance* only, so coloured glows were impossible
    // and the lighting pass never sampled it anyway.
    outMaterial         = vec4(emis, ao);
}
)GLSL";

// 80-byte push constant block: mat4 mvp + mat4 model. Under 128-byte spec
// guarantee. Vertex stage only — fragment reads from the material UBO.
struct ScenePushConstants {
    glm::mat4 mvp;
    glm::mat4 model;
};

} // namespace

void VulkanGBuffer::create_demo_pipeline() {
    VkDevice vk_device = device_->get_device();

    auto vert = shader_registry_->compile_glsl(kGeomVertSrc, ShaderStage::Vertex,   "gbuffer_demo.vert");
    if (!vert) {
        vert = shader_registry_->create_from_spirv(kGBufferDemoVertSpv, kGBufferDemoVertSpv_size,
                                                   ShaderStage::Vertex, "gbuffer_demo.vert");
    }
    auto frag = shader_registry_->compile_glsl(kGeomFragSrc, ShaderStage::Fragment, "gbuffer_demo.frag");
    if (!frag) {
        frag = shader_registry_->create_from_spirv(kGBufferDemoFragSpv, kGBufferDemoFragSpv_size,
                                                   ShaderStage::Fragment, "gbuffer_demo.frag");
    }
    if (!vert || !frag) {
        throw std::runtime_error("Failed to compile geometry-pass shaders");
    }

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.offset     = 0;
    push_range.size       = sizeof(GeometryPushConstants);

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount         = 0;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges    = &push_range;
    if (vkCreatePipelineLayout(vk_device, &layout_info, nullptr, &demo_pipeline_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create geometry pipeline layout");
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

    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(DemoVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attrs{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(DemoVertex, position)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(DemoVertex, normal)};
    attrs[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(DemoVertex, albedo)};

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
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;     // Demo triangle's winding can vary
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS;

    // 4 color attachments, no blending.
    std::array<VkPipelineColorBlendAttachmentState, 4> cbas{};
    for (auto& cba : cbas) {
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        cba.blendEnable    = VK_FALSE;
    }
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = static_cast<uint32_t>(cbas.size());
    cb.pAttachments    = cbas.data();

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
    info.layout              = demo_pipeline_layout_;
    info.renderPass          = render_pass_;
    info.subpass             = 0;
    if (vkCreateGraphicsPipelines(vk_device, VK_NULL_HANDLE, 1, &info, nullptr,
                                  &demo_pipeline_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create geometry pipeline");
    }
}

void VulkanGBuffer::create_scene_pipeline() {
    VkDevice vk_device = device_->get_device();

    auto vert = shader_registry_->compile_glsl(kSceneVertSrc, ShaderStage::Vertex,   "gbuffer_scene.vert");
    if (!vert) {
        vert = shader_registry_->create_from_spirv(kGBufferSceneVertSpv, kGBufferSceneVertSpv_size,
                                                   ShaderStage::Vertex, "gbuffer_scene.vert");
    }
    auto frag = shader_registry_->compile_glsl(kSceneFragSrc, ShaderStage::Fragment, "gbuffer_scene.frag");
    if (!frag) {
        frag = shader_registry_->create_from_spirv(kGBufferSceneFragSpv, kGBufferSceneFragSpv_size,
                                                   ShaderStage::Fragment, "gbuffer_scene.frag");
    }
    if (!vert || !frag) {
        throw std::runtime_error("Failed to compile scene-pass shaders");
    }

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.offset     = 0;
    push_range.size       = sizeof(ScenePushConstants);

    // set=0 unused, set=1 = per-material descriptor set (caller-supplied layout).
    // The pipeline layout requires set=0 to exist if we want to bind set=1, so
    // we declare a single layout slot at index 1. Vulkan accepts a layout with
    // any leading sets unset *as long as we don't reference them in shaders*.
    // We do: shaders only reference set=1, so we pass a 2-entry array with
    // set=0 = an empty layout and set=1 = the material layout.
    VkDescriptorSetLayoutCreateInfo empty_info{};
    empty_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    empty_info.bindingCount = 0;
    VkDescriptorSetLayout empty_layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(vk_device, &empty_info, nullptr, &empty_layout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create empty descriptor set layout for scene pipeline");
    }

    std::array<VkDescriptorSetLayout, 2> set_layouts = { empty_layout, config_.material_set_layout };

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount         = static_cast<uint32_t>(set_layouts.size());
    layout_info.pSetLayouts            = set_layouts.data();
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges    = &push_range;
    if (vkCreatePipelineLayout(vk_device, &layout_info, nullptr, &scene_pipeline_layout_) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(vk_device, empty_layout, nullptr);
        throw std::runtime_error("Failed to create scene pipeline layout");
    }

    // Empty layout is no longer needed once the pipeline layout is built.
    vkDestroyDescriptorSetLayout(vk_device, empty_layout, nullptr);

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
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    // cullMode is overridden per-variant below (back / none).
    rs.cullMode    = VK_CULL_MODE_BACK_BIT;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS;

    std::array<VkPipelineColorBlendAttachmentState, 4> cbas{};
    for (auto& cba : cbas) {
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        cba.blendEnable    = VK_FALSE;
    }
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = static_cast<uint32_t>(cbas.size());
    cb.pAttachments    = cbas.data();

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
    info.layout              = scene_pipeline_layout_;
    info.renderPass          = render_pass_;
    info.subpass             = 0;

    // Variant 1: back-face cull, used for primitives + glTF where winding
    // is trustworthy CCW.
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    if (vkCreateGraphicsPipelines(vk_device, VK_NULL_HANDLE, 1, &info, nullptr,
                                  &scene_pipeline_back_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create scene pipeline (back-cull)");
    }

    // Variant 2: no culling, used for OBJ-loaded meshes where the winding
    // can't be trusted.
    rs.cullMode = VK_CULL_MODE_NONE;
    if (vkCreateGraphicsPipelines(vk_device, VK_NULL_HANDLE, 1, &info, nullptr,
                                  &scene_pipeline_none_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create scene pipeline (no-cull)");
    }
}

void VulkanGBuffer::create_demo_vertex_buffer() {
    VkDevice vk_device = device_->get_device();
    constexpr VkDeviceSize bytes = sizeof(kDemoTriangle);

    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = bytes;
    bi.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vk_device, &bi, nullptr, &demo_vertex_buffer_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create demo vertex buffer");
    }

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(vk_device, demo_vertex_buffer_, &req);
    VkMemoryAllocateInfo alloc{};
    alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize  = req.size;
    alloc.memoryTypeIndex = device_->find_memory_type(
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(vk_device, &alloc, nullptr, &demo_vertex_buffer_memory_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate demo vertex buffer memory");
    }
    vkBindBufferMemory(vk_device, demo_vertex_buffer_, demo_vertex_buffer_memory_, 0);

    void* mapped = nullptr;
    vkMapMemory(vk_device, demo_vertex_buffer_memory_, 0, bytes, 0, &mapped);
    std::memcpy(mapped, kDemoTriangle, static_cast<size_t>(bytes));
    vkUnmapMemory(vk_device, demo_vertex_buffer_memory_);
}

void VulkanGBuffer::draw_demo_triangle(VkCommandBuffer cmd,
                                       const glm::mat4& view,
                                       const glm::mat4& proj) {
    if (demo_pipeline_ == VK_NULL_HANDLE || demo_vertex_buffer_ == VK_NULL_HANDLE) {
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

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, demo_pipeline_);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &demo_vertex_buffer_, &offset);

    GeometryPushConstants pc{};
    pc.mvp             = proj * view;
    pc.albedo_metallic = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); // use per-vertex albedo
    vkCmdPushConstants(cmd, demo_pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(pc), &pc);

    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void VulkanGBuffer::draw_items(VkCommandBuffer cmd,
                               const glm::mat4& view,
                               const glm::mat4& proj,
                               const glm::vec3& camera_position,
                               const DrawItem* draws,
                               size_t draw_count,
                               uint32_t* out_draw_calls,
                               uint32_t* out_triangles,
                               VulkanOcclusionCuller* occlusion,
                               VulkanHzbCuller* hzb_culler,
                               const Frustum* meshlet_frustum) {
    if (out_draw_calls) *out_draw_calls = 0;
    if (out_triangles)  *out_triangles  = 0;
    if (scene_pipeline_back_ == VK_NULL_HANDLE || scene_pipeline_none_ == VK_NULL_HANDLE ||
        draws == nullptr || draw_count == 0) {
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

    const glm::mat4 view_proj = proj * view;

    // Track the most recently bound mesh / material / pipeline so we don't
    // redundantly rebind. Sort draws by `mesh->is_double_sided()` first if
    // you care about minimising the pipeline rebind count (not done here —
    // typical scenes have at most a handful of cull-mode transitions).
    const Mesh*     last_mesh     = nullptr;
    const Material* last_material = nullptr;
    VkPipeline      last_pipeline = VK_NULL_HANDLE;

    for (size_t i = 0; i < draw_count; ++i) {
        const DrawItem& d = draws[i];
        if (!d.mesh || !d.material) continue;

        // Skip draws that were fully occluded last frame (per the HZB
        // culler's CPU-side AABB-vs-HZB test).
        const uint32_t draw_idx = static_cast<uint32_t>(i);
        if (hzb_culler != nullptr && !hzb_culler->was_visible(draw_idx)) {
            continue;
        }
        if (occlusion != nullptr && !occlusion->was_visible(draw_idx)) {
            continue;
        }

        // Pick pipeline: cull-back for trustworthy CCW meshes, cull-none
        // for untrusted (OBJ-loaded) meshes. Rebind only when changing.
        VkPipeline pipeline = d.mesh->is_double_sided()
                                  ? scene_pipeline_none_
                                  : scene_pipeline_back_;
        if (pipeline != last_pipeline) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            last_pipeline = pipeline;
            // Rebind material on next draw — pipeline change invalidates
            // descriptor set bindings even though both pipelines share a
            // layout (Vulkan spec: bindings persist if layouts are
            // compatible, which they are here, but it's cheap insurance).
            last_material = nullptr;
        }

        if (d.material != last_material) {
            d.material->bind(cmd, scene_pipeline_layout_, /*set=*/1);
            last_material = d.material;
        }
        if (d.mesh != last_mesh) {
            d.mesh->bind(cmd);
            last_mesh = d.mesh;
        }

        ScenePushConstants pc{};
        pc.mvp   = view_proj * d.model;
        pc.model = d.model;
        vkCmdPushConstants(cmd, scene_pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(pc), &pc);

        // Distance from camera to draw origin (model translation column).
        const glm::vec3 draw_origin = glm::vec3(d.model[3]);
        const float distance = glm::length(draw_origin - camera_position);
        const size_t lod = d.mesh->select_lod(d.submesh_index, distance);

        if (occlusion != nullptr) occlusion->begin_draw(cmd, draw_idx);

        // Per-meshlet path: only at LOD 0, only when the mesh actually has
        // meshlets, and only when the caller supplied a frustum to test
        // against. Each meshlet's local-space bounding sphere is
        // transformed to world space and frustum-tested individually; only
        // visible meshlets get a draw call. Falls back to draw_submesh
        // otherwise.
        bool drew_meshlets = false;
        if (meshlet_frustum != nullptr && lod == 0 &&
            d.submesh_index < d.mesh->submeshes().size()) {
            const auto& sm = d.mesh->submeshes()[d.submesh_index];
            if (!sm.lod0_meshlets.empty()) {
                // Approximate world-space scaling factor: largest column norm.
                // Exact only for uniform scaling but conservative for almost
                // every non-pathological transform.
                const float scale = std::max({
                    glm::length(glm::vec3(d.model[0])),
                    glm::length(glm::vec3(d.model[1])),
                    glm::length(glm::vec3(d.model[2]))
                });
                for (const auto& ml : sm.lod0_meshlets) {
                    const glm::vec3 wc = glm::vec3(d.model * glm::vec4(ml.center, 1.0f));
                    const float wr = ml.radius * scale;
                    if (!meshlet_frustum->is_sphere_visible(wc, wr)) continue;
                    d.mesh->draw_meshlet(cmd, ml.first_index, ml.index_count);
                    if (out_draw_calls) ++(*out_draw_calls);
                    if (out_triangles) *out_triangles += ml.index_count / 3;
                }
                drew_meshlets = true;
            }
        }
        if (!drew_meshlets) {
            d.mesh->draw_submesh(cmd, d.submesh_index, lod);
            if (out_draw_calls) ++(*out_draw_calls);
            if (out_triangles && d.submesh_index < d.mesh->submeshes().size()) {
                const auto& sm = d.mesh->submeshes()[d.submesh_index];
                const size_t li = std::min(lod, sm.lods.empty() ? 0 : sm.lods.size() - 1);
                if (!sm.lods.empty()) {
                    *out_triangles += sm.lods[li].index_count / 3;
                }
            }
        }
        if (occlusion != nullptr) occlusion->end_draw(cmd, draw_idx);
    }
}

void VulkanGBuffer::begin_lighting_pass(VkCommandBuffer cmd) {
    // Lighting pass reads from G-Buffer, no need to begin another render pass here
    // The caller will handle the lighting render pass
}

void VulkanGBuffer::end_lighting_pass(VkCommandBuffer cmd) {
    // Lighting pass cleanup handled by caller
}

void VulkanGBuffer::clear(VkCommandBuffer cmd, const glm::vec4& clear_color) {
    // Clear values are set in begin_geometry_pass
}

void VulkanGBuffer::resize(VulkanDevice* device, uint32_t width, uint32_t height) {
    if (width == config_.width && height == config_.height) {
        return;
    }
    
    cleanup();
    config_.width = width;
    config_.height = height;
    
    auto new_gbuffer = create(device, config_);
    if (new_gbuffer) {
        *this = std::move(*new_gbuffer);
    }
}

void VulkanGBuffer::cleanup() {
    if (!device_) return;

    VkDevice vk_device = device_->get_device();

    if (demo_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(vk_device, demo_pipeline_, nullptr);
        demo_pipeline_ = VK_NULL_HANDLE;
    }
    if (demo_pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vk_device, demo_pipeline_layout_, nullptr);
        demo_pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (scene_pipeline_back_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(vk_device, scene_pipeline_back_, nullptr);
        scene_pipeline_back_ = VK_NULL_HANDLE;
    }
    if (scene_pipeline_none_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(vk_device, scene_pipeline_none_, nullptr);
        scene_pipeline_none_ = VK_NULL_HANDLE;
    }
    if (scene_pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vk_device, scene_pipeline_layout_, nullptr);
        scene_pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (demo_vertex_buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(vk_device, demo_vertex_buffer_, nullptr);
        demo_vertex_buffer_ = VK_NULL_HANDLE;
    }
    if (demo_vertex_buffer_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, demo_vertex_buffer_memory_, nullptr);
        demo_vertex_buffer_memory_ = VK_NULL_HANDLE;
    }
    // Drop the registry before the device-bound resources below — its
    // VkShaderModules need the device live.
    shader_registry_.reset();

    if (framebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(vk_device, framebuffer_, nullptr);
        framebuffer_ = VK_NULL_HANDLE;
    }
    
    if (render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vk_device, render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }
    
    // Destroy image views
    if (position_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vk_device, position_view_, nullptr);
        position_view_ = VK_NULL_HANDLE;
    }
    if (normal_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vk_device, normal_view_, nullptr);
        normal_view_ = VK_NULL_HANDLE;
    }
    if (albedo_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vk_device, albedo_view_, nullptr);
        albedo_view_ = VK_NULL_HANDLE;
    }
    if (material_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vk_device, material_view_, nullptr);
        material_view_ = VK_NULL_HANDLE;
    }
    if (depth_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vk_device, depth_view_, nullptr);
        depth_view_ = VK_NULL_HANDLE;
    }
    
    // Destroy images
    if (position_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(vk_device, position_image_, nullptr);
        position_image_ = VK_NULL_HANDLE;
    }
    if (normal_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(vk_device, normal_image_, nullptr);
        normal_image_ = VK_NULL_HANDLE;
    }
    if (albedo_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(vk_device, albedo_image_, nullptr);
        albedo_image_ = VK_NULL_HANDLE;
    }
    if (material_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(vk_device, material_image_, nullptr);
        material_image_ = VK_NULL_HANDLE;
    }
    if (depth_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(vk_device, depth_image_, nullptr);
        depth_image_ = VK_NULL_HANDLE;
    }
    
    // Free memory
    if (position_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, position_memory_, nullptr);
        position_memory_ = VK_NULL_HANDLE;
    }
    if (normal_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, normal_memory_, nullptr);
        normal_memory_ = VK_NULL_HANDLE;
    }
    if (albedo_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, albedo_memory_, nullptr);
        albedo_memory_ = VK_NULL_HANDLE;
    }
    if (material_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, material_memory_, nullptr);
        material_memory_ = VK_NULL_HANDLE;
    }
    if (depth_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, depth_memory_, nullptr);
        depth_memory_ = VK_NULL_HANDLE;
    }
}

VkFormat VulkanGBuffer::format_to_vk(GBufferFormat fmt) {
    switch (fmt) {
        case GBufferFormat::RGBA8:    return VK_FORMAT_R8G8B8A8_UNORM;
        case GBufferFormat::RGBA16F:  return VK_FORMAT_R16G16B16A16_SFLOAT;
        case GBufferFormat::RGBA32F:  return VK_FORMAT_R32G32B32A32_SFLOAT;
        case GBufferFormat::RGB16F:   return VK_FORMAT_R16G16B16_SFLOAT;
        case GBufferFormat::RG16F:    return VK_FORMAT_R16G16_SFLOAT;
        case GBufferFormat::R32F:     return VK_FORMAT_R32_SFLOAT;
        case GBufferFormat::R8:       return VK_FORMAT_R8_UNORM;
        default:                       return VK_FORMAT_R16G16B16A16_SFLOAT;
    }
}

} // namespace gws::renderer::gpu
