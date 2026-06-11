/**
 * @file vulkan_environment_map.cpp
 * @brief Implementation of the HDR environment cubemap.
 */

#include "vulkan_environment_map.h"
#include "vulkan_device.h"
#include "equirect_to_cubemap_spirv.h"
#include "ibl_irradiance_spirv.h"
#include "ibl_prefilter_env_spirv.h"
#include "ibl_brdf_lut_spirv.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <stb_image.h>

#include <spdlog/spdlog.h>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace gws::renderer::gpu {

namespace {

constexpr VkFormat kCubemapFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

// Tiny IEEE-754 single → half. Doesn't handle subnormals / NaN robustly —
// fine for slowly-varying gradient data, never used for HDR file loads
// (those go through the compute shader instead).
inline uint16_t float_to_half(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    const uint32_t sign     = (bits >> 31) & 0x1;
    int32_t       exp_bits  = static_cast<int32_t>((bits >> 23) & 0xff) - 127 + 15;
    uint32_t      mant_bits = (bits >> 13) & 0x3ff;
    if (exp_bits <= 0)  return static_cast<uint16_t>(sign << 15);
    if (exp_bits >= 31) return static_cast<uint16_t>((sign << 15) | (31 << 10));
    return static_cast<uint16_t>((sign << 15) | (exp_bits << 10) | mant_bits);
}

// One-time-submit helper: allocate primary cmd, beginRecording, run lambda,
// submit, wait, free. Mirrors the same pattern as `vulkan_texture.cpp` so
// we don't introduce yet another command-buffer lifetime style.
template <typename Fn>
void run_one_time_command(VulkanDevice* device, Fn&& fn) {
    VkDevice      vk   = device->get_device();
    VkCommandPool pool = device->get_command_pool();
    VkQueue       queue = device->get_graphics_queue();

    VkCommandBufferAllocateInfo alloc{};
    alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool        = pool;
    alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(vk, &alloc, &cmd) != VK_SUCCESS) {
        throw std::runtime_error("EnvironmentMap: failed to allocate one-time command buffer");
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
    vkFreeCommandBuffers(vk, pool, 1, &cmd);
}

// Same face-axis convention as the compute shader, in CPU space. Used by
// the procedural fallback to walk each face's pixels.
glm::vec3 face_direction(uint32_t face, uint32_t x, uint32_t y, uint32_t size) {
    const float u = ((static_cast<float>(x) + 0.5f) / static_cast<float>(size)) * 2.0f - 1.0f;
    const float v = ((static_cast<float>(y) + 0.5f) / static_cast<float>(size)) * 2.0f - 1.0f;
    glm::vec3 d;
    switch (face) {
        case 0: d = { 1.0f, -v, -u}; break; // +X
        case 1: d = {-1.0f, -v,  u}; break; // -X
        case 2: d = {  u,   1.0f,  v}; break; // +Y
        case 3: d = {  u,  -1.0f, -v}; break; // -Y
        case 4: d = {  u,  -v,  1.0f}; break; // +Z
        default:d = { -u,  -v, -1.0f}; break; // -Z
    }
    return glm::normalize(d);
}

// Match the palette of the gradient fragment shader so the procedural
// fallback cubemap is visually identical to "session 1's" inline sky.
glm::vec3 gradient_for_dir(const glm::vec3& d) {
    const float t = glm::clamp(d.y * 0.5f + 0.5f, 0.0f, 1.0f);
    const float s = t * t * (3.0f - 2.0f * t);
    const glm::vec3 horizon{0.55f, 0.65f, 0.78f};
    const glm::vec3 zenith {0.12f, 0.24f, 0.50f};
    return glm::mix(horizon, zenith, s);
}

} // namespace

// ---- Construction helpers --------------------------------------------------

std::unique_ptr<VulkanEnvironmentMap>
VulkanEnvironmentMap::create_procedural(VulkanDevice* device, uint32_t face_size) {
    if (device == nullptr || face_size == 0) return nullptr;
    auto em = std::unique_ptr<VulkanEnvironmentMap>(new VulkanEnvironmentMap());
    em->device_    = device;
    em->face_size_ = face_size;
    if (!em->create_cubemap_image(kCubemapFormat)) return nullptr;
    if (!em->create_sampler())                      return nullptr;
    em->upload_procedural_faces();
    spdlog::info("VulkanEnvironmentMap: procedural cubemap built ({}^2 per face)", face_size);
    return em;
}

