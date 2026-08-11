#include "vulkan_vxao_pass.h"
#include "vulkan_device.h"
#include "vulkan_g_buffer.h"
#include "vulkan_scene_mesh.h"
#include "vulkan_render_graph.h" // DrawItem
#include "vxao_spirv.h"

#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>
#include <array>
#include <cstring>
#include <cstdint>

namespace gws::renderer::gpu {

namespace {
constexpr VkFormat kVoxelFormat = VK_FORMAT_R8_UNORM;
constexpr VkFormat kAoFormat    = VK_FORMAT_R8_UNORM;
struct VolUBO { glm::vec4 vol_min; glm::vec4 vol_size; glm::vec4 grid; };
struct VoxPush { glm::mat4 mvp; glm::mat4 model; };
} // namespace

std::unique_ptr<VulkanVxaoPass> VulkanVxaoPass::create(VulkanDevice* device,
                                                       VulkanGBuffer* g_buffer,
                                                       uint32_t width,
                                                       uint32_t height) {
    if (!device || !g_buffer || width == 0 || height == 0) return nullptr;
    auto p = std::unique_ptr<VulkanVxaoPass>(new VulkanVxaoPass());
    if (!p->initialize(device, g_buffer, width, height)) return nullptr;
    spdlog::info("VulkanVxaoPass created ({}x{}, grid {}^3)", width, height, kGrid);
    return p;
}

VulkanVxaoPass::~VulkanVxaoPass() { destroy(); }

bool VulkanVxaoPass::initialize(VulkanDevice* device, VulkanGBuffer* g_buffer,
                                uint32_t width, uint32_t height) {
    device_ = device; g_buffer_ = g_buffer; width_ = width; height_ = height;
    VkDevice vk = device_->get_device();
    {
        VkSamplerCreateInfo si{}; si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_NEAREST; si.minFilter = VK_FILTER_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(vk, &si, nullptr, &gbuffer_sampler_) != VK_SUCCESS) return false;
    }
    if (!create_voxel_image())      return false;
    if (!create_volume_ubo())       return false;
    if (!create_voxelize_pipeline())return false;
    if (!create_ao_image())         return false;
    if (!create_cone_pipeline())    return false;
    return true;
}

bool VulkanVxaoPass::create_voxel_image() {
    VkDevice vk = device_->get_device();
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_3D;
    ii.format = kVoxelFormat;
    ii.extent = { kGrid, kGrid, kGrid };
    ii.mipLevels = 1; ii.arrayLayers = 1; ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
               VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(vk, &ii, nullptr, &voxel_image_) != VK_SUCCESS) return false;
    VkMemoryRequirements mr{}; vkGetImageMemoryRequirements(vk, voxel_image_, &mr);
    VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = device_->find_memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vk, &ai, nullptr, &voxel_memory_) != VK_SUCCESS) return false;
    vkBindImageMemory(vk, voxel_image_, voxel_memory_, 0);

    VkImageViewCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = voxel_image_; vi.viewType = VK_IMAGE_VIEW_TYPE_3D; vi.format = kVoxelFormat;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(vk, &vi, nullptr, &voxel_storage_view_) != VK_SUCCESS) return false;
    if (vkCreateImageView(vk, &vi, nullptr, &voxel_sampled_view_) != VK_SUCCESS) return false;

    VkSamplerCreateInfo si{}; si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR; si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(vk, &si, nullptr, &voxel_sampler_) != VK_SUCCESS) return false;
    return true;
}

bool VulkanVxaoPass::create_volume_ubo() {
    VkDevice vk = device_->get_device();
    VkBufferCreateInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = sizeof(VolUBO); bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vk, &bi, nullptr, &vol_ubo_) != VK_SUCCESS) return false;
    VkMemoryRequirements mr{}; vkGetBufferMemoryRequirements(vk, vol_ubo_, &mr);
    VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = device_->find_memory_type(mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(vk, &ai, nullptr, &vol_ubo_memory_) != VK_SUCCESS) return false;
    vkBindBufferMemory(vk, vol_ubo_, vol_ubo_memory_, 0);
    vkMapMemory(vk, vol_ubo_memory_, 0, sizeof(VolUBO), 0, &vol_ubo_mapped_);
    return true;
}

