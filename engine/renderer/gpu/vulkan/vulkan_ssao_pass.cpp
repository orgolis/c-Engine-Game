/**
 * @file vulkan_ssao_pass.cpp
 * @brief Implementation of the SSAO compute pass.
 */

#include "vulkan_ssao_pass.h"
#include "vulkan_device.h"
#include "vulkan_g_buffer.h"
#include "ssao_spirv.h"
#include "ssao_blur_spirv.h"
#include "ssao_rt_spirv.h"
#include "ao_techniques_spirv.h" // HBAO / HDAO / GTAO

#include <spdlog/spdlog.h>
#include <array>
#include <cstring>

namespace gws::renderer::gpu {

namespace {
constexpr VkFormat kOcclusionFormat = VK_FORMAT_R8_UNORM;
}

std::unique_ptr<VulkanSsaoPass> VulkanSsaoPass::create(VulkanDevice* device,
                                                        VulkanGBuffer* g_buffer,
                                                        uint32_t width,
                                                        uint32_t height,
                                                        bool use_rt) {
    if (device == nullptr || g_buffer == nullptr || width == 0 || height == 0) {
        spdlog::error("VulkanSsaoPass::create: bad args");
        return nullptr;
    }
    auto pass = std::unique_ptr<VulkanSsaoPass>(new VulkanSsaoPass());
    pass->rt_available_ = device->has_ray_tracing();
    // Default to GTAO: an ANALYTIC (horizon-based) AO that is smooth by
    // construction — no Monte-Carlo ray noise, no temporal accumulation, no
    // flowing, and cheaper than ray-traced AO. RT AO is inherently grainy
    // without a full denoiser, so it's opt-in via set_technique() only.
    (void)use_rt;
    pass->technique_ = AoTechnique::GTAO;
    if (!pass->initialize(device, g_buffer, width, height)) {
        return nullptr;
    }
    spdlog::info("VulkanSsaoPass created ({}x{}, technique={})", width, height,
                 static_cast<int>(pass->technique_));
    return pass;
}

VulkanSsaoPass::~VulkanSsaoPass() { destroy(); }

void VulkanSsaoPass::destroy() {
    if (device_ == nullptr) return;
    VkDevice vk = device_->get_device();
    if (blur_pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(vk, blur_pipeline_, nullptr); blur_pipeline_ = VK_NULL_HANDLE; }
    if (blur_layout_   != VK_NULL_HANDLE) { vkDestroyPipelineLayout(vk, blur_layout_, nullptr); blur_layout_ = VK_NULL_HANDLE; }
    if (blur_pool_     != VK_NULL_HANDLE) { vkDestroyDescriptorPool(vk, blur_pool_, nullptr); blur_pool_ = VK_NULL_HANDLE; blur_set_ = VK_NULL_HANDLE; }
    if (blur_dsl_      != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(vk, blur_dsl_, nullptr); blur_dsl_ = VK_NULL_HANDLE; }
    if (pipeline_  != VK_NULL_HANDLE) { vkDestroyPipeline(vk, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (layout_    != VK_NULL_HANDLE) { vkDestroyPipelineLayout(vk, layout_, nullptr); layout_ = VK_NULL_HANDLE; }
    if (pool_      != VK_NULL_HANDLE) { vkDestroyDescriptorPool(vk, pool_, nullptr); pool_ = VK_NULL_HANDLE; set_ = VK_NULL_HANDLE; }
    if (dsl_       != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(vk, dsl_, nullptr); dsl_ = VK_NULL_HANDLE; }
    if (sampler_   != VK_NULL_HANDLE) { vkDestroySampler(vk, sampler_, nullptr); sampler_ = VK_NULL_HANDLE; }
    if (gbuffer_sampler_ != VK_NULL_HANDLE) { vkDestroySampler(vk, gbuffer_sampler_, nullptr); gbuffer_sampler_ = VK_NULL_HANDLE; }
    destroy_output_image();
    device_ = nullptr;
}

void VulkanSsaoPass::destroy_output_image() {
    if (device_ == nullptr) return;
    VkDevice vk = device_->get_device();
    if (output_view_   != VK_NULL_HANDLE) { vkDestroyImageView(vk, output_view_, nullptr); output_view_ = VK_NULL_HANDLE; }
    if (output_image_  != VK_NULL_HANDLE) { vkDestroyImage(vk, output_image_, nullptr); output_image_ = VK_NULL_HANDLE; }
    if (output_memory_ != VK_NULL_HANDLE) { vkFreeMemory(vk, output_memory_, nullptr); output_memory_ = VK_NULL_HANDLE; }
    if (blurred_view_   != VK_NULL_HANDLE) { vkDestroyImageView(vk, blurred_view_, nullptr); blurred_view_ = VK_NULL_HANDLE; }
    if (blurred_image_  != VK_NULL_HANDLE) { vkDestroyImage(vk, blurred_image_, nullptr); blurred_image_ = VK_NULL_HANDLE; }
    if (blurred_memory_ != VK_NULL_HANDLE) { vkFreeMemory(vk, blurred_memory_, nullptr); blurred_memory_ = VK_NULL_HANDLE; }
    if (history_view_   != VK_NULL_HANDLE) { vkDestroyImageView(vk, history_view_, nullptr); history_view_ = VK_NULL_HANDLE; }
    if (history_image_  != VK_NULL_HANDLE) { vkDestroyImage(vk, history_image_, nullptr); history_image_ = VK_NULL_HANDLE; }
    if (history_memory_ != VK_NULL_HANDLE) { vkFreeMemory(vk, history_memory_, nullptr); history_memory_ = VK_NULL_HANDLE; }
}

bool VulkanSsaoPass::initialize(VulkanDevice* device, VulkanGBuffer* g_buffer,
                                 uint32_t width, uint32_t height) {
    device_   = device;
    g_buffer_ = g_buffer;
    width_    = width;
    height_   = height;

    // Sampler for reading G-Buffer inputs.
    {
        VkSamplerCreateInfo si{};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = VK_FILTER_NEAREST;
        si.minFilter    = VK_FILTER_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(device_->get_device(), &si, nullptr, &gbuffer_sampler_) != VK_SUCCESS) return false;
    }
    // Sampler for downstream lighting pass reading our occlusion.
    {
        VkSamplerCreateInfo si{};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = VK_FILTER_LINEAR;
        si.minFilter    = VK_FILTER_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(device_->get_device(), &si, nullptr, &sampler_) != VK_SUCCESS) return false;
    }

    if (!create_output_image()) return false;
    if (!build_compute_for_technique()) return false;
    if (!create_blur_pipeline()) return false;
    if (!create_blur_descriptor_resources()) return false;
    write_blur_descriptor_set();
    return true;
}

bool VulkanSsaoPass::build_compute_for_technique() {
    if (!create_pipeline())             return false;
    if (!create_descriptor_resources()) return false;
    write_descriptor_set();
    // A previously-bound TLAS must be re-written into the fresh descriptor
    // set when we (re)build into RT mode.
    if (technique_ == AoTechnique::RT && tlas_ != VK_NULL_HANDLE) {
        VkAccelerationStructureKHR t = tlas_;
        tlas_ = VK_NULL_HANDLE; // force set_tlas to perform the write
        set_tlas(t);
    }
    return true;
}

void VulkanSsaoPass::destroy_compute_resources() {
    VkDevice vk = device_->get_device();
    if (pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(vk, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (layout_   != VK_NULL_HANDLE) { vkDestroyPipelineLayout(vk, layout_, nullptr); layout_ = VK_NULL_HANDLE; }
    if (pool_     != VK_NULL_HANDLE) { vkDestroyDescriptorPool(vk, pool_, nullptr); pool_ = VK_NULL_HANDLE; set_ = VK_NULL_HANDLE; }
    if (dsl_      != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(vk, dsl_, nullptr); dsl_ = VK_NULL_HANDLE; }
}

void VulkanSsaoPass::set_technique(AoTechnique t) {
    // Resolve unavailable techniques to a sensible fallback.
    if (t == AoTechnique::RT && !rt_available_) t = AoTechnique::SSAO;
    if (t == technique_) return;
    vkDeviceWaitIdle(device_->get_device());
    technique_ = t;
    history_valid_ = false;   // technique change → AO history no longer valid
    destroy_compute_resources();
    build_compute_for_technique();
    spdlog::info("SSAO pass: switched to AO technique {}", static_cast<int>(technique_));
}

bool VulkanSsaoPass::create_output_image() {
    VkDevice vk = device_->get_device();
    auto make_r8 = [&](VkImage& img, VkDeviceMemory& mem, VkImageView& view) -> bool {
        VkImageCreateInfo ii{};
        ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType     = VK_IMAGE_TYPE_2D;
        ii.format        = kOcclusionFormat;
        ii.extent        = { width_, height_, 1 };
        ii.mipLevels     = 1;
        ii.arrayLayers   = 1;
        ii.samples       = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
        // + TRANSFER bits: output is copied into history each frame (RT temporal
        // reprojection accumulation).
        ii.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(vk, &ii, nullptr, &img) != VK_SUCCESS) return false;
        VkMemoryRequirements mr{};
        vkGetImageMemoryRequirements(vk, img, &mr);
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = mr.size;
        ai.memoryTypeIndex = device_->find_memory_type(
            mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(vk, &ai, nullptr, &mem) != VK_SUCCESS) return false;
        vkBindImageMemory(vk, img, mem, 0);
        VkImageViewCreateInfo vi{};
        vi.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image            = img;
        vi.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        vi.format           = kOcclusionFormat;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        return vkCreateImageView(vk, &vi, nullptr, &view) == VK_SUCCESS;
    };
    if (!make_r8(output_image_,  output_memory_,  output_view_))  return false;
    if (!make_r8(blurred_image_, blurred_memory_, blurred_view_)) return false;
    if (!make_r8(history_image_, history_memory_, history_view_)) return false;
    return true;
}

bool VulkanSsaoPass::create_pipeline() {
    VkDevice vk = device_->get_device();

    std::array<VkDescriptorSetLayoutBinding, 6> b{};
    b[0].binding = 0; b[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[0].descriptorCount = 1; b[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    b[1].binding = 1; b[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[1].descriptorCount = 1; b[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    b[2].binding = 2; b[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[2].descriptorCount = 1; b[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    b[3].binding = 3; b[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    b[3].descriptorCount = 1; b[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    b[4].binding = 4; b[4].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    b[4].descriptorCount = 1; b[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    b[5].binding = 5; b[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // RT history
    b[5].descriptorCount = 1; b[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = (technique_ == AoTechnique::RT) ? 6u : 4u;   // RT adds accel(4) + history(5)
    li.pBindings    = b.data();
    if (vkCreateDescriptorSetLayout(vk, &li, nullptr, &dsl_) != VK_SUCCESS) return false;

    // Push constants: RT needs 2 mat4 (cur+prev view_proj) + ivec2 + 3 floats +
    // 2 ints = 156B (round to 160); screen-space techniques use 1 mat4 = 96B.
    VkPushConstantRange pr{};
    pr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pr.size       = (technique_ == AoTechnique::RT) ? 160u : 96u;
    VkPipelineLayoutCreateInfo pli{};
    pli.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount         = 1;
    pli.pSetLayouts            = &dsl_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges    = &pr;
    if (vkCreatePipelineLayout(vk, &pli, nullptr, &layout_) != VK_SUCCESS) return false;

    VkShaderModuleCreateInfo smi{};
    smi.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    // Select the compute shader for the active technique. VXAO has no
    // screen-space shader here (handled by the voxel pass in Stage 2); it
    // falls back to SSAO until then.
    switch (technique_) {
        case AoTechnique::HBAO: smi.codeSize = kHbaoSpv_size;    smi.pCode = kHbaoSpv;    break;
        case AoTechnique::HDAO: smi.codeSize = kHdaoSpv_size;    smi.pCode = kHdaoSpv;    break;
        case AoTechnique::GTAO: smi.codeSize = kGtaoSpv_size;    smi.pCode = kGtaoSpv;    break;
        case AoTechnique::RT:   smi.codeSize = kSsaoRtSpv_size;  smi.pCode = kSsaoRtSpv;  break;
        case AoTechnique::SSAO:
        case AoTechnique::VXAO:
        default:                smi.codeSize = kSsaoSpv_size;    smi.pCode = kSsaoSpv;    break;
    }
    VkShaderModule sm = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vk, &smi, nullptr, &sm) != VK_SUCCESS) return false;
    VkComputePipelineCreateInfo cpi{};
    cpi.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpi.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.module = sm;
    cpi.stage.pName  = "main";
    cpi.layout       = layout_;
    VkResult r = vkCreateComputePipelines(vk, VK_NULL_HANDLE, 1, &cpi, nullptr, &pipeline_);
    vkDestroyShaderModule(vk, sm, nullptr);
    return r == VK_SUCCESS;
}

bool VulkanSsaoPass::create_descriptor_resources() {
    VkDevice vk = device_->get_device();
    std::array<VkDescriptorPoolSize, 3> ps{};
    // RT mode has a 4th combined sampler (binding 5 = reprojection history).
    ps[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps[0].descriptorCount = (technique_ == AoTechnique::RT) ? 4u : 3u;
    ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;              ps[1].descriptorCount = 1;
    ps[2].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR; ps[2].descriptorCount = 1;
    VkDescriptorPoolCreateInfo pi{};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets       = 1;
    pi.poolSizeCount = (technique_ == AoTechnique::RT) ? 3u : 2u;
    pi.pPoolSizes    = ps.data();
    if (vkCreateDescriptorPool(vk, &pi, nullptr, &pool_) != VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo dai{};
    dai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool     = pool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts        = &dsl_;
    return vkAllocateDescriptorSets(vk, &dai, &set_) == VK_SUCCESS;
}

void VulkanSsaoPass::write_descriptor_set() {
    if (set_ == VK_NULL_HANDLE || g_buffer_ == nullptr) return;
    VkDevice vk = device_->get_device();

    std::array<VkDescriptorImageInfo, 5> infos{};
    infos[0].sampler     = gbuffer_sampler_;
    infos[0].imageView   = g_buffer_->get_position_view();
    infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    infos[1].sampler     = gbuffer_sampler_;
    infos[1].imageView   = g_buffer_->get_normal_view();
    infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    infos[2].sampler     = gbuffer_sampler_;
    infos[2].imageView   = g_buffer_->get_depth_view();
    infos[2].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    infos[3].imageView   = output_view_;
    infos[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    infos[4].sampler     = sampler_;             // RT reprojection history
    infos[4].imageView   = history_view_;
    infos[4].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    std::array<VkWriteDescriptorSet, 5> ws{};
    for (uint32_t i = 0; i < 3; ++i) {
        ws[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[i].dstSet          = set_;
        ws[i].dstBinding      = i;
        ws[i].descriptorCount = 1;
        ws[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ws[i].pImageInfo      = &infos[i];
    }
    ws[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ws[3].dstSet          = set_;
    ws[3].dstBinding      = 3;
    ws[3].descriptorCount = 1;
    ws[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ws[3].pImageInfo      = &infos[3];
    uint32_t write_count = 4;
    if (technique_ == AoTechnique::RT) {     // binding 5 only exists in RT layout
        ws[4].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[4].dstSet          = set_;
        ws[4].dstBinding      = 5;
        ws[4].descriptorCount = 1;
        ws[4].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ws[4].pImageInfo      = &infos[4];
        write_count = 5;
    }
    vkUpdateDescriptorSets(vk, write_count, ws.data(), 0, nullptr);
}

void VulkanSsaoPass::set_tlas(VkAccelerationStructureKHR tlas) {
    if (technique_ != AoTechnique::RT) return;
    if (tlas == tlas_ || tlas == VK_NULL_HANDLE) { tlas_ = tlas; return; }
    tlas_ = tlas;
    VkWriteDescriptorSetAccelerationStructureKHR as_write{};
    as_write.sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    as_write.accelerationStructureCount = 1;
    as_write.pAccelerationStructures    = &tlas_;
    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.pNext           = &as_write;
    w.dstSet          = set_;
    w.dstBinding      = 4;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    vkUpdateDescriptorSets(device_->get_device(), 1, &w, 0, nullptr);
}

bool VulkanSsaoPass::create_blur_pipeline() {
    VkDevice vk = device_->get_device();
    // Bindings: 0 raw SSAO, 1 normal, 2 depth, 3 storage image (blurred out).
    std::array<VkDescriptorSetLayoutBinding, 4> b{};
    for (uint32_t i = 0; i < 3; ++i) {
        b[i].binding         = i;
        b[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[i].descriptorCount = 1;
        b[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    b[3].binding         = 3;
    b[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    b[3].descriptorCount = 1;
    b[3].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = static_cast<uint32_t>(b.size());
    li.pBindings    = b.data();
    if (vkCreateDescriptorSetLayout(vk, &li, nullptr, &blur_dsl_) != VK_SUCCESS) return false;

    // Push constants: ivec2 + 4 floats + 1 int = 32 bytes.
    VkPushConstantRange pr{};
    pr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pr.size       = 32;
    VkPipelineLayoutCreateInfo pli{};
    pli.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount         = 1;
    pli.pSetLayouts            = &blur_dsl_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges    = &pr;
    if (vkCreatePipelineLayout(vk, &pli, nullptr, &blur_layout_) != VK_SUCCESS) return false;

    VkShaderModuleCreateInfo smi{};
    smi.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = kSsaoBlurSpv_size;
    smi.pCode    = kSsaoBlurSpv;
    VkShaderModule sm = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vk, &smi, nullptr, &sm) != VK_SUCCESS) return false;
    VkComputePipelineCreateInfo cpi{};
    cpi.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpi.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.module = sm;
    cpi.stage.pName  = "main";
    cpi.layout       = blur_layout_;
    VkResult r = vkCreateComputePipelines(vk, VK_NULL_HANDLE, 1, &cpi, nullptr, &blur_pipeline_);
    vkDestroyShaderModule(vk, sm, nullptr);
    return r == VK_SUCCESS;
}

bool VulkanSsaoPass::create_blur_descriptor_resources() {
    VkDevice vk = device_->get_device();
    std::array<VkDescriptorPoolSize, 2> ps{};
    ps[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps[0].descriptorCount = 3;
    ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;          ps[1].descriptorCount = 1;
    VkDescriptorPoolCreateInfo pi{};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets       = 1;
    pi.poolSizeCount = static_cast<uint32_t>(ps.size());
    pi.pPoolSizes    = ps.data();
    if (vkCreateDescriptorPool(vk, &pi, nullptr, &blur_pool_) != VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo dai{};
    dai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool     = blur_pool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts        = &blur_dsl_;
    return vkAllocateDescriptorSets(vk, &dai, &blur_set_) == VK_SUCCESS;
}

void VulkanSsaoPass::write_blur_descriptor_set() {
    if (blur_set_ == VK_NULL_HANDLE || g_buffer_ == nullptr) return;
    VkDevice vk = device_->get_device();

    std::array<VkDescriptorImageInfo, 4> infos{};
    infos[0].sampler     = sampler_;
    infos[0].imageView   = output_view_; // raw SSAO output
    infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    infos[1].sampler     = gbuffer_sampler_;
    infos[1].imageView   = g_buffer_->get_normal_view();
    infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    infos[2].sampler     = gbuffer_sampler_;
    infos[2].imageView   = g_buffer_->get_depth_view();
    infos[2].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    infos[3].imageView   = blurred_view_;
    infos[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    std::array<VkWriteDescriptorSet, 4> ws{};
    for (uint32_t i = 0; i < 3; ++i) {
        ws[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[i].dstSet          = blur_set_;
        ws[i].dstBinding      = i;
        ws[i].descriptorCount = 1;
        ws[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ws[i].pImageInfo      = &infos[i];
    }
    ws[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ws[3].dstSet          = blur_set_;
    ws[3].dstBinding      = 3;
    ws[3].descriptorCount = 1;
    ws[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ws[3].pImageInfo      = &infos[3];
    vkUpdateDescriptorSets(vk, static_cast<uint32_t>(ws.size()), ws.data(), 0, nullptr);
}

void VulkanSsaoPass::resize(uint32_t width, uint32_t height) {
    if (width == width_ && height == height_) return;
    width_  = width;
    height_ = height;
    destroy_output_image();
    create_output_image();
    write_descriptor_set();
    write_blur_descriptor_set();
    history_valid_ = false;   // recreated image → no valid AO history
}

void VulkanSsaoPass::execute(VkCommandBuffer cmd,
                              const glm::mat4& view,
                              const glm::mat4& proj) {
    if (!enabled_) {
        // When disabled, do nothing — the image holds whatever value it
        // had before. The lighting pass should fall back to its own AO
        // term via descriptor swap or a config flag.
        return;
    }
    if (pipeline_ == VK_NULL_HANDLE || cmd == VK_NULL_HANDLE) return;
    // RT mode: don't dispatch until a TLAS is bound (binding 4 would be an
    // unbound acceleration structure → driver hang).
    if (technique_ == AoTechnique::RT && tlas_ == VK_NULL_HANDLE) return;

    // Temporal accumulation (RT AO): accumulate whenever a valid history exists.
    // The shader reprojects the history by world position (convention-proof), so
    // it keeps converging while the camera MOVES — no static gate.
    const glm::mat4 vp = proj * view;
    const bool accumulate = (technique_ == AoTechnique::RT) && history_valid_;

    // Output is written fresh each frame (history is a separate image) → discard.
    VkImageMemoryBarrier b{};
    b.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout                   = VK_IMAGE_LAYOUT_GENERAL;
    b.image                       = output_image_;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);

    // First-ever frame: bring history into GENERAL so binding-5's layout is valid
    // (contents unused until history_valid_). Later frames leave it GENERAL.
    if (technique_ == AoTechnique::RT && !history_valid_) {
        VkImageMemoryBarrier hb{};
        hb.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        hb.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
        hb.newLayout        = VK_IMAGE_LAYOUT_GENERAL;
        hb.image            = history_image_;
        hb.subresourceRange = b.subresourceRange;
        hb.srcAccessMask    = 0;
        hb.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &hb);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            layout_, 0, 1, &set_, 0, nullptr);

    // Per-technique AO radius. RT now relies on temporal accumulation, so a
    // medium radius (6) gives presence + converges smooth. Screen-space
    // techniques want a small/medium world radius.
    float radius_val;
    switch (technique_) {
        case AoTechnique::RT:   radius_val = 6.0f;           break;
        case AoTechnique::HBAO:
        case AoTechnique::HDAO:
        case AoTechnique::GTAO: radius_val = 3.0f;           break;
        default:                radius_val = config_.radius; break;
    }
    const int32_t frame_idx = static_cast<int32_t>(frame_counter_ & 0xFFFu); // small for fract()

    if (technique_ == AoTechnique::RT) {
        // RT: current + previous view_proj for convention-proof reprojection.
        struct RtPC {
            float   cur_view_proj[16];
            float   prev_view_proj[16];
            int32_t output_w, output_h;
            float   radius, bias, intensity;
            int32_t frame, accumulate;
        } pc{};
        std::memcpy(pc.cur_view_proj,  &vp[0][0],              sizeof(pc.cur_view_proj));
        std::memcpy(pc.prev_view_proj, &prev_view_proj_[0][0], sizeof(pc.prev_view_proj));
        pc.output_w   = static_cast<int32_t>(width_);
        pc.output_h   = static_cast<int32_t>(height_);
        pc.radius     = radius_val;
        pc.bias       = config_.bias;
        pc.intensity  = config_.intensity;
        pc.frame      = frame_idx;
        pc.accumulate = accumulate ? 1 : 0;
        vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
    } else {
        // Screen-space techniques: single (current) view_proj.
        struct PC {
            float   view_proj[16];
            int32_t output_w, output_h;
            float   radius, bias, intensity;
            int32_t frame, accumulate;
        } pc{};
        std::memcpy(pc.view_proj, &vp[0][0], sizeof(pc.view_proj));
        pc.output_w   = static_cast<int32_t>(width_);
        pc.output_h   = static_cast<int32_t>(height_);
        pc.radius     = radius_val;
        pc.bias       = config_.bias;
        pc.intensity  = config_.intensity;
        pc.frame      = frame_idx;
        pc.accumulate = 0;
        vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
    }

    const uint32_t gx = (width_ + 7) / 8;
    const uint32_t gy = (height_ + 7) / 8;
    vkCmdDispatch(cmd, gx, gy, 1);

    // Transition raw output to SHADER_READ_ONLY so the blur compute can
    // sample it; transition the blurred image to GENERAL for compute write.
    b.image         = output_image_;
    b.oldLayout     = VK_IMAGE_LAYOUT_GENERAL;
    b.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);

    b.image         = blurred_image_;
    b.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);

    // Bilateral blur pass.
    if (blur_pipeline_ != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, blur_pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                blur_layout_, 0, 1, &blur_set_, 0, nullptr);
        struct BlurPC {
            int32_t output_w, output_h;
            float   depth_sigma;
            float   normal_sigma;
            int32_t radius;
            float   _pad0, _pad1, _pad2;
        } bpc{};
        bpc.output_w     = static_cast<int32_t>(width_);
        bpc.output_h     = static_cast<int32_t>(height_);
        // Looser + wider than before: the RT AO is stochastic, so the denoise
        // must reach across more pixels and tolerate more depth/normal variation
        // to actually clean the grain (tight sigmas were preserving the noise).
        bpc.depth_sigma  = 0.04f;  // was 0.01 — smooth across more depth slope
        bpc.normal_sigma = 0.30f;  // was 0.20 — smooth across more curvature
        bpc.radius       = 3;      // was 2 — 7x7 kernel
        vkCmdPushConstants(cmd, blur_layout_, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(bpc), &bpc);
        vkCmdDispatch(cmd, gx, gy, 1);
    }

    // Transition blurred to SHADER_READ_ONLY for the lighting pass.
    b.image         = blurred_image_;
    b.oldLayout     = VK_IMAGE_LAYOUT_GENERAL;
    b.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);

    // RT temporal reprojection: copy this frame's accumulated AO into the
    // history image so next frame can reproject + blend against it.
    if (technique_ == AoTechnique::RT) {
        VkImageMemoryBarrier cb{};
        cb.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        cb.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        cb.image         = output_image_;   // SHADER_READ_ONLY (blur read) -> TRANSFER_SRC
        cb.oldLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        cb.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        cb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        cb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &cb);

        cb.image         = history_image_;  // GENERAL (RT sampled it) -> TRANSFER_DST
        cb.oldLayout     = VK_IMAGE_LAYOUT_GENERAL;
        cb.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        cb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        cb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &cb);

        VkImageCopy region{};
        region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.extent         = { width_, height_, 1 };
        vkCmdCopyImage(cmd, output_image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       history_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        cb.image         = history_image_;  // TRANSFER_DST -> GENERAL (next frame's RT sample)
        cb.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        cb.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
        cb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        cb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &cb);
    }

    // Advance temporal state: this frame's vp becomes next frame's "previous"
    // (for reprojection), and the history is now valid so we accumulate.
    prev_view_proj_ = vp;
    history_valid_  = true;
    ++frame_counter_;
}

} // namespace gws::renderer::gpu
