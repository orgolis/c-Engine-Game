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
#include <stdexcept>
#include <cstring>
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
                             VkImageLayout from, VkImageLayout to) {
    VkImageMemoryBarrier barrier{};
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout                       = from;
    barrier.newLayout                       = to;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = img;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
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

} // namespace

Texture::~Texture() { destroy(); }

Texture::Texture(Texture&& other) noexcept
    : device_(other.device_),
      image_(other.image_),
      view_(other.view_),
      sampler_(other.sampler_),
      memory_(other.memory_),
      width_(other.width_),
      height_(other.height_) {
    other.device_  = nullptr;
    other.image_   = VK_NULL_HANDLE;
    other.view_    = VK_NULL_HANDLE;
    other.sampler_ = VK_NULL_HANDLE;
    other.memory_  = VK_NULL_HANDLE;
    other.width_   = 0;
    other.height_  = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        destroy();
        device_  = other.device_;
        image_   = other.image_;
        view_    = other.view_;
        sampler_ = other.sampler_;
        memory_  = other.memory_;
        width_   = other.width_;
        height_  = other.height_;
        other.device_  = nullptr;
        other.image_   = VK_NULL_HANDLE;
        other.view_    = VK_NULL_HANDLE;
        other.sampler_ = VK_NULL_HANDLE;
        other.memory_  = VK_NULL_HANDLE;
        other.width_   = 0;
        other.height_  = 0;
    }
    return *this;
}

void Texture::destroy() {
    if (!device_) return;
    VkDevice vkdev = device_->get_device();

    if (sampler_ != VK_NULL_HANDLE) { vkDestroySampler(vkdev, sampler_, nullptr); sampler_ = VK_NULL_HANDLE; }
    if (view_    != VK_NULL_HANDLE) { vkDestroyImageView(vkdev, view_, nullptr);  view_    = VK_NULL_HANDLE; }
    if (image_   != VK_NULL_HANDLE) { vkDestroyImage(vkdev, image_, nullptr);     image_   = VK_NULL_HANDLE; }
    if (memory_  != VK_NULL_HANDLE) { vkFreeMemory(vkdev, memory_, nullptr);      memory_  = VK_NULL_HANDLE; }
    device_ = nullptr;
}

