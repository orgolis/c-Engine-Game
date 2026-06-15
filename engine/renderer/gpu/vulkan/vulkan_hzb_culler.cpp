/**
 * @file vulkan_hzb_culler.cpp
 * @brief Implementation of HZB occlusion culling.
 */

#include "vulkan_hzb_culler.h"
#include "vulkan_device.h"
#include "hzb_downsample_spirv.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace gws::renderer::gpu {

std::unique_ptr<VulkanHzbCuller>
VulkanHzbCuller::create(VulkanDevice* device, const Config& cfg) {
    if (device == nullptr) {
        spdlog::error("VulkanHzbCuller::create: null device");
        return nullptr;
    }
    if (cfg.depth_width == 0 || cfg.depth_height == 0) {
        spdlog::error("VulkanHzbCuller::create: zero depth extents");
        return nullptr;
    }
    auto c = std::unique_ptr<VulkanHzbCuller>(new VulkanHzbCuller());
    if (!c->initialize(device, cfg)) {
        return nullptr;
    }
    spdlog::info("VulkanHzbCuller created ({}x{} HZB, {} mips, readback mip={})",
                 c->hzb_width_, c->hzb_height_, c->mip_count_, cfg.readback_mip);
    return c;
}

VulkanHzbCuller::~VulkanHzbCuller() { destroy(); }

bool VulkanHzbCuller::initialize(VulkanDevice* device, const Config& cfg) {
    device_ = device;
    config_ = cfg;

    // HZB[0] is half-res of the depth buffer.
    hzb_width_  = std::max(1u, cfg.depth_width  / 2);
    hzb_height_ = std::max(1u, cfg.depth_height / 2);

    const uint32_t max_dim = std::max(hzb_width_, hzb_height_);
    mip_count_ = static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(max_dim)))) + 1;
    // Cap to a sane number; mips beyond 12 give us 1x1 tiles anyway.
    mip_count_ = std::min(mip_count_, 12u);
    // readback_mip must exist in the chain.
    if (config_.readback_mip >= mip_count_) {
        config_.readback_mip = mip_count_ - 1;
    }

    visibility_.assign(cfg.max_draws, 1);

    if (!create_hzb_image()) return false;
    if (!create_pipeline()) return false;
    if (!create_descriptor_sets()) return false;
    if (!create_readback_buffer()) return false;
    return true;
}

