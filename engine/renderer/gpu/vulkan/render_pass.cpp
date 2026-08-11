#include "render_pass.h"
#include "logging/logger.h"
#include <cassert>
#include <cstdint>

namespace gws::renderer::gpu {

// ============================================================================
// VulkanRenderPass Implementation
// ============================================================================

VulkanRenderPass::VulkanRenderPass(VkDevice device, VkRenderPass render_pass,
                                  const std::string& debug_name)
    : device(device), render_pass(render_pass), debug_name(debug_name) {
}

VulkanRenderPass::~VulkanRenderPass() {
    if (render_pass != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, render_pass, nullptr);
    }
}

// ============================================================================
// RenderPassBuilder Implementation
// ============================================================================

RenderPassBuilder::RenderPassBuilder(VkDevice device) : device(device) {
}

RenderPassBuilder& RenderPassBuilder::add_attachment(const AttachmentDescription& desc) {
    attachments.push_back(desc);
    return *this;
}

RenderPassBuilder& RenderPassBuilder::add_subpass(const std::string& name) {
    subpasses.push_back(SubpassDescription{.debug_name = name});
    return *this;
}

RenderPassBuilder& RenderPassBuilder::add_color_attachment(uint32_t attachment_index) {
    assert(!subpasses.empty() && "Must add subpass before adding attachments");
    
    ColorAttachmentReference ref;
    ref.attachment = attachment_index;
    subpasses.back().color_attachments.push_back(ref);
    
    return *this;
}

RenderPassBuilder& RenderPassBuilder::set_depth_attachment(uint32_t attachment_index) {
    assert(!subpasses.empty() && "Must add subpass before adding attachments");
    
    subpasses.back().depth_attachment.attachment = attachment_index;
    subpasses.back().has_depth = true;
    
    return *this;
}

uint32_t RenderPassBuilder::get_color_attachment_count() const {
    if (subpasses.empty()) return 0;
    return subpasses.back().color_attachments.size();
}

std::unique_ptr<VulkanRenderPass> RenderPassBuilder::build(const std::string& debug_name) {
    if (attachments.empty() || subpasses.empty()) {
        GWS_LOG_ERROR("Cannot build render pass: no attachments or subpasses");
        return nullptr;
    }
    
    // Convert attachment descriptions
    std::vector<VkAttachmentDescription> vk_attachments;
    for (const auto& att : attachments) {
        VkAttachmentDescription vk_att{};
        vk_att.format = att.format;
        vk_att.samples = VK_SAMPLE_COUNT_1_BIT;
        vk_att.loadOp = att.load_op;
        vk_att.storeOp = att.store_op;
        vk_att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        vk_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        vk_att.initialLayout = att.initial_layout;
        vk_att.finalLayout = att.final_layout;
        vk_attachments.push_back(vk_att);
    }
    
    // Convert subpass descriptions
    std::vector<VkAttachmentReference> color_refs;
    std::vector<VkAttachmentReference> depth_refs;
    std::vector<VkAttachmentReference> input_refs;
    
    std::vector<VkAttachmentReference> all_depth_refs;
    
    std::vector<VkSubpassDescription> vk_subpasses;
    for (const auto& subpass : subpasses) {
        // Collect color attachment references
        for (const auto& color_ref : subpass.color_attachments) {
            VkAttachmentReference ref;
            ref.attachment = color_ref.attachment;
            ref.layout = color_ref.layout;
            color_refs.push_back(ref);
        }
        
        // Setup depth attachment reference
        VkAttachmentReference* depth_ptr = nullptr;
        if (subpass.has_depth) {
            VkAttachmentReference ref;
            ref.attachment = subpass.depth_attachment.attachment;
            ref.layout = subpass.depth_attachment.layout;
            all_depth_refs.push_back(ref);
            depth_ptr = &all_depth_refs.back();
        }
        
        // Create subpass description
        VkSubpassDescription vk_subpass{};
        vk_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        vk_subpass.colorAttachmentCount = subpass.color_attachments.size();
        
        if (!subpass.color_attachments.empty()) {
            vk_subpass.pColorAttachments = &color_refs[color_refs.size() - subpass.color_attachments.size()];
        }
        
        if (subpass.has_depth) {
            vk_subpass.pDepthStencilAttachment = depth_ptr;
        }
        
        vk_subpasses.push_back(vk_subpass);
    }
    
    // Create render pass
    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = vk_attachments.size();
    render_pass_info.pAttachments = vk_attachments.data();
    render_pass_info.subpassCount = vk_subpasses.size();
    render_pass_info.pSubpasses = vk_subpasses.data();
    
    VkRenderPass render_pass = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &render_pass_info, nullptr, &render_pass) != VK_SUCCESS) {
        GWS_LOG_ERROR("Failed to create render pass");
        return nullptr;
    }
    
    auto pass = std::make_unique<VulkanRenderPass>(device, render_pass, debug_name);
    GWS_LOG_INFO("✅ Render pass created: {} ({} attachments, {} subpasses)", 
                debug_name, attachments.size(), subpasses.size());
    
    return pass;
}

