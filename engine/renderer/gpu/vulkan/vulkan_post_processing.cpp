/**
 * @file vulkan_post_processing.cpp
 * @brief Vulkan post-processing implementation
 */

#include "vulkan_post_processing.h"
#include "vulkan_device.h"
#include <spdlog/spdlog.h>
#include <stdexcept>

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
        post_proc->create_output_image();
        
        if (config.bloom.enabled) {
            post_proc->create_bloom_resources();
        }
        
        if (config.taa.enabled) {
            post_proc->create_taa_resources();
        }
        
        post_proc->create_descriptor_sets();
        post_proc->create_pipelines();
        
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

void VulkanPostProcessing::create_descriptor_sets() {
    // Placeholder for descriptor set creation
    spdlog::debug("Descriptor sets creation placeholder");
}

void VulkanPostProcessing::create_pipelines() {
    // Placeholder for pipeline creation
    spdlog::debug("Pipelines creation placeholder");
}

void VulkanPostProcessing::set_input_image(VkImageView image_view) {
    input_view_ = image_view;
}

void VulkanPostProcessing::apply_bloom(VkCommandBuffer cmd) {
    if (!bloom_enabled_) return;
    spdlog::debug("Applying bloom effect");
}

void VulkanPostProcessing::apply_tone_mapping(VkCommandBuffer cmd) {
    if (!tone_mapping_enabled_) return;
    spdlog::debug("Applying tone mapping");
}

void VulkanPostProcessing::apply_taa(VkCommandBuffer cmd) {
    if (!taa_enabled_) return;
    spdlog::debug("Applying TAA");
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
}

} // namespace gws::renderer::gpu