void VulkanHzbCuller::destroy() {
    if (device_ == nullptr) return;
    VkDevice vk = device_->get_device();

    if (pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(vk, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (pipeline_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(vk, pipeline_layout_, nullptr); pipeline_layout_ = VK_NULL_HANDLE; }
    if (descriptor_pool_ != VK_NULL_HANDLE) { vkDestroyDescriptorPool(vk, descriptor_pool_, nullptr); descriptor_pool_ = VK_NULL_HANDLE; descriptor_sets_.clear(); }
    if (descriptor_set_layout_ != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(vk, descriptor_set_layout_, nullptr); descriptor_set_layout_ = VK_NULL_HANDLE; }

    if (hzb_sampler_ != VK_NULL_HANDLE) { vkDestroySampler(vk, hzb_sampler_, nullptr); hzb_sampler_ = VK_NULL_HANDLE; }
    if (depth_sampler_ != VK_NULL_HANDLE) { vkDestroySampler(vk, depth_sampler_, nullptr); depth_sampler_ = VK_NULL_HANDLE; }

    if (hzb_sample_view_ != VK_NULL_HANDLE) { vkDestroyImageView(vk, hzb_sample_view_, nullptr); hzb_sample_view_ = VK_NULL_HANDLE; }
    for (auto v : hzb_mip_views_) if (v != VK_NULL_HANDLE) vkDestroyImageView(vk, v, nullptr);
    hzb_mip_views_.clear();
    if (hzb_image_ != VK_NULL_HANDLE) { vkDestroyImage(vk, hzb_image_, nullptr); hzb_image_ = VK_NULL_HANDLE; }
    if (hzb_memory_ != VK_NULL_HANDLE) { vkFreeMemory(vk, hzb_memory_, nullptr); hzb_memory_ = VK_NULL_HANDLE; }

    if (readback_buffer_ != VK_NULL_HANDLE) {
        if (readback_mapped_ != nullptr) {
            vkUnmapMemory(vk, readback_memory_);
            readback_mapped_ = nullptr;
        }
        vkDestroyBuffer(vk, readback_buffer_, nullptr); readback_buffer_ = VK_NULL_HANDLE;
    }
    if (readback_memory_ != VK_NULL_HANDLE) { vkFreeMemory(vk, readback_memory_, nullptr); readback_memory_ = VK_NULL_HANDLE; }

    device_ = nullptr;
}

bool VulkanHzbCuller::create_hzb_image() {
    VkDevice vk = device_->get_device();

    VkImageCreateInfo ii{};
    ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType     = VK_IMAGE_TYPE_2D;
    ii.format        = VK_FORMAT_R32_SFLOAT;
    ii.extent        = { hzb_width_, hzb_height_, 1 };
    ii.mipLevels     = mip_count_;
    ii.arrayLayers   = 1;
    ii.samples       = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ii.usage         = VK_IMAGE_USAGE_STORAGE_BIT
                     | VK_IMAGE_USAGE_SAMPLED_BIT
                     | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(vk, &ii, nullptr, &hzb_image_) != VK_SUCCESS) {
        spdlog::error("VulkanHzbCuller: failed to create HZB image");
        return false;
    }

    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(vk, hzb_image_, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = device_->find_memory_type(
        mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vk, &ai, nullptr, &hzb_memory_) != VK_SUCCESS) {
        spdlog::error("VulkanHzbCuller: failed to allocate HZB memory"); return false;
    }
    vkBindImageMemory(vk, hzb_image_, hzb_memory_, 0);

    // Per-mip storage views.
    hzb_mip_views_.assign(mip_count_, VK_NULL_HANDLE);
    for (uint32_t m = 0; m < mip_count_; ++m) {
        VkImageViewCreateInfo vi{};
        vi.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image            = hzb_image_;
        vi.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        vi.format           = VK_FORMAT_R32_SFLOAT;
        vi.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.baseMipLevel   = m;
        vi.subresourceRange.levelCount     = 1;
        vi.subresourceRange.layerCount     = 1;
        if (vkCreateImageView(vk, &vi, nullptr, &hzb_mip_views_[m]) != VK_SUCCESS) {
            spdlog::error("VulkanHzbCuller: failed to create HZB mip {} view", m);
            return false;
        }
    }

    // Full-chain sample view (for sampling all mips via a single binding).
    {
        VkImageViewCreateInfo vi{};
        vi.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image            = hzb_image_;
        vi.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        vi.format           = VK_FORMAT_R32_SFLOAT;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = mip_count_;
        vi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(vk, &vi, nullptr, &hzb_sample_view_) != VK_SUCCESS) {
            spdlog::error("VulkanHzbCuller: failed to create HZB sample view");
            return false;
        }
    }

    // Samplers (point, clamp-to-edge — we don't want fancy filtering for occlusion).
    VkSamplerCreateInfo sa{};
    sa.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sa.magFilter    = VK_FILTER_NEAREST;
    sa.minFilter    = VK_FILTER_NEAREST;
    sa.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sa.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sa.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sa.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(vk, &sa, nullptr, &hzb_sampler_) != VK_SUCCESS) return false;
    if (vkCreateSampler(vk, &sa, nullptr, &depth_sampler_) != VK_SUCCESS) return false;
    return true;
}

bool VulkanHzbCuller::create_pipeline() {
    VkDevice vk = device_->get_device();

    // Descriptor set: one combined-image-sampler (src), one storage image (dst).
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
    li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = static_cast<uint32_t>(b.size());
    li.pBindings    = b.data();
    if (vkCreateDescriptorSetLayout(vk, &li, nullptr, &descriptor_set_layout_) != VK_SUCCESS) {
        spdlog::error("VulkanHzbCuller: failed to create DSL"); return false;
    }

    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pc.offset     = 0;
    pc.size       = sizeof(int32_t) * 2;  // ivec2 dst_size

    VkPipelineLayoutCreateInfo pli{};
    pli.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount         = 1;
    pli.pSetLayouts            = &descriptor_set_layout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges    = &pc;
    if (vkCreatePipelineLayout(vk, &pli, nullptr, &pipeline_layout_) != VK_SUCCESS) {
        spdlog::error("VulkanHzbCuller: failed to create pipeline layout"); return false;
    }

    VkShaderModuleCreateInfo smi{};
    smi.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = kHzbDownsampleSpv_size;
    smi.pCode    = kHzbDownsampleSpv;
    VkShaderModule sm = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vk, &smi, nullptr, &sm) != VK_SUCCESS) {
        spdlog::error("VulkanHzbCuller: failed to create shader module"); return false;
    }

    VkComputePipelineCreateInfo cpi{};
    cpi.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpi.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.module = sm;
    cpi.stage.pName  = "main";
    cpi.layout       = pipeline_layout_;
    VkResult pres = vkCreateComputePipelines(vk, VK_NULL_HANDLE, 1, &cpi, nullptr, &pipeline_);
    vkDestroyShaderModule(vk, sm, nullptr);
    if (pres != VK_SUCCESS) {
        spdlog::error("VulkanHzbCuller: failed to create compute pipeline"); return false;
    }
    return true;
}

