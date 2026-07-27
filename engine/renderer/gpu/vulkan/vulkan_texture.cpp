/**
 * @file vulkan_texture.cpp
 * @brief Texture implementation. Carries the single-translation-unit
 *        `STB_IMAGE_IMPLEMENTATION` for the whole project.
 */

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "vulkan_texture.h"
#include "vulkan_device.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace gws::renderer::gpu {

namespace {

// One-time-submit helper. Allocates a primary command buffer from the
// device's pool, runs the supplied lambda inside, submits to the graphics
// queue, waits, and frees. Used for the upload-staging path.
template <typename Fn>
void run_one_time_command(VulkanDevice* device, Fn&& fn) {
    VkDevice      vkdev = device->get_device();
    VkCommandPool pool  = device->get_command_pool();
    VkQueue       queue = device->get_graphics_queue();

    VkCommandBufferAllocateInfo alloc{};
    alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool        = pool;
    alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(vkdev, &alloc, &cmd) != VK_SUCCESS) {
        throw std::runtime_error("Texture: failed to allocate one-time command buffer");
    }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    fn(cmd);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(vkdev, pool, 1, &cmd);
}

void transition_image_layout(VkCommandBuffer cmd, VkImage img,
                             VkImageLayout from, VkImageLayout to,
                             uint32_t mip_count = 1) {
    VkImageMemoryBarrier barrier{};
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout                       = from;
    barrier.newLayout                       = to;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = img;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = mip_count;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;

    VkPipelineStageFlags src_stage = 0;
    VkPipelineStageFlags dst_stage = 0;

    if (from == VK_IMAGE_LAYOUT_UNDEFINED &&
        to   == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (from == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               to   == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::runtime_error("Texture: unsupported layout transition");
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

// Allocate a host-visible staging buffer, upload `size` bytes from `src`, and
// return the buffer + memory (caller frees). Returns false on failure.
bool make_staging(VulkanDevice* device, const void* src, VkDeviceSize size,
                  VkBuffer& out_buf, VkDeviceMemory& out_mem) {
    VkDevice vkdev = device->get_device();
    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = size;
    bi.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vkdev, &bi, nullptr, &out_buf) != VK_SUCCESS) return false;

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(vkdev, out_buf, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = device->find_memory_type(
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(vkdev, &ai, nullptr, &out_mem) != VK_SUCCESS) {
        vkDestroyBuffer(vkdev, out_buf, nullptr);
        out_buf = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(vkdev, out_buf, out_mem, 0);

    void* mapped = nullptr;
    vkMapMemory(vkdev, out_mem, 0, size, 0, &mapped);
    std::memcpy(mapped, src, static_cast<size_t>(size));
    vkUnmapMemory(vkdev, out_mem);
    return true;
}

} // namespace

Texture::~Texture() { destroy(); }

Texture::Texture(Texture&& other) noexcept
    : device_(other.device_),
      image_(other.image_),
      view_(other.view_),
      sampler_(other.sampler_),
      memory_(other.memory_),
      width_(other.width_),
      height_(other.height_),
      mip_levels_(other.mip_levels_),
      generation_(other.generation_),
      owns_sampler_(other.owns_sampler_) {
    other.device_       = nullptr;
    other.image_        = VK_NULL_HANDLE;
    other.view_         = VK_NULL_HANDLE;
    other.sampler_      = VK_NULL_HANDLE;
    other.memory_       = VK_NULL_HANDLE;
    other.width_        = 0;
    other.height_       = 0;
    other.mip_levels_   = 1;
    other.owns_sampler_ = true;
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        destroy();
        device_       = other.device_;
        image_        = other.image_;
        view_         = other.view_;
        sampler_      = other.sampler_;
        memory_       = other.memory_;
        width_        = other.width_;
        height_       = other.height_;
        mip_levels_   = other.mip_levels_;
        generation_   = other.generation_;
        owns_sampler_ = other.owns_sampler_;
        other.device_       = nullptr;
        other.image_        = VK_NULL_HANDLE;
        other.view_         = VK_NULL_HANDLE;
        other.sampler_      = VK_NULL_HANDLE;
        other.memory_       = VK_NULL_HANDLE;
        other.width_        = 0;
        other.height_       = 0;
        other.mip_levels_   = 1;
        other.owns_sampler_ = true;
    }
    return *this;
}

void Texture::destroy() {
    if (!device_) return;
    VkDevice vkdev = device_->get_device();

    // Only destroy the sampler when we own it — a borrowed cache sampler is
    // owned by the SamplerCache.
    if (owns_sampler_ && sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(vkdev, sampler_, nullptr);
    }
    sampler_ = VK_NULL_HANDLE;
    if (view_   != VK_NULL_HANDLE) { vkDestroyImageView(vkdev, view_, nullptr); view_   = VK_NULL_HANDLE; }
    if (image_  != VK_NULL_HANDLE) { vkDestroyImage(vkdev, image_, nullptr);    image_  = VK_NULL_HANDLE; }
    if (memory_ != VK_NULL_HANDLE) { vkFreeMemory(vkdev, memory_, nullptr);     memory_ = VK_NULL_HANDLE; }
    device_ = nullptr;
}

void Texture::destroy_image_only_() {
    if (!device_) return;
    VkDevice vkdev = device_->get_device();
    if (view_   != VK_NULL_HANDLE) { vkDestroyImageView(vkdev, view_, nullptr); view_   = VK_NULL_HANDLE; }
    if (image_  != VK_NULL_HANDLE) { vkDestroyImage(vkdev, image_, nullptr);    image_  = VK_NULL_HANDLE; }
    if (memory_ != VK_NULL_HANDLE) { vkFreeMemory(vkdev, memory_, nullptr);     memory_ = VK_NULL_HANDLE; }
}

bool Texture::build_rgba_(const uint8_t* rgba, uint32_t width, uint32_t height,
                          bool srgb, bool gen_mips) {
    VkDevice       vkdev  = device_->get_device();
    const VkFormat format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    width_  = width;
    height_ = height;

    // Decide the mip count. Generating mips on the GPU requires the format to
    // support linear blit as both source and destination; RGBA8 does on every
    // desktop GPU, but check so we degrade to a single mip rather than error.
    uint32_t mips = 1;
    if (gen_mips) {
        VkFormatProperties fp{};
        vkGetPhysicalDeviceFormatProperties(device_->get_physical_device(), format, &fp);
        const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                                          VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                                          VK_FORMAT_FEATURE_BLIT_DST_BIT;
        if ((fp.optimalTilingFeatures & need) == need) {
            uint32_t d = std::max(width, height);
            while (d > 1) { d >>= 1; ++mips; }
        }
    }
    mip_levels_ = mips;

    const VkDeviceSize size = static_cast<VkDeviceSize>(width) * height * 4;

    VkBuffer       staging     = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    if (!make_staging(device_, rgba, size, staging, staging_mem)) return false;

    // Device-local image (mipLevels = mips; TRANSFER_SRC needed when we blit
    // to generate the chain).
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (mips > 1) usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VkImageCreateInfo ii{};
    ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType     = VK_IMAGE_TYPE_2D;
    ii.extent        = {width, height, 1};
    ii.mipLevels     = mips;
    ii.arrayLayers   = 1;
    ii.format        = format;
    ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ii.usage         = usage;
    ii.samples       = VK_SAMPLE_COUNT_1_BIT;
    ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(vkdev, &ii, nullptr, &image_) != VK_SUCCESS) {
        vkFreeMemory(vkdev, staging_mem, nullptr); vkDestroyBuffer(vkdev, staging, nullptr);
        return false;
    }

    VkMemoryRequirements ireq;
    vkGetImageMemoryRequirements(vkdev, image_, &ireq);
    VkMemoryAllocateInfo ialloc{};
    ialloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ialloc.allocationSize  = ireq.size;
    ialloc.memoryTypeIndex = device_->find_memory_type(ireq.memoryTypeBits,
                                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vkdev, &ialloc, nullptr, &memory_) != VK_SUCCESS) {
        vkDestroyImage(vkdev, image_, nullptr); image_ = VK_NULL_HANDLE;
        vkFreeMemory(vkdev, staging_mem, nullptr); vkDestroyBuffer(vkdev, staging, nullptr);
        return false;
    }
    vkBindImageMemory(vkdev, image_, memory_, 0);

    run_one_time_command(device_, [&](VkCommandBuffer cmd) {
        // All mips -> TRANSFER_DST, then copy the base level from staging.
        transition_image_layout(cmd, image_, VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mips);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel   = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent                 = {width, height, 1};
        vkCmdCopyBufferToImage(cmd, staging, image_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        if (mips == 1) {
            transition_image_layout(cmd, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
        } else {
            // Generate the mip chain by successively blitting level i-1 -> i,
            // transitioning each finished source level to SHADER_READ.
            VkImageMemoryBarrier b{};
            b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.image               = image_;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.subresourceRange.aspectMask   = VK_IMAGE_ASPECT_COLOR_BIT;
            b.subresourceRange.layerCount   = 1;
            b.subresourceRange.levelCount   = 1;

            int32_t mw = static_cast<int32_t>(width);
            int32_t mh = static_cast<int32_t>(height);
            for (uint32_t i = 1; i < mips; ++i) {
                // Level i-1: TRANSFER_DST -> TRANSFER_SRC (blit source).
                b.subresourceRange.baseMipLevel = i - 1;
                b.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                b.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                     0, nullptr, 0, nullptr, 1, &b);

                const int32_t nw = mw > 1 ? mw / 2 : 1;
                const int32_t nh = mh > 1 ? mh / 2 : 1;
                VkImageBlit blit{};
                blit.srcOffsets[0] = {0, 0, 0};
                blit.srcOffsets[1] = {mw, mh, 1};
                blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.srcSubresource.mipLevel   = i - 1;
                blit.srcSubresource.layerCount = 1;
                blit.dstOffsets[0] = {0, 0, 0};
                blit.dstOffsets[1] = {nw, nh, 1};
                blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.mipLevel   = i;
                blit.dstSubresource.layerCount = 1;
                vkCmdBlitImage(cmd,
                               image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1, &blit, VK_FILTER_LINEAR);

                // Level i-1 done as a source -> SHADER_READ.
                b.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                b.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                     0, nullptr, 0, nullptr, 1, &b);

                mw = nw; mh = nh;
            }

            // Last mip is still TRANSFER_DST -> SHADER_READ.
            b.subresourceRange.baseMipLevel = mips - 1;
            b.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                 0, nullptr, 0, nullptr, 1, &b);
        }
    });

    vkFreeMemory(vkdev, staging_mem, nullptr);
    vkDestroyBuffer(vkdev, staging, nullptr);

    // View over all mips.
    VkImageViewCreateInfo vi{};
    vi.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image                           = image_;
    vi.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    vi.format                          = format;
    vi.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.baseMipLevel   = 0;
    vi.subresourceRange.levelCount     = mips;
    vi.subresourceRange.baseArrayLayer = 0;
    vi.subresourceRange.layerCount     = 1;
    if (vkCreateImageView(vkdev, &vi, nullptr, &view_) != VK_SUCCESS) {
        return false;
    }
    return true;
}