std::unique_ptr<Texture> Texture::create_from_pixels(VulkanDevice* device,
                                                     const uint8_t* rgba_pixels,
                                                     uint32_t width, uint32_t height,
                                                     bool srgb) {
    if (!device || !rgba_pixels || width == 0 || height == 0) {
        spdlog::error("Texture::create_from_pixels: invalid args");
        return nullptr;
    }

    auto out     = std::unique_ptr<Texture>(new Texture());
    out->device_ = device;
    out->width_  = width;
    out->height_ = height;

    VkDevice vkdev    = device->get_device();
    VkFormat format   = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    VkDeviceSize size = static_cast<VkDeviceSize>(width) * height * 4;

    // 1) Staging buffer (host-visible, host-coherent)
    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = size;
    bi.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer staging = VK_NULL_HANDLE;
    if (vkCreateBuffer(vkdev, &bi, nullptr, &staging) != VK_SUCCESS) {
        return nullptr;
    }

    VkMemoryRequirements mreq;
    vkGetBufferMemoryRequirements(vkdev, staging, &mreq);

    VkMemoryAllocateInfo malloc{};
    malloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    malloc.allocationSize  = mreq.size;
    malloc.memoryTypeIndex = device->find_memory_type(
        mreq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    if (vkAllocateMemory(vkdev, &malloc, nullptr, &staging_mem) != VK_SUCCESS) {
        vkDestroyBuffer(vkdev, staging, nullptr);
        return nullptr;
    }
    vkBindBufferMemory(vkdev, staging, staging_mem, 0);

    void* mapped = nullptr;
    vkMapMemory(vkdev, staging_mem, 0, size, 0, &mapped);
    std::memcpy(mapped, rgba_pixels, static_cast<size_t>(size));
    vkUnmapMemory(vkdev, staging_mem);

    // 2) Device-local image
    VkImageCreateInfo ii{};
    ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType     = VK_IMAGE_TYPE_2D;
    ii.extent.width  = width;
    ii.extent.height = height;
    ii.extent.depth  = 1;
    ii.mipLevels     = 1;
    ii.arrayLayers   = 1;
    ii.format        = format;
    ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ii.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.samples       = VK_SAMPLE_COUNT_1_BIT;
    ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(vkdev, &ii, nullptr, &out->image_) != VK_SUCCESS) {
        vkFreeMemory(vkdev, staging_mem, nullptr);
        vkDestroyBuffer(vkdev, staging, nullptr);
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
        vkFreeMemory(vkdev, staging_mem, nullptr);
        vkDestroyBuffer(vkdev, staging, nullptr);
        return nullptr;
    }
    vkBindImageMemory(vkdev, out->image_, out->memory_, 0);

    // 3) Layout transition + copy + transition
    run_one_time_command(device, [&](VkCommandBuffer cmd) {
        transition_image_layout(cmd, out->image_,
                                VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy region{};
        region.bufferOffset                    = 0;
        region.bufferRowLength                 = 0;
        region.bufferImageHeight               = 0;
        region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel       = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount     = 1;
        region.imageOffset                     = {0, 0, 0};
        region.imageExtent                     = {width, height, 1};
        vkCmdCopyBufferToImage(cmd, staging, out->image_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        transition_image_layout(cmd, out->image_,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });

    vkFreeMemory(vkdev, staging_mem, nullptr);
    vkDestroyBuffer(vkdev, staging, nullptr);

    // 4) View
    VkImageViewCreateInfo vi{};
    vi.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image                           = out->image_;
    vi.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    vi.format                          = format;
    vi.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.baseMipLevel   = 0;
    vi.subresourceRange.levelCount     = 1;
    vi.subresourceRange.baseArrayLayer = 0;
    vi.subresourceRange.layerCount     = 1;
    if (vkCreateImageView(vkdev, &vi, nullptr, &out->view_) != VK_SUCCESS) {
        return nullptr;
    }

    // 5) Sampler (linear filter, repeat wrap, no anisotropy for simplicity)
    VkSamplerCreateInfo si{};
    si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter    = VK_FILTER_LINEAR;
    si.minFilter    = VK_FILTER_LINEAR;
    si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.minLod       = 0.0f;
    si.maxLod       = 0.0f;
    si.borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    if (vkCreateSampler(vkdev, &si, nullptr, &out->sampler_) != VK_SUCCESS) {
        return nullptr;
    }

    return out;
}

std::unique_ptr<Texture> Texture::create_from_memory(VulkanDevice* device,
                                                     const uint8_t* data, size_t size,
                                                     bool srgb) {
    int w = 0, h = 0, comp = 0;
    stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &comp, STBI_rgb_alpha);
    if (!pixels) {
        spdlog::error("Texture::create_from_memory: stb_image failed: {}", stbi_failure_reason());
        return nullptr;
    }
    auto out = create_from_pixels(device, pixels,
                                  static_cast<uint32_t>(w), static_cast<uint32_t>(h), srgb);
    stbi_image_free(pixels);
    return out;
}

std::unique_ptr<Texture> Texture::create_from_file(VulkanDevice* device,
                                                   const std::string& path, bool srgb) {
    int w = 0, h = 0, comp = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &comp, STBI_rgb_alpha);
    if (!pixels) {
        spdlog::error("Texture::create_from_file({}): stb_image failed: {}", path, stbi_failure_reason());
        return nullptr;
    }
    auto out = create_from_pixels(device, pixels,
                                  static_cast<uint32_t>(w), static_cast<uint32_t>(h), srgb);
    stbi_image_free(pixels);
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