bool VulkanHzbCuller::create_descriptor_sets() {
    VkDevice vk = device_->get_device();

    std::array<VkDescriptorPoolSize, 2> ps{};
    ps[0].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps[0].descriptorCount = mip_count_;
    ps[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ps[1].descriptorCount = mip_count_;

    VkDescriptorPoolCreateInfo pi{};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets       = mip_count_;
    pi.poolSizeCount = static_cast<uint32_t>(ps.size());
    pi.pPoolSizes    = ps.data();
    if (vkCreateDescriptorPool(vk, &pi, nullptr, &descriptor_pool_) != VK_SUCCESS) {
        spdlog::error("VulkanHzbCuller: failed to create descriptor pool"); return false;
    }

    std::vector<VkDescriptorSetLayout> layouts(mip_count_, descriptor_set_layout_);
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = descriptor_pool_;
    ai.descriptorSetCount = mip_count_;
    ai.pSetLayouts        = layouts.data();
    descriptor_sets_.assign(mip_count_, VK_NULL_HANDLE);
    if (vkAllocateDescriptorSets(vk, &ai, descriptor_sets_.data()) != VK_SUCCESS) {
        spdlog::error("VulkanHzbCuller: failed to allocate descriptor sets"); return false;
    }
    return true;
}

void VulkanHzbCuller::write_descriptor_for_mip(uint32_t mip) {
    // Mip 0: src = depth view (point sampler), dst = HZB[0] view.
    // Mip k>0: src = HZB[k-1] view, dst = HZB[k] view.
    VkDescriptorImageInfo src_info{};
    if (mip == 0) {
        src_info.sampler     = depth_sampler_;
        src_info.imageView   = depth_view_;
        src_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    } else {
        src_info.sampler     = hzb_sampler_;
        src_info.imageView   = hzb_mip_views_[mip - 1];
        src_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    }

    VkDescriptorImageInfo dst_info{};
    dst_info.imageView   = hzb_mip_views_[mip];
    dst_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    std::array<VkWriteDescriptorSet, 2> ws{};
    ws[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ws[0].dstSet          = descriptor_sets_[mip];
    ws[0].dstBinding      = 0;
    ws[0].descriptorCount = 1;
    ws[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ws[0].pImageInfo      = &src_info;
    ws[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ws[1].dstSet          = descriptor_sets_[mip];
    ws[1].dstBinding      = 1;
    ws[1].descriptorCount = 1;
    ws[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ws[1].pImageInfo      = &dst_info;
    vkUpdateDescriptorSets(device_->get_device(), 2, ws.data(), 0, nullptr);
}

void VulkanHzbCuller::set_depth_view(VkImageView depth_view) {
    if (depth_view == depth_view_) return;
    depth_view_ = depth_view;
    if (depth_view_ != VK_NULL_HANDLE) {
        // Update mip 0's descriptor set to point at the new depth view.
        write_descriptor_for_mip(0);
        // Mip 1..N descriptor sets need a one-time write (HZB views don't
        // change). Do them all here for simplicity.
        for (uint32_t m = 1; m < mip_count_; ++m) {
            write_descriptor_for_mip(m);
        }
    }
}

bool VulkanHzbCuller::create_readback_buffer() {
    readback_width_  = std::max(1u, hzb_width_  >> config_.readback_mip);
    readback_height_ = std::max(1u, hzb_height_ >> config_.readback_mip);

    const VkDeviceSize bytes =
        static_cast<VkDeviceSize>(readback_width_) *
        static_cast<VkDeviceSize>(readback_height_) * sizeof(float);

    VkDevice vk = device_->get_device();

    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = bytes;
    bi.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vk, &bi, nullptr, &readback_buffer_) != VK_SUCCESS) return false;

    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(vk, readback_buffer_, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = device_->find_memory_type(
        mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(vk, &ai, nullptr, &readback_memory_) != VK_SUCCESS) return false;
    vkBindBufferMemory(vk, readback_buffer_, readback_memory_, 0);
    if (vkMapMemory(vk, readback_memory_, 0, bytes, 0, &readback_mapped_) != VK_SUCCESS) return false;

    cpu_hzb_.assign(static_cast<size_t>(readback_width_) * readback_height_, 1.0f);
    return true;
}

void VulkanHzbCuller::build_and_readback(VkCommandBuffer cmd) {
    if (!enabled_ || cmd == VK_NULL_HANDLE || depth_view_ == VK_NULL_HANDLE ||
        pipeline_ == VK_NULL_HANDLE) {
        return;
    }

    // Transition all HZB mips from whatever layout they were in to GENERAL
    // for compute write. (UNDEFINED initially; SHADER_READ_ONLY after a
    // previous readback's TRANSFER_SRC step.)
    VkImageMemoryBarrier barr_to_general{};
    barr_to_general.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barr_to_general.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
    barr_to_general.newLayout        = VK_IMAGE_LAYOUT_GENERAL;
    barr_to_general.srcAccessMask    = 0;
    barr_to_general.dstAccessMask    = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    barr_to_general.image            = hzb_image_;
    barr_to_general.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barr_to_general.subresourceRange.baseMipLevel = 0;
    barr_to_general.subresourceRange.levelCount   = mip_count_;
    barr_to_general.subresourceRange.layerCount   = 1;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barr_to_general);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);

    for (uint32_t m = 0; m < mip_count_; ++m) {
        // The src for mip m is either the depth (m==0) or HZB[m-1]. For
        // m>0 we need a write→read barrier on the previous mip.
        if (m > 0) {
            VkImageMemoryBarrier b{};
            b.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout     = VK_IMAGE_LAYOUT_GENERAL;
            b.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
            b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b.image         = hzb_image_;
            b.subresourceRange.aspectMask   = VK_IMAGE_ASPECT_COLOR_BIT;
            b.subresourceRange.baseMipLevel = m - 1;
            b.subresourceRange.levelCount   = 1;
            b.subresourceRange.layerCount   = 1;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
        }

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline_layout_, 0, 1,
                                &descriptor_sets_[m], 0, nullptr);

        int32_t pc[2] = {
            static_cast<int32_t>(std::max(1u, hzb_width_  >> m)),
            static_cast<int32_t>(std::max(1u, hzb_height_ >> m)),
        };
        vkCmdPushConstants(cmd, pipeline_layout_,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), pc);

        const uint32_t gx = (pc[0] + 7) / 8;
        const uint32_t gy = (pc[1] + 7) / 8;
        vkCmdDispatch(cmd, gx, gy, 1);
    }

    // After build, transition the readback mip to TRANSFER_SRC for copy.
    {
        VkImageMemoryBarrier b{};
        b.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout     = VK_IMAGE_LAYOUT_GENERAL;
        b.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b.image         = hzb_image_;
        b.subresourceRange.aspectMask   = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.baseMipLevel = config_.readback_mip;
        b.subresourceRange.levelCount   = 1;
        b.subresourceRange.layerCount   = 1;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
    }

    VkBufferImageCopy region{};
    region.bufferOffset                    = 0;
    region.bufferRowLength                 = 0;
    region.bufferImageHeight               = 0;
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = config_.readback_mip;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = 1;
    region.imageExtent = { readback_width_, readback_height_, 1 };
    vkCmdCopyImageToBuffer(cmd, hzb_image_,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback_buffer_, 1, &region);

    // Make the copy visible to host reads after the frame fence.
    VkBufferMemoryBarrier bb{};
    bb.sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    bb.buffer        = readback_buffer_;
    bb.size          = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT,
                         0, 0, nullptr, 1, &bb, 0, nullptr);
}