bool VulkanVxaoPass::create_voxelize_pipeline() {
    VkDevice vk = device_->get_device();
    // Attachment-less render pass.
    {
        VkSubpassDescription sub{}; sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        VkRenderPassCreateInfo rpi{}; rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpi.subpassCount = 1; rpi.pSubpasses = &sub;
        if (vkCreateRenderPass(vk, &rpi, nullptr, &voxelize_rp_) != VK_SUCCESS) return false;
        VkFramebufferCreateInfo fbi{}; fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbi.renderPass = voxelize_rp_; fbi.attachmentCount = 0;
        fbi.width = kGrid; fbi.height = kGrid; fbi.layers = 1;
        if (vkCreateFramebuffer(vk, &fbi, nullptr, &voxelize_fb_) != VK_SUCCESS) return false;
    }
    // DSL: 0 = image3D (write), 1 = volume UBO.
    {
        std::array<VkDescriptorSetLayoutBinding, 2> b{};
        b[0].binding = 0; b[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        b[0].descriptorCount = 1; b[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        b[1].binding = 1; b[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        b[1].descriptorCount = 1; b[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo li{}; li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 2; li.pBindings = b.data();
        if (vkCreateDescriptorSetLayout(vk, &li, nullptr, &voxelize_dsl_) != VK_SUCCESS) return false;

        std::array<VkDescriptorPoolSize, 2> ps{};
        ps[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;  ps[0].descriptorCount = 1;
        ps[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; ps[1].descriptorCount = 1;
        VkDescriptorPoolCreateInfo pi{}; pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets = 1; pi.poolSizeCount = 2; pi.pPoolSizes = ps.data();
        if (vkCreateDescriptorPool(vk, &pi, nullptr, &voxelize_pool_) != VK_SUCCESS) return false;
        VkDescriptorSetAllocateInfo dai{}; dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dai.descriptorPool = voxelize_pool_; dai.descriptorSetCount = 1; dai.pSetLayouts = &voxelize_dsl_;
        if (vkAllocateDescriptorSets(vk, &dai, &voxelize_set_) != VK_SUCCESS) return false;

        VkDescriptorImageInfo img{}; img.imageView = voxel_storage_view_; img.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorBufferInfo buf{}; buf.buffer = vol_ubo_; buf.range = VK_WHOLE_SIZE;
        std::array<VkWriteDescriptorSet, 2> w{};
        w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[0].dstSet = voxelize_set_;
        w[0].dstBinding = 0; w[0].descriptorCount = 1; w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w[0].pImageInfo = &img;
        w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[1].dstSet = voxelize_set_;
        w[1].dstBinding = 1; w[1].descriptorCount = 1; w[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; w[1].pBufferInfo = &buf;
        vkUpdateDescriptorSets(vk, 2, w.data(), 0, nullptr);
    }
    // Pipeline.
    {
        VkPushConstantRange pr{}; pr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; pr.size = sizeof(VoxPush);
        VkPipelineLayoutCreateInfo pli{}; pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1; pli.pSetLayouts = &voxelize_dsl_;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pr;
        if (vkCreatePipelineLayout(vk, &pli, nullptr, &voxelize_layout_) != VK_SUCCESS) return false;

        auto mkmod = [&](const uint32_t* code, size_t size) {
            VkShaderModuleCreateInfo smi{}; smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            smi.codeSize = size; smi.pCode = code; VkShaderModule m = VK_NULL_HANDLE;
            vkCreateShaderModule(vk, &smi, nullptr, &m); return m;
        };
        VkShaderModule vs = mkmod(kVoxelizeVertSpv, kVoxelizeVertSpv_size);
        VkShaderModule fs = mkmod(kVoxelizeFragSpv, kVoxelizeFragSpv_size);
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vs; stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = "main";

        auto binding = Mesh::vertex_binding();
        VkVertexInputAttributeDescription posAttr{}; posAttr.location = 0; posAttr.binding = 0;
        posAttr.format = VK_FORMAT_R32G32B32_SFLOAT; posAttr.offset = 0;
        VkPipelineVertexInputStateCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &binding;
        vi.vertexAttributeDescriptionCount = 1; vi.pVertexAttributeDescriptions = &posAttr;

        VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport vp{}; vp.width = float(kGrid); vp.height = float(kGrid); vp.maxDepth = 1.0f;
        VkRect2D sc{}; sc.extent = { kGrid, kGrid };
        VkPipelineViewportStateCreateInfo vps{}; vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vps.viewportCount = 1; vps.pViewports = &vp; vps.scissorCount = 1; vps.pScissors = &sc;
        VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendStateCreateInfo cb{}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 0; // no color attachments
        VkGraphicsPipelineCreateInfo gpi{}; gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gpi.stageCount = 2; gpi.pStages = stages.data();
        gpi.pVertexInputState = &vi; gpi.pInputAssemblyState = &ia;
        gpi.pViewportState = &vps; gpi.pRasterizationState = &rs;
        gpi.pMultisampleState = &ms; gpi.pColorBlendState = &cb;
        gpi.layout = voxelize_layout_; gpi.renderPass = voxelize_rp_;
        VkResult r = vkCreateGraphicsPipelines(vk, VK_NULL_HANDLE, 1, &gpi, nullptr, &voxelize_pipe_);
        vkDestroyShaderModule(vk, vs, nullptr); vkDestroyShaderModule(vk, fs, nullptr);
        if (r != VK_SUCCESS) return false;
    }
    return true;
}

bool VulkanVxaoPass::create_ao_image() {
    VkDevice vk = device_->get_device();
    VkImageCreateInfo ii{}; ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D; ii.format = kAoFormat;
    ii.extent = { width_, height_, 1 }; ii.mipLevels = 1; ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(vk, &ii, nullptr, &ao_image_) != VK_SUCCESS) return false;
    VkMemoryRequirements mr{}; vkGetImageMemoryRequirements(vk, ao_image_, &mr);
    VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = device_->find_memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vk, &ai, nullptr, &ao_memory_) != VK_SUCCESS) return false;
    vkBindImageMemory(vk, ao_image_, ao_memory_, 0);
    VkImageViewCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = ao_image_; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = kAoFormat;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(vk, &vi, nullptr, &ao_view_) != VK_SUCCESS) return false;
    VkSamplerCreateInfo si{}; si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR; si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(vk, &si, nullptr, &ao_sampler_) != VK_SUCCESS) return false;
    return true;
}

bool VulkanVxaoPass::create_cone_pipeline() {
    VkDevice vk = device_->get_device();
    std::array<VkDescriptorSetLayoutBinding, 5> b{};
    b[0].binding = 0; b[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // pos
    b[1].binding = 1; b[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // normal
    b[2].binding = 2; b[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;          // ao out
    b[3].binding = 3; b[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // voxel 3D
    b[4].binding = 4; b[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;         // volume
    for (auto& x : b) { x.descriptorCount = 1; x.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; }
    VkDescriptorSetLayoutCreateInfo li{}; li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 5; li.pBindings = b.data();
    if (vkCreateDescriptorSetLayout(vk, &li, nullptr, &cone_dsl_) != VK_SUCCESS) return false;

    VkPushConstantRange pr{}; pr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; pr.size = sizeof(float) * 4;
    VkPipelineLayoutCreateInfo pli{}; pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1; pli.pSetLayouts = &cone_dsl_;
    pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pr;
    if (vkCreatePipelineLayout(vk, &pli, nullptr, &cone_layout_) != VK_SUCCESS) return false;

    VkShaderModuleCreateInfo smi{}; smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = kVxaoSpv_size; smi.pCode = kVxaoSpv;
    VkShaderModule sm = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vk, &smi, nullptr, &sm) != VK_SUCCESS) return false;
    VkComputePipelineCreateInfo cpi{}; cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; cpi.stage.module = sm; cpi.stage.pName = "main";
    cpi.layout = cone_layout_;
    VkResult r = vkCreateComputePipelines(vk, VK_NULL_HANDLE, 1, &cpi, nullptr, &cone_pipe_);
    vkDestroyShaderModule(vk, sm, nullptr);
    if (r != VK_SUCCESS) return false;

    std::array<VkDescriptorPoolSize, 3> ps{};
    ps[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps[0].descriptorCount = 3;
    ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;          ps[1].descriptorCount = 1;
    ps[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;         ps[2].descriptorCount = 1;
    VkDescriptorPoolCreateInfo pi{}; pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = 1; pi.poolSizeCount = 3; pi.pPoolSizes = ps.data();
    if (vkCreateDescriptorPool(vk, &pi, nullptr, &cone_pool_) != VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo dai{}; dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool = cone_pool_; dai.descriptorSetCount = 1; dai.pSetLayouts = &cone_dsl_;
    if (vkAllocateDescriptorSets(vk, &dai, &cone_set_) != VK_SUCCESS) return false;

    VkImageLayout sr = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    std::array<VkDescriptorImageInfo, 4> ii{};
    ii[0] = { gbuffer_sampler_, g_buffer_->get_position_view(), sr };
    ii[1] = { gbuffer_sampler_, g_buffer_->get_normal_view(),   sr };
    ii[2] = { VK_NULL_HANDLE,   ao_view_,                       VK_IMAGE_LAYOUT_GENERAL };
    ii[3] = { voxel_sampler_,   voxel_sampled_view_,            sr };
    VkDescriptorBufferInfo buf{}; buf.buffer = vol_ubo_; buf.range = VK_WHOLE_SIZE;
    std::array<VkWriteDescriptorSet, 5> w{};
    auto mkimg = [&](int i, uint32_t binding, VkDescriptorType t) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[i].dstSet = cone_set_;
        w[i].dstBinding = binding; w[i].descriptorCount = 1; w[i].descriptorType = t; w[i].pImageInfo = &ii[i];
    };
    mkimg(0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    mkimg(1, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    mkimg(2, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    mkimg(3, 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    w[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[4].dstSet = cone_set_;
    w[4].dstBinding = 4; w[4].descriptorCount = 1; w[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; w[4].pBufferInfo = &buf;
    vkUpdateDescriptorSets(vk, 5, w.data(), 0, nullptr);
    return true;
}

void VulkanVxaoPass::voxelize(VkCommandBuffer cmd, const DrawItem* items, size_t count,
                              const glm::vec3& camera_position) {
    if (voxelize_pipe_ == VK_NULL_HANDLE) return;

    const float half = volume_size_ * 0.5f;
    const glm::vec3 center = camera_position;
    const glm::vec3 vmin = center - glm::vec3(half);
    VolUBO ubo{};
    ubo.vol_min  = glm::vec4(vmin, 0.0f);
    ubo.vol_size = glm::vec4(volume_size_, volume_size_, volume_size_, 0.0f);
    ubo.grid     = glm::vec4(float(kGrid), 0, 0, 0);
    std::memcpy(vol_ubo_mapped_, &ubo, sizeof(ubo));

    auto barrier = [&](VkImageLayout from, VkImageLayout to, VkAccessFlags sa, VkAccessFlags da,
                       VkPipelineStageFlags ss, VkPipelineStageFlags ds) {
        VkImageMemoryBarrier b{}; b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = from; b.newLayout = to; b.image = voxel_image_;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.levelCount = 1; b.subresourceRange.layerCount = 1;
        b.srcAccessMask = sa; b.dstAccessMask = da;
        vkCmdPipelineBarrier(cmd, ss, ds, 0, 0, nullptr, 0, nullptr, 1, &b);
    };

    // Clear the grid.
    barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkClearColorValue clr{}; clr.float32[0] = 0.0f;
    VkImageSubresourceRange range{}; range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1; range.layerCount = 1;
    vkCmdClearColorImage(cmd, voxel_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clr, 1, &range);
    barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    // Three orthographic VPs looking down each axis, covering the volume.
    glm::mat4 axisVP[3];
    axisVP[0] = glm::orthoRH_ZO(-half, half, -half, half, 0.0f, volume_size_) *
                glm::lookAt(center + glm::vec3(half,0,0), center, glm::vec3(0,1,0)); // -X
    axisVP[1] = glm::orthoRH_ZO(-half, half, -half, half, 0.0f, volume_size_) *
                glm::lookAt(center + glm::vec3(0,half,0), center, glm::vec3(0,0,1)); // -Y
    axisVP[2] = glm::orthoRH_ZO(-half, half, -half, half, 0.0f, volume_size_) *
                glm::lookAt(center + glm::vec3(0,0,half), center, glm::vec3(0,1,0)); // -Z

    VkRenderPassBeginInfo rpi{}; rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass = voxelize_rp_; rpi.framebuffer = voxelize_fb_;
    rpi.renderArea.extent = { kGrid, kGrid };
    vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, voxelize_pipe_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, voxelize_layout_, 0, 1, &voxelize_set_, 0, nullptr);

    for (int axis = 0; axis < 3; ++axis) {
        for (size_t i = 0; i < count; ++i) {
            const DrawItem& d = items[i];
            if (!d.mesh) continue;
            VoxPush pc{}; pc.mvp = axisVP[axis] * d.model; pc.model = d.model;
            vkCmdPushConstants(cmd, voxelize_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
            d.mesh->bind(cmd);
            d.mesh->draw_submesh(cmd, d.submesh_index, 0);
        }
    }
    vkCmdEndRenderPass(cmd);

    // Make the grid readable by the cone-trace sampler.
    barrier(VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

void VulkanVxaoPass::compute_ao(VkCommandBuffer cmd) {
    if (cone_pipe_ == VK_NULL_HANDLE) return;
    // AO output → GENERAL for compute write.
    VkImageMemoryBarrier b{}; b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.image = ao_image_; b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1; b.subresourceRange.layerCount = 1;
    b.srcAccessMask = 0; b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cone_pipe_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cone_layout_, 0, 1, &cone_set_, 0, nullptr);
    struct { int32_t w, h; float intensity, pad; } pc{};
    pc.w = int32_t(width_); pc.h = int32_t(height_); pc.intensity = 1.0f;
    vkCmdPushConstants(cmd, cone_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
    vkCmdDispatch(cmd, (width_ + 7) / 8, (height_ + 7) / 8, 1);

    // AO output → SHADER_READ_ONLY for the lighting pass.
    b.oldLayout = VK_IMAGE_LAYOUT_GENERAL; b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);
}

void VulkanVxaoPass::destroy() {
    if (!device_) return;
    VkDevice vk = device_->get_device();
    auto dp = [&](VkPipeline& p){ if (p) { vkDestroyPipeline(vk, p, nullptr); p = VK_NULL_HANDLE; } };
    auto dl = [&](VkPipelineLayout& l){ if (l) { vkDestroyPipelineLayout(vk, l, nullptr); l = VK_NULL_HANDLE; } };
    auto dpool = [&](VkDescriptorPool& p){ if (p) { vkDestroyDescriptorPool(vk, p, nullptr); p = VK_NULL_HANDLE; } };
    auto dsl = [&](VkDescriptorSetLayout& l){ if (l) { vkDestroyDescriptorSetLayout(vk, l, nullptr); l = VK_NULL_HANDLE; } };
    dp(cone_pipe_); dl(cone_layout_); dpool(cone_pool_); dsl(cone_dsl_);
    dp(voxelize_pipe_); dl(voxelize_layout_); dpool(voxelize_pool_); dsl(voxelize_dsl_);
    if (voxelize_fb_) { vkDestroyFramebuffer(vk, voxelize_fb_, nullptr); voxelize_fb_ = VK_NULL_HANDLE; }
    if (voxelize_rp_) { vkDestroyRenderPass(vk, voxelize_rp_, nullptr); voxelize_rp_ = VK_NULL_HANDLE; }
    if (ao_sampler_) { vkDestroySampler(vk, ao_sampler_, nullptr); ao_sampler_ = VK_NULL_HANDLE; }
    if (ao_view_)    { vkDestroyImageView(vk, ao_view_, nullptr); ao_view_ = VK_NULL_HANDLE; }
    if (ao_image_)   { vkDestroyImage(vk, ao_image_, nullptr); ao_image_ = VK_NULL_HANDLE; }
    if (ao_memory_)  { vkFreeMemory(vk, ao_memory_, nullptr); ao_memory_ = VK_NULL_HANDLE; }
    if (vol_ubo_mapped_) { vkUnmapMemory(vk, vol_ubo_memory_); vol_ubo_mapped_ = nullptr; }
    if (vol_ubo_)        { vkDestroyBuffer(vk, vol_ubo_, nullptr); vol_ubo_ = VK_NULL_HANDLE; }
    if (vol_ubo_memory_) { vkFreeMemory(vk, vol_ubo_memory_, nullptr); vol_ubo_memory_ = VK_NULL_HANDLE; }
    if (voxel_sampler_)      { vkDestroySampler(vk, voxel_sampler_, nullptr); voxel_sampler_ = VK_NULL_HANDLE; }
    if (voxel_storage_view_) { vkDestroyImageView(vk, voxel_storage_view_, nullptr); voxel_storage_view_ = VK_NULL_HANDLE; }
    if (voxel_sampled_view_) { vkDestroyImageView(vk, voxel_sampled_view_, nullptr); voxel_sampled_view_ = VK_NULL_HANDLE; }
    if (voxel_image_)  { vkDestroyImage(vk, voxel_image_, nullptr); voxel_image_ = VK_NULL_HANDLE; }
    if (voxel_memory_) { vkFreeMemory(vk, voxel_memory_, nullptr); voxel_memory_ = VK_NULL_HANDLE; }
    if (gbuffer_sampler_) { vkDestroySampler(vk, gbuffer_sampler_, nullptr); gbuffer_sampler_ = VK_NULL_HANDLE; }
    device_ = nullptr;
}

} // namespace gws::renderer::gpu