std::unique_ptr<VulkanEnvironmentMap>
VulkanEnvironmentMap::create_from_hdr(VulkanDevice* device,
                                       const std::string& path,
                                       uint32_t face_size) {
    if (device == nullptr || face_size == 0) return nullptr;
    int w = 0, h = 0, n = 0;
    float* data = stbi_loadf(path.c_str(), &w, &h, &n, 4);
    if (data == nullptr || w <= 0 || h <= 0) {
        spdlog::warn("VulkanEnvironmentMap::create_from_hdr: failed to load {}", path);
        return nullptr;
    }
    auto em = std::unique_ptr<VulkanEnvironmentMap>(new VulkanEnvironmentMap());
    em->device_    = device;
    em->face_size_ = face_size;
    if (!em->create_cubemap_image(kCubemapFormat) || !em->create_sampler()) {
        stbi_image_free(data);
        return nullptr;
    }
    const bool ok = em->build_from_equirect_buffer(data, w, h);
    stbi_image_free(data);
    if (!ok) return nullptr;
    spdlog::info("VulkanEnvironmentMap: HDR cubemap built from {} ({}^2 per face, source {}x{})",
                 path, face_size, w, h);
    return em;
}

VulkanEnvironmentMap::~VulkanEnvironmentMap() { destroy(); }

void VulkanEnvironmentMap::destroy() {
    if (device_ == nullptr) return;
    VkDevice vk = device_->get_device();
    if (brdf_lut_sampler_ != VK_NULL_HANDLE) { vkDestroySampler  (vk, brdf_lut_sampler_, nullptr); brdf_lut_sampler_ = VK_NULL_HANDLE; }
    if (brdf_lut_view_    != VK_NULL_HANDLE) { vkDestroyImageView(vk, brdf_lut_view_,    nullptr); brdf_lut_view_    = VK_NULL_HANDLE; }
    if (brdf_lut_image_   != VK_NULL_HANDLE) { vkDestroyImage    (vk, brdf_lut_image_,   nullptr); brdf_lut_image_   = VK_NULL_HANDLE; }
    if (brdf_lut_memory_  != VK_NULL_HANDLE) { vkFreeMemory      (vk, brdf_lut_memory_,  nullptr); brdf_lut_memory_  = VK_NULL_HANDLE; }
    if (prefiltered_view_    != VK_NULL_HANDLE) { vkDestroyImageView(vk, prefiltered_view_,    nullptr); prefiltered_view_    = VK_NULL_HANDLE; }
    if (prefiltered_image_   != VK_NULL_HANDLE) { vkDestroyImage    (vk, prefiltered_image_,   nullptr); prefiltered_image_   = VK_NULL_HANDLE; }
    if (prefiltered_memory_  != VK_NULL_HANDLE) { vkFreeMemory      (vk, prefiltered_memory_,  nullptr); prefiltered_memory_  = VK_NULL_HANDLE; }
    if (irradiance_view_    != VK_NULL_HANDLE) { vkDestroyImageView(vk, irradiance_view_,    nullptr); irradiance_view_    = VK_NULL_HANDLE; }
    if (irradiance_image_   != VK_NULL_HANDLE) { vkDestroyImage    (vk, irradiance_image_,   nullptr); irradiance_image_   = VK_NULL_HANDLE; }
    if (irradiance_memory_  != VK_NULL_HANDLE) { vkFreeMemory      (vk, irradiance_memory_,  nullptr); irradiance_memory_  = VK_NULL_HANDLE; }
    if (sampler_       != VK_NULL_HANDLE) { vkDestroySampler  (vk, sampler_,       nullptr); sampler_       = VK_NULL_HANDLE; }
    if (cubemap_view_  != VK_NULL_HANDLE) { vkDestroyImageView(vk, cubemap_view_,  nullptr); cubemap_view_  = VK_NULL_HANDLE; }
    if (cubemap_image_ != VK_NULL_HANDLE) { vkDestroyImage    (vk, cubemap_image_, nullptr); cubemap_image_ = VK_NULL_HANDLE; }
    if (cubemap_memory_!= VK_NULL_HANDLE) { vkFreeMemory      (vk, cubemap_memory_,nullptr); cubemap_memory_= VK_NULL_HANDLE; }
    device_ = nullptr;
}

// ---- Image / view / sampler creation --------------------------------------

bool VulkanEnvironmentMap::create_cubemap_image(VkFormat format) {
    VkDevice vk = device_->get_device();
    VkImageCreateInfo ii{};
    ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType     = VK_IMAGE_TYPE_2D;
    ii.format        = format;
    ii.extent        = { face_size_, face_size_, 1 };
    ii.mipLevels     = 1;
    ii.arrayLayers   = 6;
    ii.samples       = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ii.usage         = VK_IMAGE_USAGE_SAMPLED_BIT
                     | VK_IMAGE_USAGE_STORAGE_BIT
                     | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ii.flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(vk, &ii, nullptr, &cubemap_image_) != VK_SUCCESS) {
        spdlog::error("VulkanEnvironmentMap: vkCreateImage failed"); return false;
    }
    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(vk, cubemap_image_, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = device_->find_memory_type(
        mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vk, &ai, nullptr, &cubemap_memory_) != VK_SUCCESS) return false;
    vkBindImageMemory(vk, cubemap_image_, cubemap_memory_, 0);

    VkImageViewCreateInfo vi{};
    vi.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image                       = cubemap_image_;
    vi.viewType                    = VK_IMAGE_VIEW_TYPE_CUBE;
    vi.format                      = format;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 6;
    if (vkCreateImageView(vk, &vi, nullptr, &cubemap_view_) != VK_SUCCESS) return false;
    return true;
}