std::unique_ptr<Texture> Texture::create_from_pixels(VulkanDevice* device,
                                                     const uint8_t* rgba_pixels,
                                                     uint32_t width, uint32_t height,
                                                     bool srgb, bool gen_mips,
                                                     VkSampler shared_sampler) {
    if (!device || !rgba_pixels || width == 0 || height == 0) {
        spdlog::error("Texture::create_from_pixels: invalid args");
        return nullptr;
    }

    auto out     = std::unique_ptr<Texture>(new Texture());
    out->device_ = device;

    if (!out->build_rgba_(rgba_pixels, width, height, srgb, gen_mips)) {
        return nullptr;
    }

    if (shared_sampler != VK_NULL_HANDLE) {
        out->sampler_      = shared_sampler;
        out->owns_sampler_ = false;
    } else {
        // Baked default sampler: linear, repeat, samples the full mip chain
        // (maxLod = CLAMP_NONE so it stays valid across an in-place reload that
        // changes the mip count).
        VkSamplerCreateInfo si{};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = VK_FILTER_LINEAR;
        si.minFilter    = VK_FILTER_LINEAR;
        si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.minLod       = 0.0f;
        si.maxLod       = VK_LOD_CLAMP_NONE;
        si.borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        if (vkCreateSampler(device->get_device(), &si, nullptr, &out->sampler_) != VK_SUCCESS) {
            return nullptr;
        }
        out->owns_sampler_ = true;
    }

    return out;
}

