#include "hzb_culler.h"
#include "../device.h"
#include "vulkan_buffer.h"
#include "vulkan_image.h"
#include "../render_graph.h"
#include "../../scene/include/entity.h"
#include "../../scene/include/scene.h"
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace gws {

// DrawCall structure for indirect rendering
struct DrawCall {
    glm::vec3 aabb_min;
    glm::vec3 aabb_max;
    uint32_t vertex_count;
    uint32_t instance_count;
    uint32_t first_vertex;
    uint32_t first_instance;
};

HZBCuller::HZBCuller(Device* device, const Config& cfg)
    : device_(device), config_(cfg) {
}

HZBCuller::~HZBCuller() {
    if (hzb_build_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_->get_device(), hzb_build_pipeline_, nullptr);
    }
    if (hzb_test_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_->get_device(), hzb_test_pipeline_, nullptr);
    }
    if (hzb_build_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_->get_device(), hzb_build_layout_, nullptr);
    }
    if (hzb_test_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_->get_device(), hzb_test_layout_, nullptr);
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
    create_compute_pipelines();
    
    // Create indirect buffers
    indirect_buffer_ = std::make_unique<VulkanBuffer>(device_, 
        config_.max_draw_calls * sizeof(VkDrawIndirectCommand),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    visible_count_buffer_ = std::make_unique<VulkanBuffer>(device_, sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // AABB buffer for culling
    aabb_buffer_ = std::make_unique<VulkanBuffer>(device_,
        config_.max_draw_calls * sizeof(glm::vec4) * 2,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
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

void HZBCuller::create_compute_pipelines() {
    // Create pipeline layout with push constants for HZB build
    VkPushConstantRange push_const_range{};
    push_const_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push_const_range.offset = 0;
    push_const_range.size = sizeof(glm::ivec4) + sizeof(uint32_t);  // src_size, dst_size, mip_level

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &hzb_descriptor_layout_;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_const_range;

    vkCreatePipelineLayout(device_->get_device(), &layout_info, nullptr, &hzb_build_layout_);

    // Similar layout for test pipeline (with more descriptor bindings)
    VkPipelineLayoutCreateInfo test_layout_info = layout_info;
    vkCreatePipelineLayout(device_->get_device(), &test_layout_info, nullptr, &hzb_test_layout_);

    // Pipeline creation deferred: requires ShaderCompiler integration
    // For full implementation:
    // 1. Load hzb_build.comp shader
    // 2. Create VkComputePipeline with pipeline layout
    // 3. Same for hzb_test.comp
}

VkImage HZBCuller::build_hzb(VkCommandBuffer cmd, VkImage depth_texture, VkExtent2D depth_size) {
    if (!config_.enabled) {
        return depth_texture;
    }

    if (!hzb_texture_) {
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

    // Build mipmap chain level by level
    uint32_t mip_levels = std::min(config_.mip_levels,
        static_cast<uint32_t>(std::log2(std::max(depth_size.width, depth_size.height))));

    for (uint32_t level = 1; level < mip_levels; level++) {
        VkExtent2D src_size = {
            depth_size.width >> (level - 1),
            depth_size.height >> (level - 1)
        };
        VkExtent2D dst_size = {
            std::max(1u, src_size.width >> 1),
            std::max(1u, src_size.height >> 1)
        };

        build_hzb_mipmap_level(cmd, level - 1, level, depth_texture, src_size, dst_size);
    }

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

    if (!config_.enabled || !indirect_buffer_ || draw_calls.empty()) {
        out_indirect_buffer = nullptr;
        return 0;
    }

    // Upload AABBs to GPU
    size_t aabb_buffer_size = draw_calls.size() * sizeof(glm::vec4) * 2;
    if (aabb_buffer_size > aabb_buffer_->get_size()) {
        return 0;  // Buffer too small
    }

    // Prepare AABB data (min, max bounds)
    std::vector<glm::vec4> aabb_data;
    for (const auto& call : draw_calls) {
        aabb_data.push_back(glm::vec4(call.aabb_min, 0));
        aabb_data.push_back(glm::vec4(call.aabb_max, 0));
    }

    // Copy to GPU (deferred: implement staged transfer)
    // For now: placeholder for actual implementation

    // Dispatch occlusion test compute shader
    // Each thread tests one mesh AABB
    uint32_t num_groups = (draw_calls.size() + 255) / 256;  // 256 threads per group

    if (hzb_test_pipeline_ != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, hzb_test_pipeline_);
        
        // Bind descriptor sets and buffers
        // Push constants with view_proj matrix
        // Dispatch compute shader

        vkCmdDispatch(cmd, num_groups, 1, 1);
    }

    // Barrier: wait for compute to finish
    VkMemoryBarrier mem_barrier{};
    mem_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mem_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mem_barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 1, &mem_barrier, 0, nullptr, 0, nullptr);

    out_indirect_buffer = indirect_buffer_->get_handle();
    
    // Return max possible count (actual count in GPU buffer)
    return static_cast<uint32_t>(draw_calls.size());
}

void HZBCuller::create_hzb_texture(VkExtent2D initial_size) {
    uint32_t mip_levels = std::min(config_.mip_levels,
        static_cast<uint32_t>(std::log2(std::max(initial_size.width, initial_size.height))) + 1);

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

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = hzb_texture_->get_image();
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R32_SFLOAT;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels, 0, 1};

    vkCreateImageView(device_->get_device(), &view_info, nullptr, &hzb_view_);
}

void HZBCuller::build_hzb_mipmap_level(VkCommandBuffer cmd, uint32_t src_level, uint32_t dst_level,
                                      VkImage depth_texture, VkExtent2D src_size, VkExtent2D dst_size) {
    if (hzb_build_pipeline_ == VK_NULL_HANDLE) {
        return;  // Pipeline not ready yet
    }

    // Transition destination mip level to storage
    VkImageMemoryBarrier dst_barrier{};
    dst_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    dst_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    dst_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    dst_barrier.image = hzb_texture_->get_image();
    dst_barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, dst_level, 1, 0, 1};
    dst_barrier.srcAccessMask = 0;
    dst_barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &dst_barrier);

    // Bind pipeline and dispatch
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, hzb_build_pipeline_);

    // Push constants
    struct {
        glm::ivec2 src_size;
        glm::ivec2 dst_size;
        uint32_t mip_level;
    } constants = {
        {static_cast<int>(src_size.width), static_cast<int>(src_size.height)},
        {static_cast<int>(dst_size.width), static_cast<int>(dst_size.height)},
        dst_level
    };

    vkCmdPushConstants(cmd, hzb_build_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,
        sizeof(constants), &constants);

    // Dispatch threads
    uint32_t group_x = (dst_size.width + 15) / 16;
    uint32_t group_y = (dst_size.height + 15) / 16;
    vkCmdDispatch(cmd, group_x, group_y, 1);
}

}  // namespace gws