// ============================================================================
// Common Render Passes
// ============================================================================

std::unique_ptr<VulkanRenderPass> RenderPasses::create_simple_color_pass(
    VkDevice device,
    VkFormat color_format,
    const std::string& debug_name) {
    
    RenderPassBuilder builder(device);
    
    builder.add_attachment({
        .format = color_format,
        .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .store_op = VK_ATTACHMENT_STORE_OP_STORE,
        .initial_layout = VK_IMAGE_LAYOUT_UNDEFINED,
        .final_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .debug_name = "ColorOutput"
    });
    
    builder.add_subpass("Main")
           .add_color_attachment(0);
    
    return builder.build(debug_name);
}

std::unique_ptr<VulkanRenderPass> RenderPasses::create_g_buffer_pass(
    VkDevice device,
    VkFormat color_format,
    VkFormat depth_format,
    const std::string& debug_name) {
    
    RenderPassBuilder builder(device);
    
    // Add 4 G-Buffer color attachments
    builder.add_attachment({
        .format = color_format,
        .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .store_op = VK_ATTACHMENT_STORE_OP_STORE,
        .initial_layout = VK_IMAGE_LAYOUT_UNDEFINED,
        .final_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .debug_name = "Albedo"
    });
    
    builder.add_attachment({
        .format = color_format,
        .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .store_op = VK_ATTACHMENT_STORE_OP_STORE,
        .initial_layout = VK_IMAGE_LAYOUT_UNDEFINED,
        .final_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .debug_name = "Normal"
    });
    
    builder.add_attachment({
        .format = color_format,
        .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .store_op = VK_ATTACHMENT_STORE_OP_STORE,
        .initial_layout = VK_IMAGE_LAYOUT_UNDEFINED,
        .final_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .debug_name = "PBR"
    });
    
    builder.add_attachment({
        .format = color_format,
        .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .store_op = VK_ATTACHMENT_STORE_OP_STORE,
        .initial_layout = VK_IMAGE_LAYOUT_UNDEFINED,
        .final_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .debug_name = "Emission"
    });
    
    // Add depth attachment
    builder.add_attachment({
        .format = depth_format,
        .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .store_op = VK_ATTACHMENT_STORE_OP_STORE,
        .initial_layout = VK_IMAGE_LAYOUT_UNDEFINED,
        .final_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        .debug_name = "Depth"
    });
    
    // Add subpass with all attachments
    builder.add_subpass("GBufferGeneration")
           .add_color_attachment(0)
           .add_color_attachment(1)
           .add_color_attachment(2)
           .add_color_attachment(3)
           .set_depth_attachment(4);
    
    return builder.build(debug_name);
}

}  // namespace gws::renderer::gpu
