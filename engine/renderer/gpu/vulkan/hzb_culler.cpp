#include "hzb_culler.h"
#include "../device.h"
#include "vulkan_buffer.h"
#include "vulkan_image.h"
#include "../render_graph.h"
#include <algorithm>
#include <glm/glm.hpp>

namespace gws {

HZBCuller::HZBCuller(Device* device, const Config& cfg)
    : device_(device), config_(cfg) {
}

HZBCuller::~HZBCuller() {
    if (hzb_build_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_->get_device(), hzb_build_pipeline_, nullptr);
    }
    if (hzb_build_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_->get_device(), hzb_build_layout_, nullptr);
    }
    if (hzb_descriptor_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_->get_device(), hzb_descriptor_layout_, nullptr);
    }
    if (hzb_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_->get_device(), hzb_view_, nullptr);
    }
}

void HZBCuller::initialize() {
    if (!config_.enabled) return;

    create_descriptor_layout();
    create_compute_pipeline();
    
    // Create indirect buffers
    VkBufferCreateInfo indirect_info{};
    indirect_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    indirect_info.size = config_.max_draw_calls * sizeof(VkDrawIndirectCommand);
    indirect_info.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    indirect_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    indirect_buffer_ = std::make_unique<VulkanBuffer>(device_, indirect_info.size,
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // Visible count buffer (for GPU counter)
    visible_count_buffer_ = std::make_unique<VulkanBuffer>(device_, sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}

void HZBCuller::create_descriptor_layout() {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

    // Binding 0: Input depth texture (sampler)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1: Output HZB texture (storage image)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = bindings.size();
    layout_info.pBindings = bindings.data();

    vkCreateDescriptorSetLayout(device_->get_device(), &layout_info, nullptr, &hzb_descriptor_layout_);
}

void HZBCuller::create_compute_pipeline() {
    // Load and compile HZB compute shader
    std::string shader_path = "engine/renderer/gpu/vulkan/shaders/hzb_build.comp";
    
    // Create pipeline layout with push constants
    VkPushConstantRange push_const_range{};
    push_const_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push_const_range.size = sizeof(glm::ivec4) * 2;  // src_size, dst_size, mip_level

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &hzb_descriptor_layout_;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_const_range;

    vkCreatePipelineLayout(device_->get_device(), &layout_info, nullptr, &hzb_build_layout_);

    // Pipeline creation deferred: requires shader compilation infrastructure
    // For now, mark as placeholder; full implementation uses ShaderCompiler
}

VkImage HZBCuller::build_hzb(VkCommandBuffer cmd, VkImage depth_texture, VkExtent2D depth_size) {
    if (!config_.enabled || !hzb_texture_) {
        // Create HZB texture if needed
        create_hzb_texture(depth_size);
    }

    // Transition depth texture to shader read
    VkImageMemoryBarrier depth_barrier{};
    depth_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    depth_barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depth_barrier.image = depth_texture;
    depth_barrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    depth_barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_WRITE_BIT;
    depth_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &depth_barrier);

    // Build mipmap chain (level 0 = depth texture, level 1..N = reduced size)
    // Each level 2x downsampled from previous
    // Deferred: full compute dispatch implementation

    // Transition back to depth attachment
    depth_barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depth_barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    depth_barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1, &depth_barrier);

    return hzb_texture_->get_image();
}

uint32_t HZBCuller::cull_draw_calls(
    VkCommandBuffer cmd,
    const std::vector<DrawCall>& draw_calls,
    const math::mat4& view_proj,
    VkBuffer& out_indirect_buffer) {

    if (!config_.enabled || !indirect_buffer_) {
        return 0;
    }

    // GPU-driven culling: dispatch compute shader to test visibility
    // For each DrawCall, test AABB against HZB and write to indirect buffer
    // Deferred: full GPU-side occlusion test implementation

    out_indirect_buffer = indirect_buffer_->get_handle();
    return static_cast<uint32_t>(draw_calls.size());  // Placeholder: all visible
}

void HZBCuller::create_hzb_texture(VkExtent2D initial_size) {
    // Create HZB texture with mipmap levels
    // Each level = initial_size / 2^level
    uint32_t mip_levels = std::min(config_.mip_levels,
        static_cast<uint32_t>(std::log2(std::max(initial_size.width, initial_size.height))));

    VkImageCreateInfo hzb_info{};
    hzb_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    hzb_info.imageType = VK_IMAGE_TYPE_2D;
    hzb_info.format = VK_FORMAT_R32_SFLOAT;
    hzb_info.extent = {initial_size.width, initial_size.height, 1};
    hzb_info.mipLevels = mip_levels;
    hzb_info.arrayLayers = 1;
    hzb_info.samples = VK_SAMPLE_COUNT_1_BIT;
    hzb_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    hzb_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    hzb_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    hzb_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    hzb_texture_ = std::make_unique<VulkanImage>(device_, hzb_info);

    // Create image view for the entire mipmap chain
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = hzb_texture_->get_image();
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R32_SFLOAT;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels, 0, 1};

    vkCreateImageView(device_->get_device(), &view_info, nullptr, &hzb_view_);
}

void HZBCuller::build_hzb_mipmap_level(VkCommandBuffer cmd, uint32_t src_level, uint32_t dst_level) {
    // Dispatch compute shader to build dst_level from src_level
    // src_level downsampled by 2x in each dimension → dst_level
    // Deferred: full compute dispatch with descriptor set binding
}

}  // namespace gws
