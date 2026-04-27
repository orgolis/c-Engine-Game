/**
 * @file vulkan_lighting_pass.cpp
 * @brief Vulkan lighting pass implementation
 */

#include "vulkan_lighting_pass.h"
#include "vulkan_device.h"
#include "vulkan_g_buffer.h"
#include "vulkan_shader_registry.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <algorithm>
#include <array>
#include <cstring>

namespace gws::renderer::gpu {

VulkanLightingPass::~VulkanLightingPass() {
    cleanup();
}

std::unique_ptr<VulkanLightingPass> VulkanLightingPass::create(VulkanDevice* device,
                                                               const LightingConfig& config,
                                                               VulkanGBuffer* gbuffer) {
    auto pass = std::make_unique<VulkanLightingPass>();
    pass->device_ = device;
    pass->gbuffer_ = gbuffer;
    pass->config_ = config;
    
    try {
        const uint32_t w = gbuffer->get_width();
        const uint32_t h = gbuffer->get_height();

        pass->shader_registry_ = std::make_unique<VulkanShaderRegistry>();
        if (!pass->shader_registry_->initialize(device)) {
            throw std::runtime_error("Failed to initialize shader registry");
        }

        pass->create_light_buffer();
        pass->create_descriptor_sets();
        pass->create_output_image(w, h);
        pass->create_render_pass();
        pass->create_framebuffer(w, h);
        pass->create_gbuffer_sampler();
        pass->create_shadow_sampler();
        pass->create_dummy_shadow_textures();
        pass->create_pipeline();
        pass->update_descriptor_set();

        spdlog::info("VulkanLightingPass created with {} max lights", config.max_lights);
        return pass;
    } catch (const std::exception& e) {
        spdlog::error("Failed to create VulkanLightingPass: {}", e.what());
        pass->cleanup();
        return nullptr;
    }
}

void VulkanLightingPass::create_light_buffer() {
    VkDevice vk_device = device_->get_device();
    VkPhysicalDevice physical_device = device_->get_physical_device();
    
    VkDeviceSize buffer_size = sizeof(Light) * config_.max_lights;
    
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(vk_device, &buffer_info, nullptr, &light_buffer_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create light buffer");
    }
    
    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(vk_device, light_buffer_, &mem_reqs);
    
    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = device_->find_memory_type(
        mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(vk_device, &alloc_info, nullptr, &light_buffer_memory_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate light buffer memory");
    }
    
    vkBindBufferMemory(vk_device, light_buffer_, light_buffer_memory_, 0);
}

void VulkanLightingPass::create_descriptor_sets() {
    VkDevice vk_device = device_->get_device();

    // 5 G-Buffer samplers + 2 shadow samplers + 1 light SSBO. Bindings
    // match the PBR shader's set=0 layout (positionTex=0, normalTex=1,
    // albedoTex=2, materialTex=3, depthTex=4, shadowMap=5,
    // pointShadowMap=6, lightBuffer=7).
    std::array<VkDescriptorSetLayoutBinding, 8> bindings{};
    for (uint32_t i = 0; i < 7; ++i) {
        bindings[i].binding         = i;
        bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    bindings[7].binding         = 7;
    bindings[7].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
    layout_info.pBindings    = bindings.data();

    if (vkCreateDescriptorSetLayout(vk_device, &layout_info, nullptr, &descriptor_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout");
    }

    std::array<VkDescriptorPoolSize, 2> pool_sizes{};
    pool_sizes[0] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 7};
    pool_sizes[1] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1};

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes    = pool_sizes.data();
    pool_info.maxSets       = 1;
    
    if (vkCreateDescriptorPool(vk_device, &pool_info, nullptr, &descriptor_pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool");
    }
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = descriptor_pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &descriptor_layout_;
    
    if (vkAllocateDescriptorSets(vk_device, &alloc_info, &descriptor_set_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor set");
    }
}

void VulkanLightingPass::create_output_image(uint32_t width, uint32_t height) {
    VkDevice vk_device = device_->get_device();
    VkPhysicalDevice physical_device = device_->get_physical_device();
    
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateImage(vk_device, &image_info, nullptr, &output_image_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create output image");
    }
    
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(vk_device, output_image_, &mem_reqs);
    
    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = device_->find_memory_type(
        mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(vk_device, &alloc_info, nullptr, &output_memory_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate output image memory");
    }
    
    vkBindImageMemory(vk_device, output_image_, output_memory_, 0);
    
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = output_image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(vk_device, &view_info, nullptr, &output_view_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create output image view");
    }
}

void VulkanLightingPass::create_render_pass() {
    VkDevice vk_device = device_->get_device();

    VkAttachmentDescription color{};
    color.format         = VK_FORMAT_R16G16B16A16_SFLOAT;
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &color_ref;

    // External → subpass 0: wait for prior fragment shader reads to finish
    // before we write the color attachment. Subpass 0 → external: ensure the
    // color writes are visible to subsequent fragment shader reads (the
    // post-processing or composite stage).
    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass    = 0;
    deps[0].srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    deps[1].srcSubpass    = 0;
    deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rp_info{};
    rp_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_info.attachmentCount = 1;
    rp_info.pAttachments    = &color;
    rp_info.subpassCount    = 1;
    rp_info.pSubpasses      = &subpass;
    rp_info.dependencyCount = static_cast<uint32_t>(deps.size());
    rp_info.pDependencies   = deps.data();

    if (vkCreateRenderPass(vk_device, &rp_info, nullptr, &render_pass_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create lighting pass render pass");
    }
}

void VulkanLightingPass::create_framebuffer(uint32_t width, uint32_t height) {
    VkDevice vk_device = device_->get_device();

    VkFramebufferCreateInfo fb_info{};
    fb_info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.renderPass      = render_pass_;
    fb_info.attachmentCount = 1;
    fb_info.pAttachments    = &output_view_;
    fb_info.width           = width;
    fb_info.height          = height;
    fb_info.layers          = 1;

    if (vkCreateFramebuffer(vk_device, &fb_info, nullptr, &framebuffer_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create lighting pass framebuffer");
    }
}

// PBR deferred-lighting shader, adapted from the shipped
// shaders/lighting_pass.frag. Differences from the shipped version:
//   * LightBuffer is a readonly SSBO (dynamic-size) instead of a UBO with a
//     fixed-size array — the existing CPU-side buffer was already allocated
//     as a storage buffer.
//   * `lightCount` lives in the push-constant block instead of inside the
//     buffer (saves a separate UBO allocation).
//   * `viewInv` / `projInv` push constants from the shipped shader are
//     dropped — they were dead code (world position is read from the
//     G-Buffer, not reconstructed from depth).
// Total push-constant size: 32 bytes (well under the 128-byte spec
// guarantee).
static const char* kLightingVertSrc = R"GLSL(
#version 450
layout(location = 0) out vec2 outTexCoord;
void main() {
    outTexCoord = vec2((gl_VertexIndex == 0) ? 2.0 : 0.0,
                       (gl_VertexIndex == 2) ? 2.0 : 0.0);
    gl_Position = vec4(outTexCoord * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

static const char* kLightingFragSrc = R"GLSL(
#version 450

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D positionTex;
layout(set = 0, binding = 1) uniform sampler2D normalTex;
layout(set = 0, binding = 2) uniform sampler2D albedoTex;
layout(set = 0, binding = 3) uniform sampler2D materialTex;
layout(set = 0, binding = 4) uniform sampler2D depthTex;
layout(set = 0, binding = 5) uniform sampler2DArray shadowMap;
layout(set = 0, binding = 6) uniform samplerCube   pointShadowMap;

struct Light {
    vec4 position;
    vec4 direction;
    vec4 colorRadius;
    vec4 attenuation;
    mat4 shadowMatrix;
    uint shadowMapIndex;
    uint castsShadow;
    uint _pad0;
    uint _pad1;
};

layout(set = 0, binding = 7) readonly buffer LightBuffer {
    Light lights[];
} lightBuffer;

layout(push_constant) uniform Constants {
    vec3  cameraPos;
    float ambientIntensity;
    vec3  ambientColor;
    uint  lightCount;
} pc;

const float PI = 3.14159265359;

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / max(denom, 1e-6);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) *
           GeometrySchlickGGX(NdotL, roughness);
}

float PCF(sampler2DArray sm, vec3 coords, float bias) {
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(sm, 0).xy);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(sm, coords + vec3(vec2(x, y) * texelSize, 0.0)).r;
            shadow += (coords.z - bias) > pcfDepth ? 0.0 : 1.0;
        }
    }
    return shadow / 9.0;
}

float directionalShadow(vec3 worldPos, vec3 normal, Light L) {
    if (L.castsShadow == 0u) return 1.0;
    vec4 fragPosLS = L.shadowMatrix * vec4(worldPos, 1.0);
    vec3 proj = fragPosLS.xyz / max(fragPosLS.w, 1e-4);
    proj.xy = proj.xy * 0.5 + 0.5;
    if (proj.z > 1.0) return 1.0;
    float bias = max(0.05 * (1.0 - dot(normal, -L.direction.xyz)), 0.005);
    return PCF(shadowMap, vec3(proj.xy, float(L.shadowMapIndex)), bias);
}

float pointShadow(vec3 worldPos, Light L) {
    if (L.castsShadow == 0u) return 1.0;
    vec3 fragToLight = worldPos - L.position.xyz;
    float closest = texture(pointShadowMap, fragToLight).r * L.colorRadius.w;
    return length(fragToLight) - 0.005 > closest ? 0.0 : 1.0;
}

vec3 evaluateLight(Light L, vec3 N, vec3 V, vec3 worldPos,
                   vec3 albedo, float roughness, float metallic) {
    vec3 lightDir;
    float distance    = 1.0;
    float attenuation = 1.0;
    if (L.position.w == 0.0) {
        lightDir = normalize(-L.direction.xyz);
    } else {
        vec3 toLight = L.position.xyz - worldPos;
        distance     = length(toLight);
        lightDir     = toLight / max(distance, 1e-4);
        attenuation  = 1.0 / max(L.attenuation.x +
                                 L.attenuation.y * distance +
                                 L.attenuation.z * distance * distance, 1e-4);
    }

    vec3 H = normalize(V + lightDir);
    float NdotL = max(dot(N, lightDir), 0.0);
    float NdotV = max(dot(N, V),         0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F  = fresnelSchlick(max(dot(H, V), 0.0), F0);
    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, lightDir, roughness);

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    vec3 numerator   = NDF * G * F;
    float denominator = 4.0 * NdotV * NdotL + 1e-4;
    vec3 specular = numerator / denominator;

    vec3 radiance = L.colorRadius.xyz * L.direction.w * attenuation;
    float shadow  = (L.position.w == 1.0)
                        ? pointShadow(worldPos, L)
                        : directionalShadow(worldPos, N, L);

    return (kD * albedo / PI + specular) * radiance * NdotL * shadow;
}

void main() {
    vec4 normalSample   = texture(normalTex,   inTexCoord);
    vec4 albedoSample   = texture(albedoTex,   inTexCoord);
    vec3 worldPos = texture(positionTex, inTexCoord).xyz;
    vec3 N        = normalize(normalSample.xyz);
    float roughness = normalSample.a;
    vec3 albedo   = albedoSample.rgb;
    float metallic = albedoSample.a;

    vec3 V = normalize(pc.cameraPos - worldPos);

    vec3 color = pc.ambientColor * pc.ambientIntensity * albedo;
    for (uint i = 0u; i < pc.lightCount; ++i) {
        color += evaluateLight(lightBuffer.lights[i], N, V, worldPos, albedo, roughness, metallic);
    }

    outColor = vec4(color, 1.0);
}
)GLSL";

void VulkanLightingPass::create_gbuffer_sampler() {
    VkDevice vk_device = device_->get_device();

    VkSamplerCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter    = VK_FILTER_NEAREST;
    info.minFilter    = VK_FILTER_NEAREST;
    info.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

    if (vkCreateSampler(vk_device, &info, nullptr, &gbuffer_sampler_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create G-Buffer sampler");
    }
}

void VulkanLightingPass::create_shadow_sampler() {
    VkDevice vk_device = device_->get_device();

    VkSamplerCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter    = VK_FILTER_LINEAR;
    info.minFilter    = VK_FILTER_LINEAR;
    info.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; // depth=1 → "lit"

    if (vkCreateSampler(vk_device, &info, nullptr, &shadow_sampler_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow sampler");
    }
}

void VulkanLightingPass::create_dummy_shadow_textures() {
    VkDevice vk_device = device_->get_device();

    auto allocate_image = [&](VkImageCreateInfo& image_info,
                              VkImage& out_image, VkDeviceMemory& out_mem) {
        if (vkCreateImage(vk_device, &image_info, nullptr, &out_image) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create dummy shadow image");
        }
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(vk_device, out_image, &req);
        VkMemoryAllocateInfo alloc{};
        alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize  = req.size;
        alloc.memoryTypeIndex = device_->find_memory_type(
            req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(vk_device, &alloc, nullptr, &out_mem) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate dummy shadow memory");
        }
        vkBindImageMemory(vk_device, out_image, out_mem, 0);
    };

    // 2D array (1 layer) — matches sampler2DArray binding.
    VkImageCreateInfo arr_info{};
    arr_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    arr_info.imageType     = VK_IMAGE_TYPE_2D;
    arr_info.format        = VK_FORMAT_D32_SFLOAT;
    arr_info.extent        = {1, 1, 1};
    arr_info.mipLevels     = 1;
    arr_info.arrayLayers   = 1;
    arr_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    arr_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    arr_info.usage         = VK_IMAGE_USAGE_SAMPLED_BIT;
    arr_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    arr_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    allocate_image(arr_info, dummy_shadow_2d_array_image_, dummy_shadow_2d_array_mem_);

    VkImageViewCreateInfo arr_view{};
    arr_view.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    arr_view.image    = dummy_shadow_2d_array_image_;
    arr_view.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    arr_view.format   = VK_FORMAT_D32_SFLOAT;
    arr_view.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    arr_view.subresourceRange.baseMipLevel   = 0;
    arr_view.subresourceRange.levelCount     = 1;
    arr_view.subresourceRange.baseArrayLayer = 0;
    arr_view.subresourceRange.layerCount     = 1;
    if (vkCreateImageView(vk_device, &arr_view, nullptr, &dummy_shadow_2d_array_view_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create dummy 2D-array shadow view");
    }

    // Cubemap (6 faces) — matches samplerCube binding.
    VkImageCreateInfo cube_info = arr_info;
    cube_info.arrayLayers = 6;
    cube_info.flags       = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    allocate_image(cube_info, dummy_shadow_cube_image_, dummy_shadow_cube_mem_);

    VkImageViewCreateInfo cube_view = arr_view;
    cube_view.image    = dummy_shadow_cube_image_;
    cube_view.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    cube_view.subresourceRange.layerCount = 6;
    if (vkCreateImageView(vk_device, &cube_view, nullptr, &dummy_shadow_cube_view_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create dummy cube shadow view");
    }

    // The dummy images are still in UNDEFINED layout, but the descriptor
    // says SHADER_READ_ONLY_OPTIMAL. Run a one-shot transition to bring
    // them into the right layout. Use the device's command pool and the
    // graphics queue directly — the shadow textures are static, this only
    // happens once at startup.
    VkCommandBufferAllocateInfo cmd_alloc{};
    cmd_alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_alloc.commandPool        = device_->get_command_pool();
    cmd_alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(vk_device, &cmd_alloc, &cmd) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate dummy-shadow transition command buffer");
    }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    auto transition = [&](VkImage image, uint32_t layer_count) {
        VkImageMemoryBarrier b{};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = image;
        b.srcAccessMask       = 0;
        b.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        b.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
        b.subresourceRange.baseMipLevel   = 0;
        b.subresourceRange.levelCount     = 1;
        b.subresourceRange.baseArrayLayer = 0;
        b.subresourceRange.layerCount     = layer_count;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
    };
    transition(dummy_shadow_2d_array_image_, 1);
    transition(dummy_shadow_cube_image_,     6);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    vkQueueSubmit(device_->get_graphics_queue(), 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(device_->get_graphics_queue());
    vkFreeCommandBuffers(vk_device, device_->get_command_pool(), 1, &cmd);
}

void VulkanLightingPass::create_pipeline() {
    VkDevice vk_device = device_->get_device();

    auto vert = shader_registry_->compile_glsl(kLightingVertSrc, ShaderStage::Vertex,
                                               "lighting_pass.vert");
    auto frag = shader_registry_->compile_glsl(kLightingFragSrc, ShaderStage::Fragment,
                                               "lighting_pass.frag");
    if (!vert || !frag) {
        throw std::runtime_error("Failed to compile lighting shaders");
    }

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push_range.offset     = 0;
    push_range.size       = sizeof(float) * 8; // vec3 cameraPos + float ambient + vec3 ambientColor + uint lightCount

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount         = 1;
    layout_info.pSetLayouts            = &descriptor_layout_;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges    = &push_range;

    if (vkCreatePipelineLayout(vk_device, &layout_info, nullptr, &pipeline_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create lighting pipeline layout");
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

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    std::array<VkDynamicState, 2> dyn_states{
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
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
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments    = &cba;

    VkGraphicsPipelineCreateInfo info{};
    info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount          = static_cast<uint32_t>(stages.size());
    info.pStages             = stages.data();
    info.pVertexInputState   = &vi;
    info.pInputAssemblyState = &ia;
    info.pViewportState      = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState   = &ms;
    info.pColorBlendState    = &cb;
    info.pDynamicState       = &dyn;
    info.layout              = pipeline_layout_;
    info.renderPass          = render_pass_;
    info.subpass             = 0;

    if (vkCreateGraphicsPipelines(vk_device, VK_NULL_HANDLE, 1, &info, nullptr,
                                  &pipeline_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create lighting pipeline");
    }
}

void VulkanLightingPass::update_descriptor_set() {
    if (descriptor_set_ == VK_NULL_HANDLE || gbuffer_ == nullptr ||
        gbuffer_sampler_ == VK_NULL_HANDLE || shadow_sampler_ == VK_NULL_HANDLE) {
        return;
    }

    std::array<VkImageView, 5> gbuffer_views{
        gbuffer_->get_position_view(),
        gbuffer_->get_normal_view(),
        gbuffer_->get_albedo_view(),
        gbuffer_->get_material_view(),
        gbuffer_->get_depth_view(),
    };

    std::array<VkDescriptorImageInfo, 7> image_infos{};
    for (uint32_t i = 0; i < 5; ++i) {
        image_infos[i].sampler     = gbuffer_sampler_;
        image_infos[i].imageView   = gbuffer_views[i];
        image_infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    // Shadow maps fall back to the dummy 1×1 textures when no real shadow
    // map has been bound. The shader reads them unconditionally; the
    // `castsShadow` flag in each Light entry controls whether the result
    // is actually used.
    image_infos[5].sampler     = directional_shadow_sampler_ ? directional_shadow_sampler_ : shadow_sampler_;
    image_infos[5].imageView   = directional_shadow_view_    ? directional_shadow_view_    : dummy_shadow_2d_array_view_;
    image_infos[5].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_infos[6].sampler     = point_shadow_sampler_       ? point_shadow_sampler_       : shadow_sampler_;
    image_infos[6].imageView   = point_shadow_view_          ? point_shadow_view_          : dummy_shadow_cube_view_;
    image_infos[6].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 8> writes{};
    for (uint32_t i = 0; i < 7; ++i) {
        writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet          = descriptor_set_;
        writes[i].dstBinding      = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo      = &image_infos[i];
    }

    VkDescriptorBufferInfo light_info{};
    light_info.buffer = light_buffer_;
    light_info.offset = 0;
    light_info.range  = VK_WHOLE_SIZE;

    writes[7].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[7].dstSet          = descriptor_set_;
    writes[7].dstBinding      = 7;
    writes[7].descriptorCount = 1;
    writes[7].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[7].pBufferInfo     = &light_info;

    vkUpdateDescriptorSets(device_->get_device(),
                           static_cast<uint32_t>(writes.size()), writes.data(),
                           0, nullptr);
}

void VulkanLightingPass::upload_lights() {
    if (light_buffer_memory_ == VK_NULL_HANDLE || lights_.empty()) {
        return;
    }
    const VkDeviceSize bytes = sizeof(Light) * lights_.size();
    void* mapped = nullptr;
    if (vkMapMemory(device_->get_device(), light_buffer_memory_, 0, bytes, 0, &mapped) != VK_SUCCESS) {
        spdlog::warn("VulkanLightingPass::upload_lights: vkMapMemory failed");
        return;
    }
    std::memcpy(mapped, lights_.data(), static_cast<size_t>(bytes));
    vkUnmapMemory(device_->get_device(), light_buffer_memory_);
}

void VulkanLightingPass::add_light(const Light& light) {
    if (light_count_ < config_.max_lights) {
        lights_.push_back(light);
        light_count_++;
    } else {
        spdlog::warn("Light limit reached, cannot add more lights");
    }
}

uint32_t VulkanLightingPass::add_directional_light(const glm::vec3& direction,
                                                   const glm::vec3& color,
                                                   float intensity,
                                                   bool casts_shadow) {
    if (light_count_ >= config_.max_lights) {
        spdlog::warn("Light limit reached, cannot add directional light");
        return UINT32_MAX;
    }
    Light light{};
    // Lighting shader convention: position.w == 0 => directional.
    light.position    = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    const glm::vec3 dir =
        (glm::dot(direction, direction) > 0.0f) ? glm::normalize(direction)
                                                : glm::vec3(0.0f, -1.0f, 0.0f);
    light.direction   = glm::vec4(dir, intensity);
    light.color_radius = glm::vec4(color, 0.0f); // Radius unused for directional.
    light.attenuation = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    light.shadow_matrix = glm::mat4(1.0f);
    light.shadow_map_index = 0;
    light.casts_shadow = casts_shadow ? 1u : 0u;
    add_light(light);
    return light_count_ - 1;
}

uint32_t VulkanLightingPass::add_point_light(const glm::vec3& position,
                                             const glm::vec3& color,
                                             float intensity,
                                             float radius,
                                             bool casts_shadow) {
    if (light_count_ >= config_.max_lights) {
        spdlog::warn("Light limit reached, cannot add point light");
        return UINT32_MAX;
    }
    Light light{};
    // position.w == 1 => point light.
    light.position = glm::vec4(position, 1.0f);
    light.direction = glm::vec4(0.0f, 0.0f, 0.0f, intensity);
    light.color_radius = glm::vec4(color, radius);
    // Standard "OpenGL distance falloff" curve: 1, 4.5/r, 75/(r*r).
    const float r = (radius > 0.0f) ? radius : 1.0f;
    light.attenuation = glm::vec4(1.0f, 4.5f / r, 75.0f / (r * r), 0.0f);
    light.shadow_matrix = glm::mat4(1.0f);
    light.casts_shadow = casts_shadow ? 1u : 0u;
    add_light(light);
    return light_count_ - 1;
}

uint32_t VulkanLightingPass::add_spot_light(const glm::vec3& position,
                                            const glm::vec3& direction,
                                            const glm::vec3& color,
                                            float intensity,
                                            float range,
                                            float outer_cone_cos,
                                            bool casts_shadow) {
    if (light_count_ >= config_.max_lights) {
        spdlog::warn("Light limit reached, cannot add spot light");
        return UINT32_MAX;
    }
    Light light{};
    // position.w == 2 => spot light.
    light.position = glm::vec4(position, 2.0f);
    const glm::vec3 dir =
        (glm::dot(direction, direction) > 0.0f) ? glm::normalize(direction)
                                                : glm::vec3(0.0f, -1.0f, 0.0f);
    light.direction = glm::vec4(dir, intensity);
    light.color_radius = glm::vec4(color, range);
    const float r = (range > 0.0f) ? range : 1.0f;
    light.attenuation = glm::vec4(1.0f, 4.5f / r, 75.0f / (r * r), outer_cone_cos);
    light.shadow_matrix = glm::mat4(1.0f);
    light.casts_shadow = casts_shadow ? 1u : 0u;
    add_light(light);
    return light_count_ - 1;
}

void VulkanLightingPass::set_directional_shadow_map(VkImageView view, VkSampler sampler) {
    directional_shadow_view_    = view;
    directional_shadow_sampler_ = sampler;
    update_descriptor_set();
}

void VulkanLightingPass::set_point_shadow_map(VkImageView view, VkSampler sampler) {
    point_shadow_view_    = view;
    point_shadow_sampler_ = sampler;
    update_descriptor_set();
}

void VulkanLightingPass::update_light(uint32_t index, const Light& light) {
    if (index < light_count_) {
        lights_[index] = light;
    }
}

void VulkanLightingPass::remove_light(uint32_t index) {
    if (index < light_count_) {
        lights_.erase(lights_.begin() + index);
        light_count_--;
    }
}

void VulkanLightingPass::clear_lights() {
    lights_.clear();
    light_count_ = 0;
}

const Light& VulkanLightingPass::get_light(uint32_t index) const {
    if (index >= light_count_) {
        throw std::out_of_range("Light index out of range");
    }
    return lights_[index];
}

void VulkanLightingPass::begin_pass(VkCommandBuffer cmd, uint32_t width, uint32_t height) {
    if (render_pass_ == VK_NULL_HANDLE || framebuffer_ == VK_NULL_HANDLE) {
        spdlog::warn("VulkanLightingPass::begin_pass called before render pass setup");
        return;
    }

    // Push the latest CPU-side light list to the GPU buffer just before the
    // pass executes. Buffer is host-coherent so no flush is required.
    upload_lights();

    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderPassBeginInfo info{};
    info.sType                 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.renderPass            = render_pass_;
    info.framebuffer           = framebuffer_;
    info.renderArea.offset     = {0, 0};
    info.renderArea.extent     = {width, height};
    info.clearValueCount       = 1;
    info.pClearValues          = &clear;

    vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanLightingPass::end_pass(VkCommandBuffer cmd) {
    if (render_pass_ == VK_NULL_HANDLE) {
        return;
    }
    vkCmdEndRenderPass(cmd);
}

void VulkanLightingPass::render(VkCommandBuffer cmd) {
    if (pipeline_ == VK_NULL_HANDLE) {
        spdlog::warn("Lighting pass pipeline not initialized");
        return;
    }

    const uint32_t width  = gbuffer_ ? gbuffer_->get_width()  : 0;
    const uint32_t height = gbuffer_ ? gbuffer_->get_height() : 0;

    VkViewport vp{};
    vp.x        = 0.0f;
    vp.y        = 0.0f;
    vp.width    = static_cast<float>(width);
    vp.height   = static_cast<float>(height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {width, height};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_,
                            0, 1, &descriptor_set_, 0, nullptr);

    // Push constants match the PBR shader's `Constants` block.
    // Layout (std430-like, all 4-byte slots):
    //   vec3  cameraPos        [0..2]  +  float ambientIntensity [3]
    //   vec3  ambientColor     [4..6]  +  uint  lightCount       [7]
    struct {
        float camera_x, camera_y, camera_z, ambient_intensity;
        float ambient_r, ambient_g, ambient_b;
        uint32_t light_count;
    } pc{
        camera_position_.x, camera_position_.y, camera_position_.z,
        config_.global_ambient,
        config_.ambient_color.r, config_.ambient_color.g, config_.ambient_color.b,
        light_count_,
    };
    vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);

    // Fullscreen triangle: 3 vertices, no vertex buffer, derived from gl_VertexIndex.
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void VulkanLightingPass::set_ambient_light(float intensity) {
    config_.global_ambient = intensity;
}

void VulkanLightingPass::resize(uint32_t width, uint32_t height) {
    // Will be implemented when output image resizing is needed
}

void VulkanLightingPass::cleanup() {
    if (!device_) return;
    
    VkDevice vk_device = device_->get_device();
    
    if (framebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(vk_device, framebuffer_, nullptr);
        framebuffer_ = VK_NULL_HANDLE;
    }
    
    if (render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vk_device, render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }
    
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(vk_device, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    
    if (pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vk_device, pipeline_layout_, nullptr);
        pipeline_layout_ = VK_NULL_HANDLE;
    }
    
    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vk_device, descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
    }
    
    if (descriptor_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vk_device, descriptor_layout_, nullptr);
        descriptor_layout_ = VK_NULL_HANDLE;
    }
    
    if (output_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vk_device, output_view_, nullptr);
        output_view_ = VK_NULL_HANDLE;
    }
    
    if (output_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(vk_device, output_image_, nullptr);
        output_image_ = VK_NULL_HANDLE;
    }
    
    if (output_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, output_memory_, nullptr);
        output_memory_ = VK_NULL_HANDLE;
    }
    
    if (light_buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(vk_device, light_buffer_, nullptr);
        light_buffer_ = VK_NULL_HANDLE;
    }
    
    if (light_buffer_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, light_buffer_memory_, nullptr);
        light_buffer_memory_ = VK_NULL_HANDLE;
    }

    if (gbuffer_sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(vk_device, gbuffer_sampler_, nullptr);
        gbuffer_sampler_ = VK_NULL_HANDLE;
    }

    if (shadow_sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(vk_device, shadow_sampler_, nullptr);
        shadow_sampler_ = VK_NULL_HANDLE;
    }

    if (dummy_shadow_2d_array_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vk_device, dummy_shadow_2d_array_view_, nullptr);
        dummy_shadow_2d_array_view_ = VK_NULL_HANDLE;
    }
    if (dummy_shadow_2d_array_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(vk_device, dummy_shadow_2d_array_image_, nullptr);
        dummy_shadow_2d_array_image_ = VK_NULL_HANDLE;
    }
    if (dummy_shadow_2d_array_mem_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, dummy_shadow_2d_array_mem_, nullptr);
        dummy_shadow_2d_array_mem_ = VK_NULL_HANDLE;
    }
    if (dummy_shadow_cube_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vk_device, dummy_shadow_cube_view_, nullptr);
        dummy_shadow_cube_view_ = VK_NULL_HANDLE;
    }
    if (dummy_shadow_cube_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(vk_device, dummy_shadow_cube_image_, nullptr);
        dummy_shadow_cube_image_ = VK_NULL_HANDLE;
    }
    if (dummy_shadow_cube_mem_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, dummy_shadow_cube_mem_, nullptr);
        dummy_shadow_cube_mem_ = VK_NULL_HANDLE;
    }

    // Drop the registry last so its cached VkShaderModules are torn down
    // while the device is still alive.
    shader_registry_.reset();
}

} // namespace gws::renderer::gpu