bool VulkanEnvironmentMap::create_sampler() {
    VkSamplerCreateInfo si{};
    si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter    = VK_FILTER_LINEAR;
    si.minFilter    = VK_FILTER_LINEAR;
    si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    return vkCreateSampler(device_->get_device(), &si, nullptr, &sampler_) == VK_SUCCESS;
}

// ---- Procedural fallback ---------------------------------------------------

void VulkanEnvironmentMap::upload_procedural_faces() {
    VkDevice vk = device_->get_device();
    const VkDeviceSize bytes_per_face =
        static_cast<VkDeviceSize>(face_size_) * face_size_ * 8; // 4 channels * uint16_t
    const VkDeviceSize bytes_total = bytes_per_face * 6;

    VkBuffer       staging      = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem  = VK_NULL_HANDLE;
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size  = bytes_total;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(vk, &bi, nullptr, &staging);
    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(vk, staging, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = device_->find_memory_type(
        mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(vk, &ai, nullptr, &staging_mem);
    vkBindBufferMemory(vk, staging, staging_mem, 0);

    void* mapped = nullptr;
    vkMapMemory(vk, staging_mem, 0, bytes_total, 0, &mapped);
    auto* dst = static_cast<uint16_t*>(mapped);
    for (uint32_t face = 0; face < 6; ++face) {
        for (uint32_t y = 0; y < face_size_; ++y) {
            for (uint32_t x = 0; x < face_size_; ++x) {
                const glm::vec3 d = face_direction(face, x, y, face_size_);
                const glm::vec3 c = gradient_for_dir(d);
                dst[0] = float_to_half(c.r);
                dst[1] = float_to_half(c.g);
                dst[2] = float_to_half(c.b);
                dst[3] = float_to_half(1.0f);
                dst += 4;
            }
        }
    }
    vkUnmapMemory(vk, staging_mem);

    run_one_time_command(device_, [&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier b{};
        b.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout                   = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.image                       = cubemap_image_;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.layerCount = 6;
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);

        std::array<VkBufferImageCopy, 6> regions{};
        for (uint32_t face = 0; face < 6; ++face) {
            auto& r = regions[face];
            r.bufferOffset                    = bytes_per_face * face;
            r.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            r.imageSubresource.baseArrayLayer = face;
            r.imageSubresource.layerCount     = 1;
            r.imageExtent                     = { face_size_, face_size_, 1 };
        }
        vkCmdCopyBufferToImage(cmd, staging, cubemap_image_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<uint32_t>(regions.size()), regions.data());

        b.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
    });

    vkDestroyBuffer(vk, staging, nullptr);
    vkFreeMemory(vk, staging_mem, nullptr);
}

// ---- HDR equirectangular → cubemap (compute) ------------------------------

bool VulkanEnvironmentMap::build_from_equirect_buffer(const float* rgba_data,
                                                       int src_width,
                                                       int src_height) {
    VkDevice vk = device_->get_device();

    // Stage the equirectangular texture as a 2D R32G32B32A32_SFLOAT image
    // we can sample from in the compute shader.
    VkImage        src_image  = VK_NULL_HANDLE;
    VkDeviceMemory src_memory = VK_NULL_HANDLE;
    VkImageView    src_view   = VK_NULL_HANDLE;
    VkSampler      src_sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl  = VK_NULL_HANDLE;
    VkPipelineLayout      pl   = VK_NULL_HANDLE;
    VkPipeline            pipe = VK_NULL_HANDLE;
    VkDescriptorPool      pool = VK_NULL_HANDLE;
    VkImageView    writable_cube_view = VK_NULL_HANDLE;
    bool ok = false;

    auto cleanup = [&]() {
        if (writable_cube_view != VK_NULL_HANDLE) vkDestroyImageView(vk, writable_cube_view, nullptr);
        if (pool      != VK_NULL_HANDLE) vkDestroyDescriptorPool(vk, pool, nullptr);
        if (pipe      != VK_NULL_HANDLE) vkDestroyPipeline(vk, pipe, nullptr);
        if (pl        != VK_NULL_HANDLE) vkDestroyPipelineLayout(vk, pl, nullptr);
        if (dsl       != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(vk, dsl, nullptr);
        if (src_sampler != VK_NULL_HANDLE) vkDestroySampler(vk, src_sampler, nullptr);
        if (src_view  != VK_NULL_HANDLE) vkDestroyImageView(vk, src_view, nullptr);
        if (src_image != VK_NULL_HANDLE) vkDestroyImage(vk, src_image, nullptr);
        if (src_memory != VK_NULL_HANDLE) vkFreeMemory(vk, src_memory, nullptr);
    };

    // -- create the equirect source image --
    VkImageCreateInfo ii{};
    ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType     = VK_IMAGE_TYPE_2D;
    ii.format        = VK_FORMAT_R32G32B32A32_SFLOAT;
    ii.extent        = { static_cast<uint32_t>(src_width),
                         static_cast<uint32_t>(src_height), 1 };
    ii.mipLevels     = 1;
    ii.arrayLayers   = 1;
    ii.samples       = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ii.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(vk, &ii, nullptr, &src_image) != VK_SUCCESS) { cleanup(); return false; }

    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(vk, src_image, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = device_->find_memory_type(
        mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vk, &ai, nullptr, &src_memory) != VK_SUCCESS) { cleanup(); return false; }
    vkBindImageMemory(vk, src_image, src_memory, 0);

    VkImageViewCreateInfo vi{};
    vi.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image            = src_image;
    vi.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    vi.format           = VK_FORMAT_R32G32B32A32_SFLOAT;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(vk, &vi, nullptr, &src_view) != VK_SUCCESS) { cleanup(); return false; }

    // -- upload the equirectangular data --
    {
        const VkDeviceSize bytes =
            static_cast<VkDeviceSize>(src_width) * src_height * 4 * sizeof(float);
        VkBuffer       sb  = VK_NULL_HANDLE;
        VkDeviceMemory sbm = VK_NULL_HANDLE;
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size  = bytes;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(vk, &bi, nullptr, &sb);
        VkMemoryRequirements smr{};
        vkGetBufferMemoryRequirements(vk, sb, &smr);
        VkMemoryAllocateInfo sai{};
        sai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        sai.allocationSize  = smr.size;
        sai.memoryTypeIndex = device_->find_memory_type(
            smr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(vk, &sai, nullptr, &sbm);
        vkBindBufferMemory(vk, sb, sbm, 0);
        void* mapped = nullptr;
        vkMapMemory(vk, sbm, 0, bytes, 0, &mapped);
        std::memcpy(mapped, rgba_data, bytes);
        vkUnmapMemory(vk, sbm);

        run_one_time_command(device_, [&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.image = src_image;
            b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            b.subresourceRange.levelCount = 1;
            b.subresourceRange.layerCount = 1;
            b.srcAccessMask = 0;
            b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
            VkBufferImageCopy r{};
            r.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            r.imageSubresource.layerCount = 1;
            r.imageExtent = { static_cast<uint32_t>(src_width),
                              static_cast<uint32_t>(src_height), 1 };
            vkCmdCopyBufferToImage(cmd, sb, src_image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
            b.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
        });
        vkDestroyBuffer(vk, sb, nullptr);
        vkFreeMemory(vk, sbm, nullptr);
    }

    // -- compute pipeline for the projection --
    VkSamplerCreateInfo si{};
    si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter    = VK_FILTER_LINEAR;
    si.minFilter    = VK_FILTER_LINEAR;
    si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;        // wrap horizontally
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; // clamp at poles
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(vk, &si, nullptr, &src_sampler) != VK_SUCCESS) { cleanup(); return false; }

    std::array<VkDescriptorSetLayoutBinding, 2> b{};
    b[0].binding         = 0;
    b[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[0].descriptorCount = 1;
    b[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    b[1].binding         = 1;
    b[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    b[1].descriptorCount = 1;
    b[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 2;
    li.pBindings    = b.data();
    if (vkCreateDescriptorSetLayout(vk, &li, nullptr, &dsl) != VK_SUCCESS) { cleanup(); return false; }

    VkPushConstantRange pr{};
    pr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pr.size       = sizeof(int32_t) * 3;
    VkPipelineLayoutCreateInfo pli{};
    pli.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount         = 1;
    pli.pSetLayouts            = &dsl;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges    = &pr;
    if (vkCreatePipelineLayout(vk, &pli, nullptr, &pl) != VK_SUCCESS) { cleanup(); return false; }

    VkShaderModuleCreateInfo smi{};
    smi.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = kEquirectToCubemapSpv_size;
    smi.pCode    = kEquirectToCubemapSpv;
    VkShaderModule sm = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vk, &smi, nullptr, &sm) != VK_SUCCESS) { cleanup(); return false; }
    VkComputePipelineCreateInfo cpi{};
    cpi.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpi.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.module = sm;
    cpi.stage.pName  = "main";
    cpi.layout       = pl;
    VkResult pres = vkCreateComputePipelines(vk, VK_NULL_HANDLE, 1, &cpi, nullptr, &pipe);
    vkDestroyShaderModule(vk, sm, nullptr);
    if (pres != VK_SUCCESS) { cleanup(); return false; }

    std::array<VkDescriptorPoolSize, 2> ps{};
    ps[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps[0].descriptorCount = 1;
    ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ps[1].descriptorCount = 1;
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets       = 1;
    pi.poolSizeCount = static_cast<uint32_t>(ps.size());
    pi.pPoolSizes    = ps.data();
    if (vkCreateDescriptorPool(vk, &pi, nullptr, &pool) != VK_SUCCESS) { cleanup(); return false; }
    VkDescriptorSetAllocateInfo dai{};
    dai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool     = pool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts        = &dsl;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(vk, &dai, &set) != VK_SUCCESS) { cleanup(); return false; }

    // Storage-image view of the cubemap.
    VkImageViewCreateInfo wvi{};
    wvi.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    wvi.image            = cubemap_image_;
    wvi.viewType         = VK_IMAGE_VIEW_TYPE_CUBE;
    wvi.format           = kCubemapFormat;
    wvi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    wvi.subresourceRange.levelCount = 1;
    wvi.subresourceRange.layerCount = 6;
    if (vkCreateImageView(vk, &wvi, nullptr, &writable_cube_view) != VK_SUCCESS) { cleanup(); return false; }

    VkDescriptorImageInfo s_info{};
    s_info.sampler     = src_sampler;
    s_info.imageView   = src_view;
    s_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorImageInfo d_info{};
    d_info.imageView   = writable_cube_view;
    d_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    std::array<VkWriteDescriptorSet, 2> ws{};
    ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[0].dstSet = set;
    ws[0].dstBinding = 0; ws[0].descriptorCount = 1;
    ws[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ws[0].pImageInfo = &s_info;
    ws[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[1].dstSet = set;
    ws[1].dstBinding = 1; ws[1].descriptorCount = 1;
    ws[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; ws[1].pImageInfo = &d_info;
    vkUpdateDescriptorSets(vk, 2, ws.data(), 0, nullptr);

    // -- dispatch once per face --
    run_one_time_command(device_, [&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier ib{};
        ib.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        ib.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ib.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        ib.image = cubemap_image_;
        ib.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ib.subresourceRange.levelCount = 1;
        ib.subresourceRange.layerCount = 6;
        ib.srcAccessMask = 0;
        ib.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &ib);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pl, 0, 1, &set, 0, nullptr);
        const uint32_t groups = (face_size_ + 7) / 8;
        for (int32_t face = 0; face < 6; ++face) {
            int32_t pc[3] = {
                static_cast<int32_t>(face_size_),
                static_cast<int32_t>(face_size_),
                face
            };
            vkCmdPushConstants(cmd, pl, VK_SHADER_STAGE_COMPUTE_BIT,
                               0, sizeof(pc), pc);
            vkCmdDispatch(cmd, groups, groups, 1);
        }

        ib.oldLayout     = VK_IMAGE_LAYOUT_GENERAL;
        ib.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ib.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        ib.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &ib);
    });

    ok = true;
    cleanup();
    return ok;
}

// ---- IBL bake -------------------------------------------------------------

namespace {

// Cube image with N mips, usage = sampled + storage + transfer_dst.
struct CubeAlloc {
    VkImage        image  = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

bool create_cube_storage(VulkanDevice* device, uint32_t face_size, uint32_t mips,
                          VkFormat format, CubeAlloc& out) {
    VkDevice vk = device->get_device();
    VkImageCreateInfo ii{};
    ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType     = VK_IMAGE_TYPE_2D;
    ii.format        = format;
    ii.extent        = { face_size, face_size, 1 };
    ii.mipLevels     = mips;
    ii.arrayLayers   = 6;
    ii.samples       = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ii.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ii.flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(vk, &ii, nullptr, &out.image) != VK_SUCCESS) return false;
    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(vk, out.image, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = device->find_memory_type(mr.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vk, &ai, nullptr, &out.memory) != VK_SUCCESS) return false;
    vkBindImageMemory(vk, out.image, out.memory, 0);
    return true;
}

VkImageView create_cube_view(VkDevice vk, VkImage img, VkFormat format,
                              uint32_t base_mip, uint32_t mip_count) {
    VkImageViewCreateInfo vi{};
    vi.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image                       = img;
    vi.viewType                    = VK_IMAGE_VIEW_TYPE_CUBE;
    vi.format                      = format;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.baseMipLevel = base_mip;
    vi.subresourceRange.levelCount   = mip_count;
    vi.subresourceRange.layerCount   = 6;
    VkImageView v = VK_NULL_HANDLE;
    vkCreateImageView(vk, &vi, nullptr, &v);
    return v;
}

void transition_cube(VkCommandBuffer cmd, VkImage img,
                     uint32_t base_mip, uint32_t mip_count,
                     VkImageLayout from, VkImageLayout to,
                     VkAccessFlags src_access, VkAccessFlags dst_access,
                     VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage) {
    VkImageMemoryBarrier b{};
    b.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout                   = from;
    b.newLayout                   = to;
    b.image                       = img;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = base_mip;
    b.subresourceRange.levelCount   = mip_count;
    b.subresourceRange.layerCount   = 6;
    b.srcAccessMask = src_access;
    b.dstAccessMask = dst_access;
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage,
                         0, 0, nullptr, 0, nullptr, 1, &b);
}

// Compute pipeline owning the descriptor set layout + pool + pipeline.
struct ComputeBake {
    VkDescriptorSetLayout dsl  = VK_NULL_HANDLE;
    VkPipelineLayout      pl   = VK_NULL_HANDLE;
    VkPipeline            pipe = VK_NULL_HANDLE;
    VkDescriptorPool      pool = VK_NULL_HANDLE;

    bool build(VulkanDevice* device, const uint32_t* spv, uint32_t spv_size,
               VkDescriptorSetLayoutBinding* bindings, uint32_t binding_count,
               uint32_t push_constant_size, uint32_t max_sets) {
        VkDevice vk = device->get_device();
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = binding_count;
        li.pBindings    = bindings;
        if (vkCreateDescriptorSetLayout(vk, &li, nullptr, &dsl) != VK_SUCCESS) return false;
        VkPushConstantRange pr{};
        pr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pr.size       = push_constant_size;
        VkPipelineLayoutCreateInfo pli{};
        pli.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount         = 1;
        pli.pSetLayouts            = &dsl;
        pli.pushConstantRangeCount = push_constant_size > 0 ? 1 : 0;
        pli.pPushConstantRanges    = push_constant_size > 0 ? &pr : nullptr;
        if (vkCreatePipelineLayout(vk, &pli, nullptr, &pl) != VK_SUCCESS) return false;
        VkShaderModuleCreateInfo smi{};
        smi.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smi.codeSize = spv_size;
        smi.pCode    = spv;
        VkShaderModule sm = VK_NULL_HANDLE;
        if (vkCreateShaderModule(vk, &smi, nullptr, &sm) != VK_SUCCESS) return false;
        VkComputePipelineCreateInfo cpi{};
        cpi.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpi.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpi.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        cpi.stage.module = sm;
        cpi.stage.pName  = "main";
        cpi.layout       = pl;
        VkResult r = vkCreateComputePipelines(vk, VK_NULL_HANDLE, 1, &cpi, nullptr, &pipe);
        vkDestroyShaderModule(vk, sm, nullptr);
        if (r != VK_SUCCESS) return false;

        // One pool sized for max_sets sets across all binding types we need.
        std::array<VkDescriptorPoolSize, 2> ps{};
        ps[0].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps[0].descriptorCount = max_sets * 2;
        ps[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        ps[1].descriptorCount = max_sets * 2;
        VkDescriptorPoolCreateInfo pi{};
        pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets       = max_sets;
        pi.poolSizeCount = static_cast<uint32_t>(ps.size());
        pi.pPoolSizes    = ps.data();
        return vkCreateDescriptorPool(vk, &pi, nullptr, &pool) == VK_SUCCESS;
    }

    VkDescriptorSet allocate(VkDevice vk) const {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &dsl;
        VkDescriptorSet s = VK_NULL_HANDLE;
        vkAllocateDescriptorSets(vk, &ai, &s);
        return s;
    }

    void destroy(VkDevice vk) {
        if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(vk, pool, nullptr);
        if (pipe != VK_NULL_HANDLE) vkDestroyPipeline(vk, pipe, nullptr);
        if (pl   != VK_NULL_HANDLE) vkDestroyPipelineLayout(vk, pl, nullptr);
        if (dsl  != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(vk, dsl, nullptr);
        *this = {};
    }
};

// Bindings for the cubemap-source compute shaders (binding 0 = source
// cubemap sampler, binding 1 = destination storage cube view).
std::array<VkDescriptorSetLayoutBinding, 2> cube_in_cube_out_bindings() {
    std::array<VkDescriptorSetLayoutBinding, 2> b{};
    b[0].binding         = 0;
    b[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[0].descriptorCount = 1;
    b[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    b[1].binding         = 1;
    b[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    b[1].descriptorCount = 1;
    b[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    return b;
}

} // anonymous namespace

bool VulkanEnvironmentMap::bake_ibl(uint32_t irradiance_size,
                                    uint32_t prefilter_size,
                                    uint32_t prefilter_mips,
                                    uint32_t brdf_lut_size) {
    if (ibl_baked_) return true;
    if (cubemap_image_ == VK_NULL_HANDLE) {
        spdlog::error("VulkanEnvironmentMap::bake_ibl: base cubemap not built");
        return false;
    }
    if (!bake_irradiance(irradiance_size)) return false;
    if (!bake_prefiltered(prefilter_size, prefilter_mips)) return false;
    if (!bake_brdf_lut(brdf_lut_size)) return false;
    prefilter_mips_ = prefilter_mips;
    ibl_baked_ = true;
    spdlog::info("VulkanEnvironmentMap: IBL baked (irradiance={}^2, prefilter={}^2 x {} mips, brdf_lut={}^2)",
                 irradiance_size, prefilter_size, prefilter_mips, brdf_lut_size);
    return true;
}

bool VulkanEnvironmentMap::bake_irradiance(uint32_t size) {
    VkDevice vk = device_->get_device();
    CubeAlloc img;
    if (!create_cube_storage(device_, size, 1, kCubemapFormat, img)) return false;
    irradiance_image_  = img.image;
    irradiance_memory_ = img.memory;
    irradiance_view_   = create_cube_view(vk, img.image, kCubemapFormat, 0, 1);
    if (irradiance_view_ == VK_NULL_HANDLE) return false;

    auto bindings = cube_in_cube_out_bindings();
    ComputeBake cb;
    if (!cb.build(device_, kIblIrradianceSpv, kIblIrradianceSpv_size,
                  bindings.data(), 2, sizeof(int32_t) * 3, 1)) {
        cb.destroy(vk); return false;
    }
    VkDescriptorSet set = cb.allocate(vk);
    VkDescriptorImageInfo src_info{};
    src_info.sampler     = sampler_;
    src_info.imageView   = cubemap_view_;
    src_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorImageInfo dst_info{};
    dst_info.imageView   = irradiance_view_;
    dst_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    std::array<VkWriteDescriptorSet, 2> ws{};
    ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[0].dstSet = set;
    ws[0].dstBinding = 0; ws[0].descriptorCount = 1;
    ws[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ws[0].pImageInfo = &src_info;
    ws[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[1].dstSet = set;
    ws[1].dstBinding = 1; ws[1].descriptorCount = 1;
    ws[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; ws[1].pImageInfo = &dst_info;
    vkUpdateDescriptorSets(vk, 2, ws.data(), 0, nullptr);

    run_one_time_command(device_, [&](VkCommandBuffer cmd) {
        transition_cube(cmd, irradiance_image_, 0, 1,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                        0, VK_ACCESS_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cb.pipe);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                cb.pl, 0, 1, &set, 0, nullptr);
        const uint32_t groups = (size + 7) / 8;
        for (int32_t face = 0; face < 6; ++face) {
            int32_t pc[3] = {
                static_cast<int32_t>(size),
                static_cast<int32_t>(size),
                face
            };
            vkCmdPushConstants(cmd, cb.pl, VK_SHADER_STAGE_COMPUTE_BIT,
                               0, sizeof(pc), pc);
            vkCmdDispatch(cmd, groups, groups, 1);
        }
        transition_cube(cmd, irradiance_image_, 0, 1,
                        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    });
    cb.destroy(vk);
    return true;
}

bool VulkanEnvironmentMap::bake_prefiltered(uint32_t size, uint32_t mips) {
    VkDevice vk = device_->get_device();
    CubeAlloc img;
    if (!create_cube_storage(device_, size, mips, kCubemapFormat, img)) return false;
    prefiltered_image_  = img.image;
    prefiltered_memory_ = img.memory;
    prefiltered_view_   = create_cube_view(vk, img.image, kCubemapFormat, 0, mips);
    if (prefiltered_view_ == VK_NULL_HANDLE) return false;

    // Per-mip writable views (each compute dispatch writes one mip layer).
    std::vector<VkImageView> mip_views(mips, VK_NULL_HANDLE);
    for (uint32_t m = 0; m < mips; ++m) {
        mip_views[m] = create_cube_view(vk, img.image, kCubemapFormat, m, 1);
    }

    auto bindings = cube_in_cube_out_bindings();
    ComputeBake cb;
    if (!cb.build(device_, kIblPrefilterEnvSpv, kIblPrefilterEnvSpv_size,
                  bindings.data(), 2, sizeof(int32_t) * 3 + sizeof(float), mips)) {
        for (auto v : mip_views) if (v != VK_NULL_HANDLE) vkDestroyImageView(vk, v, nullptr);
        cb.destroy(vk); return false;
    }

    std::vector<VkDescriptorSet> sets(mips, VK_NULL_HANDLE);
    for (uint32_t m = 0; m < mips; ++m) {
        sets[m] = cb.allocate(vk);
        VkDescriptorImageInfo src_info{};
        src_info.sampler     = sampler_;
        src_info.imageView   = cubemap_view_;
        src_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorImageInfo dst_info{};
        dst_info.imageView   = mip_views[m];
        dst_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        std::array<VkWriteDescriptorSet, 2> ws{};
        ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[0].dstSet = sets[m];
        ws[0].dstBinding = 0; ws[0].descriptorCount = 1;
        ws[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ws[0].pImageInfo = &src_info;
        ws[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; ws[1].dstSet = sets[m];
        ws[1].dstBinding = 1; ws[1].descriptorCount = 1;
        ws[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; ws[1].pImageInfo = &dst_info;
        vkUpdateDescriptorSets(vk, 2, ws.data(), 0, nullptr);
    }

    run_one_time_command(device_, [&](VkCommandBuffer cmd) {
        transition_cube(cmd, prefiltered_image_, 0, mips,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                        0, VK_ACCESS_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cb.pipe);
        for (uint32_t m = 0; m < mips; ++m) {
            const uint32_t mip_size = std::max(1u, size >> m);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    cb.pl, 0, 1, &sets[m], 0, nullptr);
            const float roughness = (mips > 1) ? float(m) / float(mips - 1) : 0.0f;
            const uint32_t groups = (mip_size + 7) / 8;
            for (int32_t face = 0; face < 6; ++face) {
                struct PC {
                    int32_t w, h, face_idx;
                    float roughness;
                } pc;
                pc.w = static_cast<int32_t>(mip_size);
                pc.h = static_cast<int32_t>(mip_size);
                pc.face_idx = face;
                pc.roughness = roughness;
                vkCmdPushConstants(cmd, cb.pl, VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, groups, groups, 1);
            }
        }
        transition_cube(cmd, prefiltered_image_, 0, mips,
                        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    });
    for (auto v : mip_views) if (v != VK_NULL_HANDLE) vkDestroyImageView(vk, v, nullptr);
    cb.destroy(vk);
    return true;
}

bool VulkanEnvironmentMap::bake_brdf_lut(uint32_t size) {
    VkDevice vk = device_->get_device();

    // 2D image, R16G16_SFLOAT, storage + sampled.
    VkImageCreateInfo ii{};
    ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType     = VK_IMAGE_TYPE_2D;
    ii.format        = VK_FORMAT_R16G16_SFLOAT;
    ii.extent        = { size, size, 1 };
    ii.mipLevels     = 1;
    ii.arrayLayers   = 1;
    ii.samples       = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ii.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(vk, &ii, nullptr, &brdf_lut_image_) != VK_SUCCESS) return false;
    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(vk, brdf_lut_image_, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = device_->find_memory_type(mr.memoryTypeBits,
                                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vk, &ai, nullptr, &brdf_lut_memory_) != VK_SUCCESS) return false;
    vkBindImageMemory(vk, brdf_lut_image_, brdf_lut_memory_, 0);

    VkImageViewCreateInfo vi{};
    vi.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image            = brdf_lut_image_;
    vi.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    vi.format           = VK_FORMAT_R16G16_SFLOAT;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(vk, &vi, nullptr, &brdf_lut_view_) != VK_SUCCESS) return false;

    // Linear-clamp sampler for the LUT.
    VkSamplerCreateInfo si{};
    si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter    = VK_FILTER_LINEAR;
    si.minFilter    = VK_FILTER_LINEAR;
    si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(vk, &si, nullptr, &brdf_lut_sampler_) != VK_SUCCESS) return false;

    // Compute pipeline — only 1 storage-image binding.
    VkDescriptorSetLayoutBinding lb{};
    lb.binding         = 0;
    lb.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    lb.descriptorCount = 1;
    lb.stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    ComputeBake cb;
    if (!cb.build(device_, kIblBrdfLutSpv, kIblBrdfLutSpv_size,
                  &lb, 1, sizeof(int32_t) * 2, 1)) {
        cb.destroy(vk); return false;
    }
    VkDescriptorSet set = cb.allocate(vk);
    VkDescriptorImageInfo dst_info{};
    dst_info.imageView   = brdf_lut_view_;
    dst_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = set;
    w.dstBinding      = 0;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w.pImageInfo      = &dst_info;
    vkUpdateDescriptorSets(vk, 1, &w, 0, nullptr);

    run_one_time_command(device_, [&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier b{};
        b.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout                   = VK_IMAGE_LAYOUT_GENERAL;
        b.image                       = brdf_lut_image_;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.layerCount = 1;
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cb.pipe);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                cb.pl, 0, 1, &set, 0, nullptr);
        int32_t pc[2] = { static_cast<int32_t>(size), static_cast<int32_t>(size) };
        vkCmdPushConstants(cmd, cb.pl, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(pc), pc);
        const uint32_t groups = (size + 7) / 8;
        vkCmdDispatch(cmd, groups, groups, 1);
        b.oldLayout     = VK_IMAGE_LAYOUT_GENERAL;
        b.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
    });
    cb.destroy(vk);
    return true;
}

} // namespace gws::renderer::gpu