void VulkanHzbCuller::pull_readback() {
    if (!enabled_ || readback_mapped_ == nullptr) return;
    const size_t bytes = cpu_hzb_.size() * sizeof(float);
    std::memcpy(cpu_hzb_.data(), readback_mapped_, bytes);
    cpu_hzb_valid_ = true;
}

void VulkanHzbCuller::test_visibility(const glm::vec3* aabb_mins,
                                      const glm::vec3* aabb_maxs,
                                      size_t count,
                                      const glm::mat4& view_proj) {
    const size_t draw_count = std::min(count, static_cast<size_t>(config_.max_draws));

    if (visibility_.size() < draw_count) visibility_.assign(config_.max_draws, 1);

    if (!enabled_ || !cpu_hzb_valid_) {
        std::fill(visibility_.begin(), visibility_.begin() + draw_count, 1);
        last_visible_count_ = static_cast<uint32_t>(draw_count);
        last_culled_count_  = 0;
        have_results_       = false;
        return;
    }

    uint32_t visible = 0;
    uint32_t culled  = 0;
    const float w = static_cast<float>(readback_width_);
    const float h = static_cast<float>(readback_height_);

    for (size_t i = 0; i < draw_count; ++i) {
        const glm::vec3& mn = aabb_mins[i];
        const glm::vec3& mx = aabb_maxs[i];

        // Project all 8 AABB corners to clip space. Track min/max screen
        // coords and the minimum NDC depth (the closest point of the AABB
        // to the camera). If any corner is behind the near plane, treat as
        // visible — conservative when partially clipped.
        float min_x =  std::numeric_limits<float>::infinity();
        float min_y =  std::numeric_limits<float>::infinity();
        float max_x = -std::numeric_limits<float>::infinity();
        float max_y = -std::numeric_limits<float>::infinity();
        float min_z =  std::numeric_limits<float>::infinity();
        bool  behind_near = false;

        for (int c = 0; c < 8; ++c) {
            glm::vec4 corner(
                (c & 1) ? mx.x : mn.x,
                (c & 2) ? mx.y : mn.y,
                (c & 4) ? mx.z : mn.z,
                1.0f);
            glm::vec4 clip = view_proj * corner;
            if (clip.w <= 0.0f) { behind_near = true; break; }
            const float inv_w = 1.0f / clip.w;
            const float ndc_x = clip.x * inv_w;
            const float ndc_y = clip.y * inv_w;
            const float ndc_z = clip.z * inv_w;
            min_x = std::min(min_x, ndc_x);
            min_y = std::min(min_y, ndc_y);
            max_x = std::max(max_x, ndc_x);
            max_y = std::max(max_y, ndc_y);
            min_z = std::min(min_z, ndc_z);
        }

        if (behind_near) {
            visibility_[i] = 1;
            ++visible;
            continue;
        }

        // Clip AABB against the frustum sides; if entirely off-screen,
        // mark invisible (frustum culling already does this but the HZB
        // sample requires on-screen coords anyway).
        if (max_x < -1.0f || min_x > 1.0f || max_y < -1.0f || min_y > 1.0f ||
            min_z >  1.0f) {
            visibility_[i] = 0;
            ++culled;
            continue;
        }

        // NDC → [0,1] UV, then to readback texel coords.
        const float u0 = std::max(0.0f, (min_x * 0.5f + 0.5f)) * w;
        const float v0 = std::max(0.0f, (min_y * 0.5f + 0.5f)) * h;
        const float u1 = std::min(1.0f, (max_x * 0.5f + 0.5f)) * w;
        const float v1 = std::min(1.0f, (max_y * 0.5f + 0.5f)) * h;
        const int ix0 = std::clamp(static_cast<int>(std::floor(u0)), 0, static_cast<int>(readback_width_) - 1);
        const int iy0 = std::clamp(static_cast<int>(std::floor(v0)), 0, static_cast<int>(readback_height_) - 1);
        const int ix1 = std::clamp(static_cast<int>(std::floor(u1)), 0, static_cast<int>(readback_width_) - 1);
        const int iy1 = std::clamp(static_cast<int>(std::floor(v1)), 0, static_cast<int>(readback_height_) - 1);

        // The engine uses GLM with default (OpenGL) depth convention, so
        // ndc_z is in [-1, 1] while Vulkan stores depth values in [0, 1].
        // For ndc_z < 0 the AABB straddles Vulkan's near-clip plane —
        // treat as "definitely visible" (conservative).
        if (min_z < 0.0f) {
            visibility_[i] = 1;
            ++visible;
            continue;
        }

        // Sample the MAX (furthest) depth across the AABB's screen
        // footprint. If our nearest NDC depth exceeds that, we're behind
        // every visible pixel in the region → cull.
        float region_max = 0.0f;
        for (int y = iy0; y <= iy1; ++y) {
            for (int x = ix0; x <= ix1; ++x) {
                const float d = cpu_hzb_[y * readback_width_ + x];
                region_max = std::max(region_max, d);
            }
        }

        if (min_z > region_max + 1e-4f) {
            visibility_[i] = 0;
            ++culled;
        } else {
            visibility_[i] = 1;
            ++visible;
        }
    }

    last_visible_count_ = visible;
    last_culled_count_  = culled;
    have_results_       = true;
}

bool VulkanHzbCuller::was_visible(uint32_t draw_index) const {
    if (!enabled_ || !have_results_)      return true;
    if (draw_index >= visibility_.size()) return true;
    return visibility_[draw_index] != 0;
}

} // namespace gws::renderer::gpu