bool Texture::reload_from_pixels(const uint8_t* rgba, uint32_t width, uint32_t height,
                                 bool srgb, bool gen_mips) {
    if (!device_ || !rgba || width == 0 || height == 0) return false;
    // Caller guarantees the GPU is idle. Rebuild image/view, keep the sampler.
    destroy_image_only_();
    if (!build_rgba_(rgba, width, height, srgb, gen_mips)) return false;
    ++generation_;
    return true;
}

std::unique_ptr<Texture> Texture::create_from_float_pixels(VulkanDevice* device,
                                                          const float* rgba_pixels,
                                                          uint32_t width, uint32_t height) {
    if (!device || !rgba_pixels || width == 0 || height == 0) {
        spdlog::error("Texture::create_from_float_pixels: invalid args");
        return nullptr;
    }

    auto out     = std::unique_ptr<Texture>(new Texture());
    out->device_ = device;
    out->width_  = width;
    out->height_ = height;

    VkDevice vkdev      = device->get_device();
    const VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
    const VkDeviceSize size = static_cast<VkDeviceSize>(width) * height * 4 * sizeof(float);

    VkBuffer       staging     = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    if (!make_staging(device, rgba_pixels, size, staging, staging_mem)) return nullptr;

    VkImageCreateInfo ii{};
    ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType     = VK_IMAGE_TYPE_2D;
    ii.extent        = {width, height, 1};
    ii.mipLevels     = 1;
    ii.arrayLayers   = 1;
    ii.format        = format;
    ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ii.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.samples       = VK_SAMPLE_COUNT_1_BIT;
    ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(vkdev, &ii, nullptr, &out->image_) != VK_SUCCESS) {
        vkFreeMemory(vkdev, staging_mem, nullptr); vkDestroyBuffer(vkdev, staging, nullptr);
        return nullptr;
    }
    VkMemoryRequirements ireq;
    vkGetImageMemoryRequirements(vkdev, out->image_, &ireq);
    VkMemoryAllocateInfo ialloc{};
    ialloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ialloc.allocationSize  = ireq.size;
    ialloc.memoryTypeIndex = device->find_memory_type(ireq.memoryTypeBits,
                                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vkdev, &ialloc, nullptr, &out->memory_) != VK_SUCCESS) {
        vkFreeMemory(vkdev, staging_mem, nullptr); vkDestroyBuffer(vkdev, staging, nullptr);
        return nullptr;
    }
    vkBindImageMemory(vkdev, out->image_, out->memory_, 0);

    run_one_time_command(device, [&](VkCommandBuffer cmd) {
        transition_image_layout(cmd, out->image_,
                                VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent                 = {width, height, 1};
        vkCmdCopyBufferToImage(cmd, staging, out->image_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        transition_image_layout(cmd, out->image_,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });
    vkFreeMemory(vkdev, staging_mem, nullptr);
    vkDestroyBuffer(vkdev, staging, nullptr);

    VkImageViewCreateInfo vi{};
    vi.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image                       = out->image_;
    vi.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    vi.format                      = format;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(vkdev, &vi, nullptr, &out->view_) != VK_SUCCESS) return nullptr;

    // Sampler — linear + CLAMP_TO_EDGE (LUTs must not wrap).
    VkSamplerCreateInfo si{};
    si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter    = VK_FILTER_LINEAR;
    si.minFilter    = VK_FILTER_LINEAR;
    si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    if (vkCreateSampler(vkdev, &si, nullptr, &out->sampler_) != VK_SUCCESS) return nullptr;

    return out;
}

std::unique_ptr<Texture> Texture::create_from_memory(VulkanDevice* device,
                                                     const uint8_t* data, size_t size,
                                                     bool srgb, bool gen_mips,
                                                     VkSampler shared_sampler) {
    int w = 0, h = 0, comp = 0;
    stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &comp, STBI_rgb_alpha);
    if (!pixels) {
        spdlog::error("Texture::create_from_memory: stb_image failed: {}", stbi_failure_reason());
        return nullptr;
    }
    auto out = create_from_pixels(device, pixels,
                                  static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                                  srgb, gen_mips, shared_sampler);
    stbi_image_free(pixels);
    return out;
}

