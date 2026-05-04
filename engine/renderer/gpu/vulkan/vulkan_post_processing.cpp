/**
 * @file vulkan_post_processing.cpp
 * @brief Vulkan post-processing implementation
 */

#include "vulkan_post_processing.h"
#include "vulkan_device.h"
#include "vulkan_shader_registry.h"
#include "post_proc_tonemap_spirv.h" // pre-compiled SPIR-V fallback for GCC builds
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
    color *= pc.exposure;
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

    if (vkCreateDescriptorSetLayout(vk_device, &layout_info, nullptr, &descriptor_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create tone mapping descriptor set layout");
    }

    VkDescriptorPoolSize pool_size{};
    pool_size.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = static_cast<uint32_t>(bindings.size());

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes    = &pool_size;
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

    std::array<VkWriteDescriptorSet, 2> writes{};
    for (uint32_t i = 0; i < writes.size(); ++i) {
        writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet          = descriptor_sets_[0];
        writes[i].dstBinding      = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo      = &image_infos[i];
    }

    vkUpdateDescriptorSets(device_->get_device(),
                           static_cast<uint32_t>(writes.size()), writes.data(),
                           0, nullptr);
}

void VulkanPostProcessing::set_input_image(VkImageView image_view) {
    input_view_ = image_view;
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
    if (tone_mapping_enabled_) apply_tone_mapping(cmd);
    if (chromatic_enabled_) apply_chromatic(cmd);
    if (vignette_enabled_) apply_vignette(cmd);
    if (film_grain_enabled_) apply_film_grain(cmd);
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

    // Drop the shader registry last so its cached VkShaderModules are torn
    // down while the device is still alive.
    shader_registry_.reset();
}

} // namespace gws::renderer::gpu
