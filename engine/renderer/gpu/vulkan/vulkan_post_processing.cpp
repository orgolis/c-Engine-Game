/**
 * @file vulkan_post_processing.cpp
 * @brief Vulkan post-processing implementation
 */

#include "vulkan_post_processing.h"
#include "vulkan_device.h"
#include "vulkan_shader_registry.h"
#include "post_proc_tonemap_spirv.h" // pre-compiled SPIR-V fallback for GCC builds
#include "auto_exposure_spirv.h"
#include "post_proc_fxaa_spirv.h"
#include "post_proc_colorfx_spirv.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <array>

namespace gws::renderer::gpu {

VulkanPostProcessing::~VulkanPostProcessing() {
    cleanup();
}

std::unique_ptr<VulkanPostProcessing> VulkanPostProcessing::create(VulkanDevice* device,
                                                                   const PostProcessingConfig& config) {
    auto post_proc = std::make_unique<VulkanPostProcessing>();
    post_proc->device_ = device;
    post_proc->config_ = config;
    // Sync the colour-FX runtime enables from the config.
    post_proc->chromatic_enabled_       = config.enable_chromatic;
    post_proc->vignette_enabled_        = config.enable_vignette;
    post_proc->film_grain_enabled_      = config.enable_film_grain;
    post_proc->sharpen_enabled_         = config.enable_sharpen;
    post_proc->lens_distortion_enabled_ = config.enable_lens_distortion;
    post_proc->color_grade_enabled_     = config.enable_color_grade;
    post_proc->posterize_enabled_       = config.enable_posterize;
    post_proc->pixelate_enabled_        = config.enable_pixelate;
    post_proc->scanlines_enabled_       = config.enable_scanlines;
    
    try {
        post_proc->shader_registry_ = std::make_unique<VulkanShaderRegistry>();
        if (!post_proc->shader_registry_->initialize(device)) {
            throw std::runtime_error("Failed to initialize shader registry");
        }

        post_proc->create_output_image();

        if (config.bloom.enabled) {
            post_proc->create_bloom_resources();
        }

        if (config.taa.enabled) {
            post_proc->create_taa_resources();
        }

        post_proc->create_tonemap_render_pass();
        post_proc->create_tonemap_framebuffer();
        post_proc->create_tonemap_sampler();
        post_proc->create_descriptor_sets();
        post_proc->create_tonemap_pipeline();

        if (config.bloom.enabled && !post_proc->bloom_mips_.empty()) {
            post_proc->create_bloom_render_pass();
            post_proc->create_bloom_framebuffer();
            post_proc->create_bloom_descriptor();
            post_proc->create_bloom_pipeline();
        }

        if (config.taa.enabled && post_proc->taa_history_ != VK_NULL_HANDLE) {
            post_proc->create_taa_output_image();
            post_proc->create_taa_render_pass();
            post_proc->create_taa_framebuffer();
            post_proc->create_taa_descriptor();
            post_proc->create_taa_pipeline();
            post_proc->prime_taa_history();
        }

        spdlog::info("VulkanPostProcessing created: {}x{}", config.width, config.height);
        return post_proc;
    } catch (const std::exception& e) {
        spdlog::error("Failed to create VulkanPostProcessing: {}", e.what());
        post_proc->cleanup();
        return nullptr;
    }
}

void VulkanPostProcessing::create_output_image() {
    VkDevice vk_device = device_->get_device();
    VkPhysicalDevice physical_device = device_->get_physical_device();
    
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    image_info.extent = {config_.width, config_.height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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

void VulkanPostProcessing::create_bloom_resources() {
    VkDevice vk_device = device_->get_device();
    
    bloom_mips_.resize(config_.bloom.mip_levels);
    bloom_mip_views_.resize(config_.bloom.mip_levels);
    bloom_mip_memories_.resize(config_.bloom.mip_levels);
    
    uint32_t mip_width = config_.width;
    uint32_t mip_height = config_.height;
    
    for (uint32_t i = 0; i < config_.bloom.mip_levels; ++i) {
        mip_width = std::max(1u, mip_width / 2);
        mip_height = std::max(1u, mip_height / 2);
        
        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        image_info.extent = {mip_width, mip_height, 1};
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateImage(vk_device, &image_info, nullptr, &bloom_mips_[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create bloom mip image");
        }
        
        VkMemoryRequirements mem_reqs;
        vkGetImageMemoryRequirements(vk_device, bloom_mips_[i], &mem_reqs);
        
        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_reqs.size;
        alloc_info.memoryTypeIndex = device_->find_memory_type(
            mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        if (vkAllocateMemory(vk_device, &alloc_info, nullptr, &bloom_mip_memories_[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate bloom mip memory");
        }
        
        vkBindImageMemory(vk_device, bloom_mips_[i], bloom_mip_memories_[i], 0);
        
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = bloom_mips_[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;
        
        if (vkCreateImageView(vk_device, &view_info, nullptr, &bloom_mip_views_[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create bloom mip view");
        }
    }
    
    spdlog::debug("Bloom resources created: {} mip levels", config_.bloom.mip_levels);
}

void VulkanPostProcessing::create_taa_resources() {
    VkDevice vk_device = device_->get_device();
    
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    image_info.extent = {config_.width, config_.height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateImage(vk_device, &image_info, nullptr, &taa_history_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create TAA history image");
    }
    
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(vk_device, taa_history_, &mem_reqs);
    
    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = device_->find_memory_type(
        mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(vk_device, &alloc_info, nullptr, &taa_history_memory_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate TAA history memory");
    }
    
    vkBindImageMemory(vk_device, taa_history_, taa_history_memory_, 0);
    
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = taa_history_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(vk_device, &view_info, nullptr, &taa_history_view_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create TAA history view");
    }
}

// Synced from engine/renderer/gpu/vulkan/shaders/tonemap.vert.
// Embedded so the post-processing module can compile its pipeline at
// startup without runtime file lookups.
static const char* kTonemapVertSrc = R"GLSL(
#version 450
layout(location = 0) out vec2 outTexCoord;
void main() {
    outTexCoord = vec2((gl_VertexIndex == 0) ? 2.0 : 0.0,
                       (gl_VertexIndex == 2) ? 2.0 : 0.0);
    gl_Position = vec4(outTexCoord * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

// Tone mapping with optional additive bloom composite. Bloom is sampled
// from bloom_mips_[0] (1/2 resolution) with linear filtering for an
// implicit upsample. When bloom is disabled, binding 1 is bound to the
// input texture so the shader still has a valid sampler — the
// `bloomIntensity == 0` path at runtime makes its contribution vanish.
static const char* kTonemapFragSrc = R"GLSL(
#version 450
layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D inputTexture;
layout(set = 0, binding = 1) uniform sampler2D bloomTexture;
layout(set = 0, binding = 2) readonly buffer ExposureBuffer {
    float exposure;
    float _pad0;
    float _pad1;
    float _pad2;
} autoExposure;
layout(push_constant) uniform TonemapConstants {
    float exposure;
    float gamma;
    float contrast;
    float saturation;
    float bloomIntensity;
    float _pad0;
    float _pad1;
    float _pad2;
} pc;
vec3 ACESFilm(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
void main() {
    vec3 color = texture(inputTexture, inTexCoord).rgb;
    vec3 bloom = texture(bloomTexture, inTexCoord).rgb;
    color += bloom * pc.bloomIntensity;
    float live_exp = autoExposure.exposure;
    float exp_used = live_exp > 0.0 ? live_exp : pc.exposure;
    color *= exp_used;
    color = ACESFilm(color);
    color = mix(vec3(0.5), color, pc.contrast);
    float lum = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(lum), color, pc.saturation);
    color = pow(color, vec3(1.0 / pc.gamma));
    outColor = vec4(color, 1.0);
}
)GLSL";

// TAA "placeholder" shader — temporal smear without reprojection.
// `outColor = mix(history, current, blendFactor)`. With blendFactor close
// to 1.0 the history influence is small (sharp); close to 0.0 means
// heavy smear (test that history actually feeds in). A real TAA would
// reproject `history` using motion vectors and reject disocclusions.
static const char* kTaaFragSrc = R"GLSL(
#version 450
layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D currentTex;
layout(set = 0, binding = 1) uniform sampler2D historyTex;
layout(push_constant) uniform TaaConstants {
    float blendFactor;
    float _pad[3];
} pc;
void main() {
    vec3 current = texture(currentTex, inTexCoord).rgb;
    vec3 history = texture(historyTex, inTexCoord).rgb;
    outColor = vec4(mix(history, current, clamp(pc.blendFactor, 0.0, 1.0)), 1.0);
}
)GLSL";

// Bloom: bright-pass extract + 9-tap separable-friendly Gaussian. We do a
// single-pass 9-tap radial sample for simplicity (not the textbook
// separable horizontal+vertical that would need a ping-pong target).
// Quality is rough but the GPU pass is real and exercises the full chain.
static const char* kBloomFragSrc = R"GLSL(
#version 450
layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D inputTexture;
layout(push_constant) uniform BloomConstants {
    vec2  texelSize;     // 1 / source resolution
    float threshold;
    float intensity;
} pc;

vec3 brightPass(vec3 c) {
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float over = max(0.0, lum - pc.threshold);
    float k = over / max(lum, 1e-4);
    return c * k;
}

void main() {
    // 9-tap Gaussian weights (sigma ~ 1.5)
    const float w[5] = float[5](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 sum = brightPass(texture(inputTexture, inTexCoord).rgb) * w[0];
    for (int i = 1; i < 5; ++i) {
        vec2 off = vec2(float(i)) * pc.texelSize;
        sum += brightPass(texture(inputTexture, inTexCoord + vec2(off.x, 0.0)).rgb) * w[i] * 0.5;
        sum += brightPass(texture(inputTexture, inTexCoord - vec2(off.x, 0.0)).rgb) * w[i] * 0.5;
        sum += brightPass(texture(inputTexture, inTexCoord + vec2(0.0, off.y)).rgb) * w[i] * 0.5;
        sum += brightPass(texture(inputTexture, inTexCoord - vec2(0.0, off.y)).rgb) * w[i] * 0.5;
    }
    outColor = vec4(sum * pc.intensity, 1.0);
}
)GLSL";

void VulkanPostProcessing::create_bloom_render_pass() {
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

    VkRenderPassCreateInfo info{};
    info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments    = &color;
    info.subpassCount    = 1;
    info.pSubpasses      = &subpass;
    info.dependencyCount = static_cast<uint32_t>(deps.size());
    info.pDependencies   = deps.data();

    if (vkCreateRenderPass(vk_device, &info, nullptr, &bloom_render_pass_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create bloom render pass");
    }
}

void VulkanPostProcessing::create_bloom_framebuffer() {
    VkDevice vk_device = device_->get_device();

    // Use bloom_mips_[0] (1/2 source resolution) as the blur target.
    bloom_width_  = std::max(1u, config_.width  / 2);
    bloom_height_ = std::max(1u, config_.height / 2);

    VkFramebufferCreateInfo info{};
    info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    info.renderPass      = bloom_render_pass_;
    info.attachmentCount = 1;
    info.pAttachments    = &bloom_mip_views_[0];
    info.width           = bloom_width_;
    info.height          = bloom_height_;
    info.layers          = 1;

    if (vkCreateFramebuffer(vk_device, &info, nullptr, &bloom_framebuffer_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create bloom framebuffer");
    }
}

void VulkanPostProcessing::create_bloom_descriptor() {
    VkDevice vk_device = device_->get_device();

    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings    = &binding;
    if (vkCreateDescriptorSetLayout(vk_device, &layout_info, nullptr,
                                    &bloom_descriptor_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create bloom descriptor set layout");
    }

    VkDescriptorPoolSize pool_size{};
    pool_size.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 1;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes    = &pool_size;
    pool_info.maxSets       = 1;
    if (vkCreateDescriptorPool(vk_device, &pool_info, nullptr,
                               &bloom_descriptor_pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create bloom descriptor pool");
    }

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool     = bloom_descriptor_pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts        = &bloom_descriptor_layout_;
    if (vkAllocateDescriptorSets(vk_device, &alloc_info, &bloom_descriptor_set_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate bloom descriptor set");
    }
}

void VulkanPostProcessing::update_bloom_descriptor() {
    if (bloom_descriptor_set_ == VK_NULL_HANDLE || input_view_ == VK_NULL_HANDLE ||
        input_sampler_ == VK_NULL_HANDLE) {
        return;
    }

    VkDescriptorImageInfo image_info{};
    image_info.sampler     = input_sampler_;
    image_info.imageView   = input_view_;
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = bloom_descriptor_set_;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &image_info;
    vkUpdateDescriptorSets(device_->get_device(), 1, &write, 0, nullptr);
}

void VulkanPostProcessing::create_bloom_pipeline() {
    VkDevice vk_device = device_->get_device();

    auto vert = shader_registry_->compile_glsl(kTonemapVertSrc, ShaderStage::Vertex,
                                               "post_proc_fullscreen.vert");
    if (!vert) {
        vert = shader_registry_->create_from_spirv(kTonemapVertSpv, kTonemapVertSpv_size,
                                                   ShaderStage::Vertex, "post_proc_fullscreen.vert");
    }
    auto frag = shader_registry_->compile_glsl(kBloomFragSrc, ShaderStage::Fragment,
                                               "post_proc_bloom.frag");
    if (!frag) {
        frag = shader_registry_->create_from_spirv(kBloomFragSpv, kBloomFragSpv_size,
                                                   ShaderStage::Fragment, "post_proc_bloom.frag");
    }
    if (!vert || !frag) {
        throw std::runtime_error("Failed to compile bloom shaders");
    }

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push_range.offset     = 0;
    push_range.size       = sizeof(float) * 4; // texelSize.xy + threshold + intensity

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount         = 1;
    layout_info.pSetLayouts            = &bloom_descriptor_layout_;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges    = &push_range;
    if (vkCreatePipelineLayout(vk_device, &layout_info, nullptr,
                               &bloom_pipeline_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create bloom pipeline layout");
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
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
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
    info.layout              = bloom_pipeline_layout_;
    info.renderPass          = bloom_render_pass_;
    info.subpass             = 0;
    if (vkCreateGraphicsPipelines(vk_device, VK_NULL_HANDLE, 1, &info, nullptr,
                                  &bloom_pipeline_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create bloom pipeline");
    }
}

void VulkanPostProcessing::create_taa_output_image() {
    VkDevice vk_device = device_->get_device();

    VkImageCreateInfo image_info{};
    image_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType     = VK_IMAGE_TYPE_2D;
    image_info.format        = VK_FORMAT_R16G16B16A16_SFLOAT;
    image_info.extent        = {config_.width, config_.height, 1};
    image_info.mipLevels     = 1;
    image_info.arrayLayers   = 1;
    image_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(vk_device, &image_info, nullptr, &taa_output_image_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create TAA output image");
    }

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(vk_device, taa_output_image_, &req);
    VkMemoryAllocateInfo alloc{};
    alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize  = req.size;
    alloc.memoryTypeIndex = device_->find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vk_device, &alloc, nullptr, &taa_output_memory_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate TAA output memory");
    }
    vkBindImageMemory(vk_device, taa_output_image_, taa_output_memory_, 0);

    VkImageViewCreateInfo view_info{};
    view_info.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image    = taa_output_image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format   = VK_FORMAT_R16G16B16A16_SFLOAT;
    view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;
    if (vkCreateImageView(vk_device, &view_info, nullptr, &taa_output_view_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create TAA output view");
    }
}

void VulkanPostProcessing::create_taa_render_pass() {
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

    VkRenderPassCreateInfo info{};
    info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments    = &color;
    info.subpassCount    = 1;
    info.pSubpasses      = &subpass;
    info.dependencyCount = static_cast<uint32_t>(deps.size());
    info.pDependencies   = deps.data();

    if (vkCreateRenderPass(vk_device, &info, nullptr, &taa_render_pass_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create TAA render pass");
    }
}

void VulkanPostProcessing::create_taa_framebuffer() {
    VkDevice vk_device = device_->get_device();

    VkFramebufferCreateInfo info{};
    info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    info.renderPass      = taa_render_pass_;
    info.attachmentCount = 1;
    info.pAttachments    = &taa_output_view_;
    info.width           = config_.width;
    info.height          = config_.height;
    info.layers          = 1;

    if (vkCreateFramebuffer(vk_device, &info, nullptr, &taa_framebuffer_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create TAA framebuffer");
    }
}

void VulkanPostProcessing::create_taa_descriptor() {
    VkDevice vk_device = device_->get_device();

    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    for (uint32_t i = 0; i < bindings.size(); ++i) {
        bindings[i].binding         = i;
        bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
    layout_info.pBindings    = bindings.data();
    if (vkCreateDescriptorSetLayout(vk_device, &layout_info, nullptr, &taa_descriptor_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create TAA descriptor set layout");
    }

    VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes    = &pool_size;
    pool_info.maxSets       = 1;
    if (vkCreateDescriptorPool(vk_device, &pool_info, nullptr, &taa_descriptor_pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create TAA descriptor pool");
    }

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool     = taa_descriptor_pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts        = &taa_descriptor_layout_;
    if (vkAllocateDescriptorSets(vk_device, &alloc_info, &taa_descriptor_set_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate TAA descriptor set");
    }
}

void VulkanPostProcessing::update_taa_descriptor() {
    if (taa_descriptor_set_ == VK_NULL_HANDLE || input_view_ == VK_NULL_HANDLE ||
        taa_history_view_ == VK_NULL_HANDLE || input_sampler_ == VK_NULL_HANDLE) {
        return;
    }

    std::array<VkDescriptorImageInfo, 2> image_infos{};
    image_infos[0].sampler     = input_sampler_;
    image_infos[0].imageView   = input_view_;        // current frame
    image_infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_infos[1].sampler     = input_sampler_;
    image_infos[1].imageView   = taa_history_view_;  // last frame (zeros first time)
    image_infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 2> writes{};
    for (uint32_t i = 0; i < writes.size(); ++i) {
        writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet          = taa_descriptor_set_;
        writes[i].dstBinding      = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo      = &image_infos[i];
    }
    vkUpdateDescriptorSets(device_->get_device(),
                           static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void VulkanPostProcessing::create_taa_pipeline() {
    VkDevice vk_device = device_->get_device();

    auto vert = shader_registry_->compile_glsl(kTonemapVertSrc, ShaderStage::Vertex,
                                               "post_proc_fullscreen.vert");
    if (!vert) {
        vert = shader_registry_->create_from_spirv(kTonemapVertSpv, kTonemapVertSpv_size,
                                                   ShaderStage::Vertex, "post_proc_fullscreen.vert");
    }
    auto frag = shader_registry_->compile_glsl(kTaaFragSrc, ShaderStage::Fragment,
                                               "post_proc_taa.frag");
    if (!frag) {
        frag = shader_registry_->create_from_spirv(kTaaFragSpv, kTaaFragSpv_size,
                                                   ShaderStage::Fragment, "post_proc_taa.frag");
    }
    if (!vert || !frag) {
        throw std::runtime_error("Failed to compile TAA shaders");
    }

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push_range.offset     = 0;
    push_range.size       = sizeof(float) * 4;

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount         = 1;
    layout_info.pSetLayouts            = &taa_descriptor_layout_;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges    = &push_range;
    if (vkCreatePipelineLayout(vk_device, &layout_info, nullptr, &taa_pipeline_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create TAA pipeline layout");
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert->handle;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag->handle;
    stages[1].pName = "main";

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
    info.layout              = taa_pipeline_layout_;
    info.renderPass          = taa_render_pass_;
    info.subpass             = 0;
    if (vkCreateGraphicsPipelines(vk_device, VK_NULL_HANDLE, 1, &info, nullptr,
                                  &taa_pipeline_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create TAA pipeline");
    }
}

void VulkanPostProcessing::prime_taa_history() {
    // The taa_history_ image is created with usage COLOR_ATTACHMENT|SAMPLED
    // but starts in UNDEFINED layout. Transition it to SHADER_READ_ONLY so
    // the first frame's TAA pass can sample it (data is undefined but
    // layout is valid; a real TAA would clear it explicitly here).
    if (taa_history_ == VK_NULL_HANDLE) return;

    VkCommandBufferAllocateInfo alloc{};
    alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool        = device_->get_command_pool();
    alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device_->get_device(), &alloc, &cmd) != VK_SUCCESS) return;

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    VkImageMemoryBarrier b{};
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = taa_history_;
    b.srcAccessMask       = 0;
    b.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    b.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel   = 0;
    b.subresourceRange.levelCount     = 1;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.layerCount     = 1;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    vkQueueSubmit(device_->get_graphics_queue(), 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(device_->get_graphics_queue());
    vkFreeCommandBuffers(device_->get_device(), device_->get_command_pool(), 1, &cmd);
}

void VulkanPostProcessing::create_tonemap_render_pass() {
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

    VkRenderPassCreateInfo info{};
    info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments    = &color;
    info.subpassCount    = 1;
    info.pSubpasses      = &subpass;
    info.dependencyCount = static_cast<uint32_t>(deps.size());
    info.pDependencies   = deps.data();

    if (vkCreateRenderPass(vk_device, &info, nullptr, &render_pass_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create tone mapping render pass");
    }
}

void VulkanPostProcessing::create_tonemap_framebuffer() {
    VkDevice vk_device = device_->get_device();

    VkFramebufferCreateInfo info{};
    info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    info.renderPass      = render_pass_;
    info.attachmentCount = 1;
    info.pAttachments    = &output_view_;
    info.width           = config_.width;
    info.height          = config_.height;
    info.layers          = 1;

    if (vkCreateFramebuffer(vk_device, &info, nullptr, &framebuffer_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create tone mapping framebuffer");
    }
}

void VulkanPostProcessing::create_tonemap_sampler() {
    VkDevice vk_device = device_->get_device();

    VkSamplerCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter    = VK_FILTER_LINEAR;
    info.minFilter    = VK_FILTER_LINEAR;
    info.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

    if (vkCreateSampler(vk_device, &info, nullptr, &input_sampler_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create tone mapping sampler");
    }
}

void VulkanPostProcessing::create_descriptor_sets() {
    VkDevice vk_device = device_->get_device();

    // Bindings: 0 = HDR input sampler, 1 = bloom input sampler,
    //           2 = auto-exposure storage buffer (read by tonemap shader).
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[2].binding         = 2;
    bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
    layout_info.pBindings    = bindings.data();

    if (vkCreateDescriptorSetLayout(vk_device, &layout_info, nullptr, &descriptor_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create tone mapping descriptor set layout");
    }

    std::array<VkDescriptorPoolSize, 2> pool_sizes{};
    pool_sizes[0].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[0].descriptorCount = 2;
    pool_sizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_sizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes    = pool_sizes.data();
    pool_info.maxSets       = 1;

    if (vkCreateDescriptorPool(vk_device, &pool_info, nullptr, &descriptor_pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create tone mapping descriptor pool");
    }

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool     = descriptor_pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts        = &descriptor_layout_;

    descriptor_sets_.resize(1);
    if (vkAllocateDescriptorSets(vk_device, &alloc_info, &descriptor_sets_[0]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate tone mapping descriptor set");
    }
}

void VulkanPostProcessing::create_tonemap_pipeline() {
    VkDevice vk_device = device_->get_device();

    auto vert = shader_registry_->compile_glsl(kTonemapVertSrc, ShaderStage::Vertex,
                                               "post_proc_tonemap.vert");
    if (!vert) {
        vert = shader_registry_->create_from_spirv(kTonemapVertSpv, kTonemapVertSpv_size,
                                                   ShaderStage::Vertex, "post_proc_tonemap.vert");
    }
    auto frag = shader_registry_->compile_glsl(kTonemapFragSrc, ShaderStage::Fragment,
                                               "post_proc_tonemap.frag");
    if (!frag) {
        frag = shader_registry_->create_from_spirv(kTonemapFragSpv, kTonemapFragSpv_size,
                                                   ShaderStage::Fragment, "post_proc_tonemap.frag");
    }
    if (!vert || !frag) {
        throw std::runtime_error("Failed to compile tone mapping shaders");
    }

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push_range.offset     = 0;
    push_range.size       = sizeof(float) * 8; // exposure, gamma, contrast, saturation, bloomIntensity, 3 pad

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount         = 1;
    layout_info.pSetLayouts            = &descriptor_layout_;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges    = &push_range;

    pipeline_layouts_.resize(1);
    if (vkCreatePipelineLayout(vk_device, &layout_info, nullptr, &pipeline_layouts_[0]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create tone mapping pipeline layout");
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

    // Dynamic viewport/scissor so the same pipeline works at any resolution.
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
    info.layout              = pipeline_layouts_[0];
    info.renderPass          = render_pass_;
    info.subpass             = 0;

    pipelines_.resize(1);
    if (vkCreateGraphicsPipelines(vk_device, VK_NULL_HANDLE, 1, &info, nullptr,
                                  &pipelines_[0]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create tone mapping pipeline");
    }
}

void VulkanPostProcessing::create_pipelines() {
    // Per-effect pipelines are built individually (create_tonemap_pipeline, etc.).
    // This stub remains so existing callers keep working until the rest of the
    // post-processing chain (bloom, TAA) is migrated off the placeholders.
}

void VulkanPostProcessing::update_input_descriptor() {
    if (descriptor_sets_.empty() || input_view_ == VK_NULL_HANDLE ||
        input_sampler_ == VK_NULL_HANDLE) {
        return;
    }

    // Binding 0: HDR input. Binding 1: bloom result (fallback to input
    // when bloom is disabled, so the shader sampler is always valid).
    VkImageView bloom_view = (!bloom_mip_views_.empty() && bloom_mip_views_[0] != VK_NULL_HANDLE)
                                 ? bloom_mip_views_[0]
                                 : input_view_;

    std::array<VkDescriptorImageInfo, 2> image_infos{};
    image_infos[0].sampler     = input_sampler_;
    image_infos[0].imageView   = input_view_;
    image_infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_infos[1].sampler     = input_sampler_;
    image_infos[1].imageView   = bloom_view;
    image_infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Binding 2: auto-exposure storage buffer. Until the buffer is
    // lazy-created by apply_auto_exposure, skip the write — the static
    // exposure path in the shader (when auto-exposure is disabled) uses
    // the push-constant fallback so the shader still runs cleanly.
    const uint32_t write_count = (auto_exposure_buffer_ != VK_NULL_HANDLE) ? 3u : 2u;
    VkDescriptorBufferInfo expo_info{};
    expo_info.buffer = auto_exposure_buffer_;
    expo_info.offset = 0;
    expo_info.range  = VK_WHOLE_SIZE;

    std::array<VkWriteDescriptorSet, 3> writes{};
    for (uint32_t i = 0; i < 2; ++i) {
        writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet          = descriptor_sets_[0];
        writes[i].dstBinding      = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo      = &image_infos[i];
    }
    if (write_count == 3) {
        writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet          = descriptor_sets_[0];
        writes[2].dstBinding      = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].pBufferInfo     = &expo_info;
    }

    vkUpdateDescriptorSets(device_->get_device(),
                           write_count, writes.data(),
                           0, nullptr);
}

void VulkanPostProcessing::set_input_image(VkImageView image_view) {
    input_view_ = image_view;
    // Lazy-init auto-exposure here so the storage buffer exists before
    // update_input_descriptor writes binding 2 on the tonemap descriptor set.
    init_auto_exposure_resources();
    update_input_descriptor();
    update_bloom_descriptor();
    update_taa_descriptor();
}

void VulkanPostProcessing::apply_bloom(VkCommandBuffer cmd) {
    if (!bloom_enabled_) return;
    if (bloom_pipeline_ == VK_NULL_HANDLE || bloom_render_pass_ == VK_NULL_HANDLE ||
        bloom_framebuffer_ == VK_NULL_HANDLE) {
        return;
    }
    if (input_view_ == VK_NULL_HANDLE) {
        return;
    }

    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderPassBeginInfo rp_begin{};
    rp_begin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass        = bloom_render_pass_;
    rp_begin.framebuffer       = bloom_framebuffer_;
    rp_begin.renderArea.offset = {0, 0};
    rp_begin.renderArea.extent = {bloom_width_, bloom_height_};
    rp_begin.clearValueCount   = 1;
    rp_begin.pClearValues      = &clear;
    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.width    = static_cast<float>(bloom_width_);
    vp.height   = static_cast<float>(bloom_height_);
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor{};
    scissor.extent = {bloom_width_, bloom_height_};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bloom_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bloom_pipeline_layout_,
                            0, 1, &bloom_descriptor_set_, 0, nullptr);

    struct {
        float texel_x;
        float texel_y;
        float threshold;
        float intensity;
    } pc{
        // Sample from full-res input — texels are 1/source_resolution.
        1.0f / static_cast<float>(config_.width),
        1.0f / static_cast<float>(config_.height),
        config_.bloom.threshold,
        config_.bloom.intensity,
    };
    vkCmdPushConstants(cmd, bloom_pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);

    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

void VulkanPostProcessing::apply_tone_mapping(VkCommandBuffer cmd) {
    if (!tone_mapping_enabled_) return;
    if (pipelines_.empty() || pipelines_[0] == VK_NULL_HANDLE ||
        render_pass_ == VK_NULL_HANDLE || framebuffer_ == VK_NULL_HANDLE) {
        spdlog::warn("Tone mapping invoked before pipeline setup");
        return;
    }
    if (input_view_ == VK_NULL_HANDLE) {
        spdlog::warn("Tone mapping invoked with no input image bound");
        return;
    }

    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderPassBeginInfo rp_begin{};
    rp_begin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass        = render_pass_;
    rp_begin.framebuffer       = framebuffer_;
    rp_begin.renderArea.offset = {0, 0};
    rp_begin.renderArea.extent = {config_.width, config_.height};
    rp_begin.clearValueCount   = 1;
    rp_begin.pClearValues      = &clear;

    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.x        = 0.0f;
    vp.y        = 0.0f;
    vp.width    = static_cast<float>(config_.width);
    vp.height   = static_cast<float>(config_.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {config_.width, config_.height};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_[0]);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_layouts_[0], 0,
                            1, &descriptor_sets_[0],
                            0, nullptr);

    struct {
        float exposure;
        float gamma;
        float contrast;
        float saturation;
        float bloom_intensity;
        float _pad[3];
    } pc{
        config_.tone_mapping.exposure,
        config_.tone_mapping.gamma,
        config_.tone_mapping.contrast,
        config_.tone_mapping.saturation,
        (bloom_enabled_ && bloom_pipeline_ != VK_NULL_HANDLE) ? config_.bloom.intensity : 0.0f,
        {0.0f, 0.0f, 0.0f},
    };
    vkCmdPushConstants(cmd, pipeline_layouts_[0], VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);

    // Fullscreen triangle: 3 vertices, no vertex buffer, derived from gl_VertexIndex.
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);
}

void VulkanPostProcessing::apply_taa(VkCommandBuffer cmd) {
    if (!taa_enabled_) return;
    if (taa_pipeline_ == VK_NULL_HANDLE || taa_render_pass_ == VK_NULL_HANDLE ||
        taa_framebuffer_ == VK_NULL_HANDLE || input_view_ == VK_NULL_HANDLE) {
        return;
    }

    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderPassBeginInfo rp_begin{};
    rp_begin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass        = taa_render_pass_;
    rp_begin.framebuffer       = taa_framebuffer_;
    rp_begin.renderArea.offset = {0, 0};
    rp_begin.renderArea.extent = {config_.width, config_.height};
    rp_begin.clearValueCount   = 1;
    rp_begin.pClearValues      = &clear;
    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.width    = static_cast<float>(config_.width);
    vp.height   = static_cast<float>(config_.height);
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor{};
    scissor.extent = {config_.width, config_.height};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, taa_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, taa_pipeline_layout_,
                            0, 1, &taa_descriptor_set_, 0, nullptr);

    struct {
        float blend_factor;
        float _pad[3];
    } pc{config_.taa.blend_factor, {0.0f, 0.0f, 0.0f}};
    vkCmdPushConstants(cmd, taa_pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);

    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

void VulkanPostProcessing::apply_chromatic(VkCommandBuffer cmd) {
    if (!chromatic_enabled_) return;
    spdlog::debug("Applying chromatic aberration");
}

void VulkanPostProcessing::apply_vignette(VkCommandBuffer cmd) {
    if (!vignette_enabled_) return;
    spdlog::debug("Applying vignette");
}

void VulkanPostProcessing::apply_film_grain(VkCommandBuffer cmd) {
    if (!film_grain_enabled_) return;
    spdlog::debug("Applying film grain");
}

void VulkanPostProcessing::render(VkCommandBuffer cmd) {
    if (bloom_enabled_) apply_bloom(cmd);
    if (taa_enabled_) apply_taa(cmd);
    // Auto-exposure must run BEFORE tone mapping — the latter reads the
    // smoothed exposure value this compute pass updates.
    if (auto_exposure_enabled_) apply_auto_exposure(cmd, delta_time_);
    if (tone_mapping_enabled_) apply_tone_mapping(cmd);
    // FXAA runs on the LDR tone-mapped result. Edge detection on a
    // gamma-correct LDR image is what FXAA was designed for; running it
    // earlier (on HDR linear) makes the edge thresholds meaningless.
    if (fxaa_enabled_) apply_fxaa(cmd);
    // Combined colour FX (chromatic aberration + vignette + film grain) —
    // runs last, gated internally so it no-ops when all three are off.
    apply_color_fx(cmd);
}

void VulkanPostProcessing::set_effect_enabled(PostProcessEffect effect, bool enabled) {
    switch (effect) {
        case PostProcessEffect::Bloom:
            bloom_enabled_ = enabled;
            break;
        case PostProcessEffect::ToneMapping:
            tone_mapping_enabled_ = enabled;
            break;
        case PostProcessEffect::TAA:
            taa_enabled_ = enabled;
            break;
        case PostProcessEffect::Chromatic:
            chromatic_enabled_ = enabled;
            break;
        case PostProcessEffect::Vignette:
            vignette_enabled_ = enabled;
            break;
        case PostProcessEffect::FilmGrain:
            film_grain_enabled_ = enabled;
            break;
        default:
            break;
    }
}

bool VulkanPostProcessing::is_effect_enabled(PostProcessEffect effect) const {
    switch (effect) {
        case PostProcessEffect::Bloom:
            return bloom_enabled_;
        case PostProcessEffect::ToneMapping:
            return tone_mapping_enabled_;
        case PostProcessEffect::TAA:
            return taa_enabled_;
        case PostProcessEffect::Chromatic:
            return chromatic_enabled_;
        case PostProcessEffect::Vignette:
            return vignette_enabled_;
        case PostProcessEffect::FilmGrain:
            return film_grain_enabled_;
        default:
            return false;
    }
}

void VulkanPostProcessing::update_config(const PostProcessingConfig& config) {
    config_ = config;
}

void VulkanPostProcessing::resize(uint32_t width, uint32_t height) {
    if (width == config_.width && height == config_.height) {
        return;
    }
    
    cleanup();
    config_.width = width;
    config_.height = height;
    
    auto new_pp = create(device_, config_);
    if (new_pp) {
        *this = std::move(*new_pp);
    }
}

void VulkanPostProcessing::cleanup() {
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
    
    for (auto pipeline : pipelines_) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(vk_device, pipeline, nullptr);
        }
    }
    pipelines_.clear();
    
    for (auto layout : pipeline_layouts_) {
        if (layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(vk_device, layout, nullptr);
        }
    }
    pipeline_layouts_.clear();
    
    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vk_device, descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
    }
    
    if (descriptor_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vk_device, descriptor_layout_, nullptr);
        descriptor_layout_ = VK_NULL_HANDLE;
    }
    
    // Cleanup bloom resources
    for (auto view : bloom_mip_views_) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(vk_device, view, nullptr);
        }
    }
    bloom_mip_views_.clear();
    
    for (auto image : bloom_mips_) {
        if (image != VK_NULL_HANDLE) {
            vkDestroyImage(vk_device, image, nullptr);
        }
    }
    bloom_mips_.clear();
    
    for (auto memory : bloom_mip_memories_) {
        if (memory != VK_NULL_HANDLE) {
            vkFreeMemory(vk_device, memory, nullptr);
        }
    }
    bloom_mip_memories_.clear();
    
    if (bloom_output_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vk_device, bloom_output_view_, nullptr);
        bloom_output_view_ = VK_NULL_HANDLE;
    }
    
    if (bloom_output_ != VK_NULL_HANDLE) {
        vkDestroyImage(vk_device, bloom_output_, nullptr);
        bloom_output_ = VK_NULL_HANDLE;
    }
    
    if (bloom_output_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, bloom_output_memory_, nullptr);
        bloom_output_memory_ = VK_NULL_HANDLE;
    }
    
    // Cleanup TAA resources
    if (taa_history_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vk_device, taa_history_view_, nullptr);
        taa_history_view_ = VK_NULL_HANDLE;
    }
    
    if (taa_history_ != VK_NULL_HANDLE) {
        vkDestroyImage(vk_device, taa_history_, nullptr);
        taa_history_ = VK_NULL_HANDLE;
    }
    
    if (taa_history_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, taa_history_memory_, nullptr);
        taa_history_memory_ = VK_NULL_HANDLE;
    }
    
    // Cleanup output resources
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

    if (input_sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(vk_device, input_sampler_, nullptr);
        input_sampler_ = VK_NULL_HANDLE;
    }

    if (bloom_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(vk_device, bloom_pipeline_, nullptr);
        bloom_pipeline_ = VK_NULL_HANDLE;
    }
    if (bloom_pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vk_device, bloom_pipeline_layout_, nullptr);
        bloom_pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (bloom_descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vk_device, bloom_descriptor_pool_, nullptr);
        bloom_descriptor_pool_ = VK_NULL_HANDLE;
        bloom_descriptor_set_  = VK_NULL_HANDLE;
    }
    if (bloom_descriptor_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vk_device, bloom_descriptor_layout_, nullptr);
        bloom_descriptor_layout_ = VK_NULL_HANDLE;
    }
    if (bloom_framebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(vk_device, bloom_framebuffer_, nullptr);
        bloom_framebuffer_ = VK_NULL_HANDLE;
    }
    if (bloom_render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vk_device, bloom_render_pass_, nullptr);
        bloom_render_pass_ = VK_NULL_HANDLE;
    }

    if (taa_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(vk_device, taa_pipeline_, nullptr);
        taa_pipeline_ = VK_NULL_HANDLE;
    }
    if (taa_pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vk_device, taa_pipeline_layout_, nullptr);
        taa_pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (taa_descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vk_device, taa_descriptor_pool_, nullptr);
        taa_descriptor_pool_ = VK_NULL_HANDLE;
        taa_descriptor_set_  = VK_NULL_HANDLE;
    }
    if (taa_descriptor_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vk_device, taa_descriptor_layout_, nullptr);
        taa_descriptor_layout_ = VK_NULL_HANDLE;
    }
    if (taa_framebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(vk_device, taa_framebuffer_, nullptr);
        taa_framebuffer_ = VK_NULL_HANDLE;
    }
    if (taa_render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vk_device, taa_render_pass_, nullptr);
        taa_render_pass_ = VK_NULL_HANDLE;
    }
    if (taa_output_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vk_device, taa_output_view_, nullptr);
        taa_output_view_ = VK_NULL_HANDLE;
    }
    if (taa_output_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(vk_device, taa_output_image_, nullptr);
        taa_output_image_ = VK_NULL_HANDLE;
    }
    if (taa_output_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, taa_output_memory_, nullptr);
        taa_output_memory_ = VK_NULL_HANDLE;
    }

    // Auto-exposure resources.
    if (auto_exposure_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(vk_device, auto_exposure_pipeline_, nullptr);
        auto_exposure_pipeline_ = VK_NULL_HANDLE;
    }
    if (auto_exposure_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vk_device, auto_exposure_layout_, nullptr);
        auto_exposure_layout_ = VK_NULL_HANDLE;
    }
    if (auto_exposure_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vk_device, auto_exposure_pool_, nullptr);
        auto_exposure_pool_ = VK_NULL_HANDLE;
        auto_exposure_set_  = VK_NULL_HANDLE;
    }
    if (auto_exposure_dsl_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vk_device, auto_exposure_dsl_, nullptr);
        auto_exposure_dsl_ = VK_NULL_HANDLE;
    }
    if (auto_exposure_sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(vk_device, auto_exposure_sampler_, nullptr);
        auto_exposure_sampler_ = VK_NULL_HANDLE;
    }
    if (auto_exposure_buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(vk_device, auto_exposure_buffer_, nullptr);
        auto_exposure_buffer_ = VK_NULL_HANDLE;
    }
    if (auto_exposure_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, auto_exposure_memory_, nullptr);
        auto_exposure_memory_ = VK_NULL_HANDLE;
    }
    auto_exposure_initialized_ = false;

    // FXAA resources.
    if (fxaa_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(vk_device, fxaa_pipeline_, nullptr);
        fxaa_pipeline_ = VK_NULL_HANDLE;
    }
    if (fxaa_pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vk_device, fxaa_pipeline_layout_, nullptr);
        fxaa_pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (fxaa_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vk_device, fxaa_pool_, nullptr);
        fxaa_pool_ = VK_NULL_HANDLE;
        fxaa_set_  = VK_NULL_HANDLE;
    }
    if (fxaa_dsl_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vk_device, fxaa_dsl_, nullptr);
        fxaa_dsl_ = VK_NULL_HANDLE;
    }
    if (fxaa_framebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(vk_device, fxaa_framebuffer_, nullptr);
        fxaa_framebuffer_ = VK_NULL_HANDLE;
    }
    if (fxaa_render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vk_device, fxaa_render_pass_, nullptr);
        fxaa_render_pass_ = VK_NULL_HANDLE;
    }
    if (fxaa_sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(vk_device, fxaa_sampler_, nullptr);
        fxaa_sampler_ = VK_NULL_HANDLE;
    }
    if (fxaa_intermediate_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vk_device, fxaa_intermediate_view_, nullptr);
        fxaa_intermediate_view_ = VK_NULL_HANDLE;
    }
    if (fxaa_intermediate_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(vk_device, fxaa_intermediate_image_, nullptr);
        fxaa_intermediate_image_ = VK_NULL_HANDLE;
    }
    if (fxaa_intermediate_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, fxaa_intermediate_memory_, nullptr);
        fxaa_intermediate_memory_ = VK_NULL_HANDLE;
    }
    fxaa_initialized_ = false;

    // Colour-FX resources.
    if (colorfx_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(vk_device, colorfx_pipeline_, nullptr); colorfx_pipeline_ = VK_NULL_HANDLE; }
    if (colorfx_pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(vk_device, colorfx_pipeline_layout_, nullptr); colorfx_pipeline_layout_ = VK_NULL_HANDLE; }
    if (colorfx_pool_ != VK_NULL_HANDLE) { vkDestroyDescriptorPool(vk_device, colorfx_pool_, nullptr); colorfx_pool_ = VK_NULL_HANDLE; colorfx_set_ = VK_NULL_HANDLE; }
    if (colorfx_dsl_ != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(vk_device, colorfx_dsl_, nullptr); colorfx_dsl_ = VK_NULL_HANDLE; }
    if (colorfx_framebuffer_ != VK_NULL_HANDLE) { vkDestroyFramebuffer(vk_device, colorfx_framebuffer_, nullptr); colorfx_framebuffer_ = VK_NULL_HANDLE; }
    if (colorfx_render_pass_ != VK_NULL_HANDLE) { vkDestroyRenderPass(vk_device, colorfx_render_pass_, nullptr); colorfx_render_pass_ = VK_NULL_HANDLE; }
    if (colorfx_sampler_ != VK_NULL_HANDLE) { vkDestroySampler(vk_device, colorfx_sampler_, nullptr); colorfx_sampler_ = VK_NULL_HANDLE; }
    if (colorfx_intermediate_view_ != VK_NULL_HANDLE) { vkDestroyImageView(vk_device, colorfx_intermediate_view_, nullptr); colorfx_intermediate_view_ = VK_NULL_HANDLE; }
    if (colorfx_intermediate_image_ != VK_NULL_HANDLE) { vkDestroyImage(vk_device, colorfx_intermediate_image_, nullptr); colorfx_intermediate_image_ = VK_NULL_HANDLE; }
    if (colorfx_intermediate_memory_ != VK_NULL_HANDLE) { vkFreeMemory(vk_device, colorfx_intermediate_memory_, nullptr); colorfx_intermediate_memory_ = VK_NULL_HANDLE; }
    colorfx_initialized_ = false;

    // Drop the shader registry last so its cached VkShaderModules are torn
    // down while the device is still alive.
    shader_registry_.reset();
}

// ---- Auto-exposure --------------------------------------------------------

namespace {

bool create_auto_exposure_resources(VulkanDevice* device,
                                     VkBuffer& buffer, VkDeviceMemory& mem,
                                     VkSampler& sampler) {
    VkDevice vk = device->get_device();

    // 16-byte persistent buffer: float current_exposure + 12B padding.
    // Bound as STORAGE for compute writes and as UNIFORM for tonemap reads.
    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = 16;
    bi.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vk, &bi, nullptr, &buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(vk, buffer, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = device->find_memory_type(
        mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vk, &ai, nullptr, &mem) != VK_SUCCESS) return false;
    vkBindBufferMemory(vk, buffer, mem, 0);

    // Linear sampler for sampling HDR — the strided fetch in the compute
    // shader uses texelFetch so the sampler is mostly nominal, but Vulkan
    // requires one for a sampler2D binding.
    VkSamplerCreateInfo si{};
    si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter    = VK_FILTER_NEAREST;
    si.minFilter    = VK_FILTER_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    return vkCreateSampler(vk, &si, nullptr, &sampler) == VK_SUCCESS;
}

} // namespace

bool VulkanPostProcessing::init_auto_exposure_resources() {
    if (auto_exposure_initialized_) return true;
    if (input_view_ == VK_NULL_HANDLE) return false;

    VkDevice vk = device_->get_device();

    if (!create_auto_exposure_resources(device_, auto_exposure_buffer_,
                                        auto_exposure_memory_,
                                        auto_exposure_sampler_)) {
        spdlog::error("AutoExposure: resource creation failed"); return false;
    }

    // Descriptor set layout: HDR sampler + exposure SSBO + depth sampler.
    std::array<VkDescriptorSetLayoutBinding, 3> b{};
    b[0].binding = 0; b[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[0].descriptorCount = 1; b[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    b[1].binding = 1; b[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b[1].descriptorCount = 1; b[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    b[2].binding = 2; b[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[2].descriptorCount = 1; b[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 3; li.pBindings = b.data();
    if (vkCreateDescriptorSetLayout(vk, &li, nullptr, &auto_exposure_dsl_) != VK_SUCCESS) {
        spdlog::error("AutoExposure: dsl failed"); return false;
    }

    VkPushConstantRange pr{};
    pr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pr.size       = sizeof(float) * 8;
    VkPipelineLayoutCreateInfo pli{};
    pli.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount         = 1;
    pli.pSetLayouts            = &auto_exposure_dsl_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges    = &pr;
    if (vkCreatePipelineLayout(vk, &pli, nullptr, &auto_exposure_layout_) != VK_SUCCESS) {
        spdlog::error("AutoExposure: pipeline layout failed"); return false;
    }

    VkShaderModuleCreateInfo smi{};
    smi.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = kAutoExposureSpv_size;
    smi.pCode    = kAutoExposureSpv;
    VkShaderModule sm = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vk, &smi, nullptr, &sm) != VK_SUCCESS) {
        spdlog::error("AutoExposure: shader module failed"); return false;
    }
    VkComputePipelineCreateInfo cpi{};
    cpi.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpi.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.module = sm;
    cpi.stage.pName  = "main";
    cpi.layout       = auto_exposure_layout_;
    VkResult pres = vkCreateComputePipelines(vk, VK_NULL_HANDLE, 1, &cpi, nullptr,
                                             &auto_exposure_pipeline_);
    vkDestroyShaderModule(vk, sm, nullptr);
    if (pres != VK_SUCCESS) {
        spdlog::error("AutoExposure: pipeline failed"); return false;
    }

    std::array<VkDescriptorPoolSize, 2> ps{};
    ps[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps[0].descriptorCount = 2; // HDR + depth
    ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;         ps[1].descriptorCount = 1;
    VkDescriptorPoolCreateInfo pi{};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets       = 1;
    pi.poolSizeCount = static_cast<uint32_t>(ps.size());
    pi.pPoolSizes    = ps.data();
    if (vkCreateDescriptorPool(vk, &pi, nullptr, &auto_exposure_pool_) != VK_SUCCESS) {
        spdlog::error("AutoExposure: pool failed"); return false;
    }
    VkDescriptorSetAllocateInfo dai{};
    dai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool     = auto_exposure_pool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts        = &auto_exposure_dsl_;
    if (vkAllocateDescriptorSets(vk, &dai, &auto_exposure_set_) != VK_SUCCESS) {
        spdlog::error("AutoExposure: set alloc failed"); return false;
    }

    VkDescriptorImageInfo ii{};
    ii.sampler     = auto_exposure_sampler_;
    ii.imageView   = input_view_;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorBufferInfo bii{};
    bii.buffer = auto_exposure_buffer_;
    bii.offset = 0;
    bii.range  = VK_WHOLE_SIZE;
    // Depth binding. If no scene depth was provided, fall back to the HDR
    // view so the descriptor is valid — sky exclusion simply won't fire
    // (every pixel reads as non-sky), preserving the old behaviour.
    VkDescriptorImageInfo di{};
    di.sampler     = auto_exposure_sampler_;
    di.imageView   = (scene_depth_view_ != VK_NULL_HANDLE) ? scene_depth_view_ : input_view_;
    di.imageLayout = (scene_depth_view_ != VK_NULL_HANDLE)
                        ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    std::array<VkWriteDescriptorSet, 3> ws{};
    ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[0].dstSet = auto_exposure_set_;
    ws[0].dstBinding = 0; ws[0].descriptorCount = 1;
    ws[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ws[0].pImageInfo = &ii;
    ws[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[1].dstSet = auto_exposure_set_;
    ws[1].dstBinding = 1; ws[1].descriptorCount = 1;
    ws[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; ws[1].pBufferInfo = &bii;
    ws[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[2].dstSet = auto_exposure_set_;
    ws[2].dstBinding = 2; ws[2].descriptorCount = 1;
    ws[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ws[2].pImageInfo = &di;
    vkUpdateDescriptorSets(vk, 3, ws.data(), 0, nullptr);

    auto_exposure_initialized_ = true;
    auto_exposure_needs_seed_  = true;
    spdlog::info("AutoExposure initialized");
    return true;
}

void VulkanPostProcessing::apply_auto_exposure(VkCommandBuffer cmd, float delta_s) {
    if (!auto_exposure_initialized_) return;

    // First frame after init: seed the buffer with the configured static
    // exposure so the screen doesn't flash black before convergence.
    if (auto_exposure_needs_seed_) {
        const float seed = config_.tone_mapping.exposure;
        vkCmdUpdateBuffer(cmd, auto_exposure_buffer_, 0, sizeof(float), &seed);
        VkMemoryBarrier sb{};
        sb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        sb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &sb, 0, nullptr, 0, nullptr);
        auto_exposure_needs_seed_ = false;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, auto_exposure_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            auto_exposure_layout_, 0, 1, &auto_exposure_set_, 0, nullptr);
    struct {
        int32_t w, h;
        float dt;
        float adapt_rate;
        float key;
        float min_exposure;
        float max_exposure;
        float _pad;
    } pc{};
    pc.w = static_cast<int32_t>(config_.width);
    pc.h = static_cast<int32_t>(config_.height);
    pc.dt = std::max(delta_s, 1.0f / 240.0f);
    pc.adapt_rate  = 1.5f;        // ~0.7s half-life
    pc.key         = 0.18f;       // middle grey
    pc.min_exposure = 0.05f;
    pc.max_exposure = 8.0f;
    vkCmdPushConstants(cmd, auto_exposure_layout_, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(pc), &pc);
    vkCmdDispatch(cmd, 1, 1, 1);

    // Make the exposure-buffer write visible to the upcoming tonemap pass.
    VkMemoryBarrier mb{};
    mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 1, &mb, 0, nullptr, 0, nullptr);
}

// ---- FXAA -----------------------------------------------------------------

bool VulkanPostProcessing::init_fxaa_resources() {
    if (fxaa_initialized_) return true;
    if (output_image_ == VK_NULL_HANDLE || output_view_ == VK_NULL_HANDLE) return false;

    VkDevice vk = device_->get_device();

    // Intermediate image — same format as the post-process output. FXAA
    // reads this and writes back to output_image_.
    {
        VkImageCreateInfo ii{};
        ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType     = VK_IMAGE_TYPE_2D;
        ii.format        = VK_FORMAT_R16G16B16A16_SFLOAT;
        ii.extent        = { config_.width, config_.height, 1 };
        ii.mipLevels     = 1;
        ii.arrayLayers   = 1;
        ii.samples       = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ii.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(vk, &ii, nullptr, &fxaa_intermediate_image_) != VK_SUCCESS) {
            spdlog::error("FXAA: failed to create intermediate image"); return false;
        }
        VkMemoryRequirements mr{};
        vkGetImageMemoryRequirements(vk, fxaa_intermediate_image_, &mr);
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = mr.size;
        ai.memoryTypeIndex = device_->find_memory_type(
            mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(vk, &ai, nullptr, &fxaa_intermediate_memory_) != VK_SUCCESS) {
            spdlog::error("FXAA: alloc failed"); return false;
        }
        vkBindImageMemory(vk, fxaa_intermediate_image_, fxaa_intermediate_memory_, 0);

        VkImageViewCreateInfo vi{};
        vi.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image            = fxaa_intermediate_image_;
        vi.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        vi.format           = VK_FORMAT_R16G16B16A16_SFLOAT;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(vk, &vi, nullptr, &fxaa_intermediate_view_) != VK_SUCCESS) return false;
    }

    // Linear sampler for the FXAA sample taps.
    {
        VkSamplerCreateInfo si{};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = VK_FILTER_LINEAR;
        si.minFilter    = VK_FILTER_LINEAR;
        si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(vk, &si, nullptr, &fxaa_sampler_) != VK_SUCCESS) return false;
    }

    // Render pass: one colour attachment (the output_image_), LOAD_OP_LOAD
    // because we want to preserve the alpha channel / structure but the
    // shader overwrites every pixel anyway — DONT_CARE is also valid.
    {
        VkAttachmentDescription att{};
        att.format         = VK_FORMAT_R16G16B16A16_SFLOAT;
        att.samples        = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference ref{};
        ref.attachment = 0;
        ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkSubpassDescription sub{};
        sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments    = &ref;
        VkSubpassDependency dep{};
        dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass    = 0;
        dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
        dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo rpi{};
        rpi.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpi.attachmentCount = 1;
        rpi.pAttachments    = &att;
        rpi.subpassCount    = 1;
        rpi.pSubpasses      = &sub;
        rpi.dependencyCount = 1;
        rpi.pDependencies   = &dep;
        if (vkCreateRenderPass(vk, &rpi, nullptr, &fxaa_render_pass_) != VK_SUCCESS) {
            spdlog::error("FXAA: render pass failed"); return false;
        }

        VkFramebufferCreateInfo fbi{};
        fbi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbi.renderPass      = fxaa_render_pass_;
        fbi.attachmentCount = 1;
        fbi.pAttachments    = &output_view_;
        fbi.width           = config_.width;
        fbi.height          = config_.height;
        fbi.layers          = 1;
        if (vkCreateFramebuffer(vk, &fbi, nullptr, &fxaa_framebuffer_) != VK_SUCCESS) {
            spdlog::error("FXAA: framebuffer failed"); return false;
        }
    }

    // Descriptor set layout + pool + set, write the intermediate image.
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding         = 0;
        b.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 1; li.pBindings = &b;
        if (vkCreateDescriptorSetLayout(vk, &li, nullptr, &fxaa_dsl_) != VK_SUCCESS) return false;

        VkDescriptorPoolSize ps{};
        ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps.descriptorCount = 1;
        VkDescriptorPoolCreateInfo pi{};
        pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets       = 1;
        pi.poolSizeCount = 1;
        pi.pPoolSizes    = &ps;
        if (vkCreateDescriptorPool(vk, &pi, nullptr, &fxaa_pool_) != VK_SUCCESS) return false;
        VkDescriptorSetAllocateInfo dai{};
        dai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dai.descriptorPool     = fxaa_pool_;
        dai.descriptorSetCount = 1;
        dai.pSetLayouts        = &fxaa_dsl_;
        if (vkAllocateDescriptorSets(vk, &dai, &fxaa_set_) != VK_SUCCESS) return false;

        VkDescriptorImageInfo ii{};
        ii.sampler     = fxaa_sampler_;
        ii.imageView   = fxaa_intermediate_view_;
        ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet w{};
        w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet          = fxaa_set_;
        w.dstBinding      = 0;
        w.descriptorCount = 1;
        w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo      = &ii;
        vkUpdateDescriptorSets(vk, 1, &w, 0, nullptr);
    }

    // Graphics pipeline. Reuses the post-process tonemap vertex shader
    // (fullscreen triangle).
    {
        VkPushConstantRange pr{};
        pr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pr.size       = sizeof(float) * 4; // vec2 rcpFrame + 2 floats pad
        VkPipelineLayoutCreateInfo pli{};
        pli.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount         = 1;
        pli.pSetLayouts            = &fxaa_dsl_;
        pli.pushConstantRangeCount = 1;
        pli.pPushConstantRanges    = &pr;
        if (vkCreatePipelineLayout(vk, &pli, nullptr, &fxaa_pipeline_layout_) != VK_SUCCESS) return false;

        auto vert = shader_registry_->create_from_spirv(kTonemapVertSpv, kTonemapVertSpv_size,
                                                        ShaderStage::Vertex, "fxaa.vert");
        auto frag = shader_registry_->create_from_spirv(kFxaaFragSpv, kFxaaFragSpv_size,
                                                        ShaderStage::Fragment, "fxaa.frag");
        if (!vert || !frag) { spdlog::error("FXAA: shader load failed"); return false; }

        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert->handle; stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag->handle; stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        std::array<VkDynamicState, 2> dyns = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
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
        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = 0xF;
        cba.blendEnable    = VK_FALSE;
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
        gpi.layout              = fxaa_pipeline_layout_;
        gpi.renderPass          = fxaa_render_pass_;
        if (vkCreateGraphicsPipelines(vk, VK_NULL_HANDLE, 1, &gpi, nullptr,
                                      &fxaa_pipeline_) != VK_SUCCESS) {
            spdlog::error("FXAA: pipeline failed"); return false;
        }
    }

    fxaa_initialized_ = true;
    spdlog::info("FXAA initialized");
    return true;
}

void VulkanPostProcessing::apply_fxaa(VkCommandBuffer cmd) {
    if (output_image_ == VK_NULL_HANDLE) return;
    if (!fxaa_initialized_ && !init_fxaa_resources()) return;

    // 1. Transition output_image_ TRANSFER_SRC and intermediate TRANSFER_DST,
    //    then blit. After the blit, intermediate is in TRANSFER_DST and
    //    output_image_ is in TRANSFER_SRC.
    auto barrier = [&](VkImage img, VkImageLayout from, VkImageLayout to,
                        VkAccessFlags srcA, VkAccessFlags dstA,
                        VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
        VkImageMemoryBarrier b{};
        b.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout                   = from;
        b.newLayout                   = to;
        b.image                       = img;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.layerCount = 1;
        b.srcAccessMask = srcA;
        b.dstAccessMask = dstA;
        vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
    };

    barrier(output_image_,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
    barrier(fxaa_intermediate_image_,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageCopy region{};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1;
    region.dstSubresource = region.srcSubresource;
    region.extent = { config_.width, config_.height, 1 };
    vkCmdCopyImage(cmd, output_image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   fxaa_intermediate_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &region);

    // 2. Transition intermediate to SHADER_READ_ONLY, output to COLOR_ATTACHMENT.
    barrier(fxaa_intermediate_image_,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    barrier(output_image_,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    // 3. Run the FXAA pass. The render pass's initialLayout = UNDEFINED so
    //    Vulkan will accept whatever we transitioned to above.
    VkRenderPassBeginInfo rpi{};
    rpi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass        = fxaa_render_pass_;
    rpi.framebuffer       = fxaa_framebuffer_;
    rpi.renderArea.offset = {0, 0};
    rpi.renderArea.extent = {config_.width, config_.height};
    vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.width  = static_cast<float>(config_.width);
    vp.height = static_cast<float>(config_.height);
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{};
    sc.extent = {config_.width, config_.height};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fxaa_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            fxaa_pipeline_layout_, 0, 1, &fxaa_set_, 0, nullptr);

    float pc[4] = {
        1.0f / static_cast<float>(config_.width),
        1.0f / static_cast<float>(config_.height),
        0.0f, 0.0f,
    };
    vkCmdPushConstants(cmd, fxaa_pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

// ---- Colour FX (chromatic + vignette + grain) -----------------------------

bool VulkanPostProcessing::init_colorfx_resources() {
    if (colorfx_initialized_) return true;
    if (output_image_ == VK_NULL_HANDLE || output_view_ == VK_NULL_HANDLE) return false;

    VkDevice vk = device_->get_device();

    // Intermediate image — same shape/format as the FXAA one.
    {
        VkImageCreateInfo ii{};
        ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType     = VK_IMAGE_TYPE_2D;
        ii.format        = VK_FORMAT_R16G16B16A16_SFLOAT;
        ii.extent        = { config_.width, config_.height, 1 };
        ii.mipLevels     = 1;
        ii.arrayLayers   = 1;
        ii.samples       = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ii.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(vk, &ii, nullptr, &colorfx_intermediate_image_) != VK_SUCCESS) return false;
        VkMemoryRequirements mr{};
        vkGetImageMemoryRequirements(vk, colorfx_intermediate_image_, &mr);
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = mr.size;
        ai.memoryTypeIndex = device_->find_memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(vk, &ai, nullptr, &colorfx_intermediate_memory_) != VK_SUCCESS) return false;
        vkBindImageMemory(vk, colorfx_intermediate_image_, colorfx_intermediate_memory_, 0);
        VkImageViewCreateInfo vi{};
        vi.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image            = colorfx_intermediate_image_;
        vi.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        vi.format           = VK_FORMAT_R16G16B16A16_SFLOAT;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(vk, &vi, nullptr, &colorfx_intermediate_view_) != VK_SUCCESS) return false;
    }
    {
        VkSamplerCreateInfo si{};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = VK_FILTER_LINEAR;
        si.minFilter    = VK_FILTER_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(vk, &si, nullptr, &colorfx_sampler_) != VK_SUCCESS) return false;
    }
    {
        VkAttachmentDescription att{};
        att.format         = VK_FORMAT_R16G16B16A16_SFLOAT;
        att.samples        = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference ref{};
        ref.attachment = 0; ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1; sub.pColorAttachments = &ref;
        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
        dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
        dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo rpi{};
        rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpi.attachmentCount = 1; rpi.pAttachments = &att;
        rpi.subpassCount = 1; rpi.pSubpasses = &sub;
        rpi.dependencyCount = 1; rpi.pDependencies = &dep;
        if (vkCreateRenderPass(vk, &rpi, nullptr, &colorfx_render_pass_) != VK_SUCCESS) return false;
        VkFramebufferCreateInfo fbi{};
        fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbi.renderPass = colorfx_render_pass_;
        fbi.attachmentCount = 1; fbi.pAttachments = &output_view_;
        fbi.width = config_.width; fbi.height = config_.height; fbi.layers = 1;
        if (vkCreateFramebuffer(vk, &fbi, nullptr, &colorfx_framebuffer_) != VK_SUCCESS) return false;
    }
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding = 0; b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1; b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 1; li.pBindings = &b;
        if (vkCreateDescriptorSetLayout(vk, &li, nullptr, &colorfx_dsl_) != VK_SUCCESS) return false;
        VkDescriptorPoolSize ps{};
        ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps.descriptorCount = 1;
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets = 1; pi.poolSizeCount = 1; pi.pPoolSizes = &ps;
        if (vkCreateDescriptorPool(vk, &pi, nullptr, &colorfx_pool_) != VK_SUCCESS) return false;
        VkDescriptorSetAllocateInfo dai{};
        dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dai.descriptorPool = colorfx_pool_; dai.descriptorSetCount = 1; dai.pSetLayouts = &colorfx_dsl_;
        if (vkAllocateDescriptorSets(vk, &dai, &colorfx_set_) != VK_SUCCESS) return false;
        VkDescriptorImageInfo ii{};
        ii.sampler = colorfx_sampler_; ii.imageView = colorfx_intermediate_view_;
        ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w.dstSet = colorfx_set_;
        w.dstBinding = 0; w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &ii;
        vkUpdateDescriptorSets(vk, 1, &w, 0, nullptr);
    }
    {
        VkPushConstantRange pr{};
        pr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pr.size       = sizeof(float) * 20; // 17 used, rounded up
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1; pli.pSetLayouts = &colorfx_dsl_;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pr;
        if (vkCreatePipelineLayout(vk, &pli, nullptr, &colorfx_pipeline_layout_) != VK_SUCCESS) return false;

        auto vert = shader_registry_->create_from_spirv(kTonemapVertSpv, kTonemapVertSpv_size,
                                                        ShaderStage::Vertex, "colorfx.vert");
        auto frag = shader_registry_->create_from_spirv(kColorFxFragSpv, kColorFxFragSpv_size,
                                                        ShaderStage::Fragment, "colorfx.frag");
        if (!vert || !frag) return false;
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vert->handle; stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = frag->handle; stages[1].pName = "main";
        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        std::array<VkDynamicState, 2> dyns = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dyns.data();
        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = 0xF; cba.blendEnable = VK_FALSE;
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &cba;
        VkGraphicsPipelineCreateInfo gpi{};
        gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gpi.stageCount = static_cast<uint32_t>(stages.size()); gpi.pStages = stages.data();
        gpi.pVertexInputState = &vi; gpi.pInputAssemblyState = &ia;
        gpi.pViewportState = &vp; gpi.pRasterizationState = &rs;
        gpi.pMultisampleState = &ms; gpi.pColorBlendState = &cb; gpi.pDynamicState = &dyn;
        gpi.layout = colorfx_pipeline_layout_; gpi.renderPass = colorfx_render_pass_;
        if (vkCreateGraphicsPipelines(vk, VK_NULL_HANDLE, 1, &gpi, nullptr, &colorfx_pipeline_) != VK_SUCCESS) return false;
    }

    colorfx_initialized_ = true;
    spdlog::info("ColorFX initialized");
    return true;
}

void VulkanPostProcessing::apply_color_fx(VkCommandBuffer cmd) {
    if (output_image_ == VK_NULL_HANDLE) return;
    // Nothing to do if every colour effect is off.
    if (!chromatic_enabled_ && !vignette_enabled_ && !film_grain_enabled_ &&
        !sharpen_enabled_ && !lens_distortion_enabled_ && !color_grade_enabled_ &&
        !posterize_enabled_ && !pixelate_enabled_ && !scanlines_enabled_) return;
    if (!colorfx_initialized_ && !init_colorfx_resources()) return;

    colorfx_time_ += delta_time_;

    auto barrier = [&](VkImage img, VkImageLayout from, VkImageLayout to,
                        VkAccessFlags srcA, VkAccessFlags dstA,
                        VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = from; b.newLayout = to; b.image = img;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.levelCount = 1; b.subresourceRange.layerCount = 1;
        b.srcAccessMask = srcA; b.dstAccessMask = dstA;
        vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
    };

    barrier(output_image_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    barrier(colorfx_intermediate_image_, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageCopy region{};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1;
    region.dstSubresource = region.srcSubresource;
    region.extent = { config_.width, config_.height, 1 };
    vkCmdCopyImage(cmd, output_image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   colorfx_intermediate_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier(colorfx_intermediate_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    barrier(output_image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkRenderPassBeginInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass = colorfx_render_pass_; rpi.framebuffer = colorfx_framebuffer_;
    rpi.renderArea.offset = {0, 0}; rpi.renderArea.extent = {config_.width, config_.height};
    vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{}; vp.width = static_cast<float>(config_.width); vp.height = static_cast<float>(config_.height); vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{}; sc.extent = {config_.width, config_.height};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, colorfx_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            colorfx_pipeline_layout_, 0, 1, &colorfx_set_, 0, nullptr);

    struct {
        float texel_x, texel_y;
        float chromatic;
        float vignette;
        float vignette_radius;
        float grain;
        float time;
        float sharpen;
        float lens_distort;
        float temperature;
        float tint;
        float saturation;
        float contrast;
        float brightness;
        float posterize;
        float pixelate;
        float scanline;
    } pc{};
    pc.texel_x = 1.0f / static_cast<float>(config_.width);
    pc.texel_y = 1.0f / static_cast<float>(config_.height);
    pc.chromatic       = chromatic_enabled_  ? config_.chromatic_intensity   : 0.0f;
    pc.vignette        = vignette_enabled_   ? config_.vignette_intensity    : 0.0f;
    pc.vignette_radius = config_.vignette_radius;
    pc.grain           = film_grain_enabled_ ? config_.film_grain_intensity  : 0.0f;
    pc.time            = colorfx_time_;
    pc.sharpen         = sharpen_enabled_         ? config_.sharpen_intensity : 0.0f;
    pc.lens_distort    = lens_distortion_enabled_ ? config_.lens_distortion   : 0.0f;
    // Color grade: identity values when off so the math is a no-op.
    pc.temperature = color_grade_enabled_ ? config_.cg_temperature : 0.0f;
    pc.tint        = color_grade_enabled_ ? config_.cg_tint        : 0.0f;
    pc.saturation  = color_grade_enabled_ ? config_.cg_saturation  : 1.0f;
    pc.contrast    = color_grade_enabled_ ? config_.cg_contrast    : 1.0f;
    pc.brightness  = color_grade_enabled_ ? config_.cg_brightness  : 1.0f;
    pc.posterize   = posterize_enabled_ ? config_.posterize_levels : 0.0f;
    pc.pixelate    = pixelate_enabled_  ? config_.pixelate_size    : 0.0f;
    pc.scanline    = scanlines_enabled_ ? config_.scanline_intensity : 0.0f;
    vkCmdPushConstants(cmd, colorfx_pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

} // namespace gws::renderer::gpu