std::unique_ptr<Texture> Texture::create_from_file(VulkanDevice* device,
                                                   const std::string& path, bool srgb,
                                                   bool gen_mips, VkSampler shared_sampler) {
    // Read the bytes ourselves via std::filesystem::path constructed from
    // UTF-8, then decode from memory. stbi_load(path) uses a narrow fopen
    // which fails for non-ASCII (e.g. Cyrillic) filenames on Windows.
    const std::filesystem::path fspath(
        std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
    std::ifstream f(fspath, std::ios::binary | std::ios::ate);
    if (!f) {
        spdlog::error("Texture::create_from_file({}): cannot open file", path);
        return nullptr;
    }
    const std::streamsize n = f.tellg();
    if (n <= 0) {
        spdlog::error("Texture::create_from_file({}): empty file", path);
        return nullptr;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(n));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(bytes.data()), n)) {
        spdlog::error("Texture::create_from_file({}): read failed", path);
        return nullptr;
    }

    int w = 0, h = 0, comp = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                            &w, &h, &comp, STBI_rgb_alpha);
    if (!pixels) {
        spdlog::error("Texture::create_from_file({}): stb_image failed: {}", path, stbi_failure_reason());
        return nullptr;
    }
    auto out = create_from_pixels(device, pixels,
                                  static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                                  srgb, gen_mips, shared_sampler);
    stbi_image_free(pixels);
    return out;
}

namespace {
// Bytes per 4x4 block for the BC VkFormats we cook (BC1 = 8, rest = 16).
uint32_t bc_vk_block_bytes(VkFormat f) {
    switch (f) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
            return 8;
        default:
            return 16;  // BC3 / BC5 / BC7
    }
}
}  // namespace

std::unique_ptr<Texture> Texture::create_compressed(VulkanDevice* device,
                                                    VkFormat format,
                                                    uint32_t width, uint32_t height,
                                                    uint32_t mip_count,
                                                    const uint8_t* block_data,
                                                    size_t data_size,
                                                    VkSampler shared_sampler) {
    if (!device || !block_data || width == 0 || height == 0 || mip_count == 0 || data_size == 0) {
        spdlog::error("Texture::create_compressed: invalid args");
        return nullptr;
    }

    VkDevice vkdev = device->get_device();

    // Bail (let the caller fall back) if the GPU can't sample this BC format.
    VkFormatProperties fp{};
    vkGetPhysicalDeviceFormatProperties(device->get_physical_device(), format, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
        spdlog::warn("Texture::create_compressed: format {} not sampleable on this GPU",
                     static_cast<int>(format));
        return nullptr;
    }

    auto out     = std::unique_ptr<Texture>(new Texture());
    out->device_      = device;
    out->width_       = width;
    out->height_      = height;
    out->mip_levels_  = mip_count;

    const uint32_t bb = bc_vk_block_bytes(format);

    VkBuffer       staging     = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    if (!make_staging(device, block_data, data_size, staging, staging_mem)) return nullptr;

    // Device-local compressed image with the full mip chain.
    VkImageCreateInfo ii{};
    ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType     = VK_IMAGE_TYPE_2D;
    ii.extent        = {width, height, 1};
    ii.mipLevels     = mip_count;
    ii.arrayLayers   = 1;
    ii.format        = format;
    ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ii.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.samples       = VK_SAMPLE_COUNT_1_BIT;
    ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(vkdev, &ii, nullptr, &out->image_) != VK_SUCCESS) {
        vkFreeMemory(vkdev, staging_mem, nullptr); vkDestroyBuffer(vkdev, staging, nullptr);
        return nullptr;
    }
    VkMemoryRequirements ireq;
    vkGetImageMemoryRequirements(vkdev, out->image_, &ireq);
    VkMemoryAllocateInfo ia{};
    ia.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ia.allocationSize  = ireq.size;
    ia.memoryTypeIndex = device->find_memory_type(ireq.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vkdev, &ia, nullptr, &out->memory_) != VK_SUCCESS) {
        vkFreeMemory(vkdev, staging_mem, nullptr); vkDestroyBuffer(vkdev, staging, nullptr);
        return nullptr;
    }
    vkBindImageMemory(vkdev, out->image_, out->memory_, 0);

    // One copy region per mip (compressed: imageExtent is in texels, buffer
    // offset advances by each mip's BC-block byte size).
    run_one_time_command(device, [&](VkCommandBuffer cmd) {
        transition_image_layout(cmd, out->image_, VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mip_count);
        VkDeviceSize offset = 0;
        uint32_t mw = width, mh = height;
        std::vector<VkBufferImageCopy> regions;
        regions.reserve(mip_count);
        for (uint32_t m = 0; m < mip_count; ++m) {
            const uint32_t blocks = ((mw + 3) / 4) * ((mh + 3) / 4);
            VkBufferImageCopy r{};
            r.bufferOffset                    = offset;
            r.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            r.imageSubresource.mipLevel       = m;
            r.imageSubresource.baseArrayLayer = 0;
            r.imageSubresource.layerCount     = 1;
            r.imageExtent                     = {mw, mh, 1};
            regions.push_back(r);
            offset += static_cast<VkDeviceSize>(blocks) * bb;
            mw = std::max(1u, mw / 2); mh = std::max(1u, mh / 2);
        }
        vkCmdCopyBufferToImage(cmd, staging, out->image_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<uint32_t>(regions.size()), regions.data());
        transition_image_layout(cmd, out->image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mip_count);
    });
    vkFreeMemory(vkdev, staging_mem, nullptr);
    vkDestroyBuffer(vkdev, staging, nullptr);

    // View over all mips.
    VkImageViewCreateInfo vi{};
    vi.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image                           = out->image_;
    vi.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    vi.format                          = format;
    vi.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.baseMipLevel   = 0;
    vi.subresourceRange.levelCount     = mip_count;
    vi.subresourceRange.baseArrayLayer = 0;
    vi.subresourceRange.layerCount     = 1;
    if (vkCreateImageView(vkdev, &vi, nullptr, &out->view_) != VK_SUCCESS) return nullptr;

    if (shared_sampler != VK_NULL_HANDLE) {
        out->sampler_      = shared_sampler;
        out->owns_sampler_ = false;
    } else {
        VkSamplerCreateInfo si{};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = VK_FILTER_LINEAR;
        si.minFilter    = VK_FILTER_LINEAR;
        si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.minLod       = 0.0f;
        si.maxLod       = static_cast<float>(mip_count - 1);
        si.borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        if (vkCreateSampler(vkdev, &si, nullptr, &out->sampler_) != VK_SUCCESS) return nullptr;
        out->owns_sampler_ = true;
    }

    return out;
}

std::unique_ptr<Texture> Texture::create_default_white(VulkanDevice* device) {
    constexpr uint8_t white[4] = {255, 255, 255, 255};
    return create_from_pixels(device, white, 1, 1, /*srgb=*/false);
}

std::unique_ptr<Texture> Texture::create_default_normal(VulkanDevice* device) {
    constexpr uint8_t normal[4] = {128, 128, 255, 255};
    return create_from_pixels(device, normal, 1, 1, /*srgb=*/false);
}

std::unique_ptr<Texture> Texture::create_default_black(VulkanDevice* device) {
    constexpr uint8_t black[4] = {0, 0, 0, 255};
    return create_from_pixels(device, black, 1, 1, /*srgb=*/false);
}

} // namespace gws::renderer::gpu
