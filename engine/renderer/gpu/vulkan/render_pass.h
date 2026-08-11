#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <string>
#include <cstdint>

namespace gws::renderer::gpu {

/**
 * @brief Describes a render pass attachment
 */
struct AttachmentDescription {
    VkFormat format;
    VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
    VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE;
    VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout final_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    std::string debug_name;
};

/**
 * @brief Describes a subpass color attachment reference
 */
struct ColorAttachmentReference {
    uint32_t attachment;
    VkImageLayout layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
};

/**
 * @brief Describes a subpass depth attachment reference
 */
struct DepthAttachmentReference {
    uint32_t attachment;
    VkImageLayout layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
};

/**
 * @brief Describes a subpass
 */
struct SubpassDescription {
    std::vector<ColorAttachmentReference> color_attachments;
    DepthAttachmentReference depth_attachment{~0u};
    bool has_depth = false;
    std::string debug_name;
};

/**
 * @brief Vulkan Render Pass wrapper
 */
class VulkanRenderPass {
public:
    VulkanRenderPass(VkDevice device, VkRenderPass render_pass,
                    const std::string& debug_name = "RenderPass");
    ~VulkanRenderPass();
    
    VkRenderPass get_handle() const { return render_pass; }
    const std::string& get_debug_name() const { return debug_name; }

private:
    VkDevice device;
    VkRenderPass render_pass;
    std::string debug_name;
};

/**
 * @brief Render Pass Builder - Declarative render pass creation
 * 
 * Example usage:
 * ```cpp
 * RenderPassBuilder builder(device);
 * builder.add_attachment({VK_FORMAT_R8G8B8A8_SRGB, ...})
 *        .add_attachment({VK_FORMAT_D32_SFLOAT, ...})
 *        .add_subpass()
 *          .add_color_attachment(0)
 *          .set_depth_attachment(1);
 * auto pass = builder.build();
 * ```
 */
class RenderPassBuilder {
public:
    RenderPassBuilder(VkDevice device);
    ~RenderPassBuilder() = default;
    
    /**
     * @brief Add an attachment description
     */
    RenderPassBuilder& add_attachment(const AttachmentDescription& desc);
    
    /**
     * @brief Add a subpass
     */
    RenderPassBuilder& add_subpass(const std::string& name = "Subpass");
    
    /**
     * @brief Add color attachment to current subpass
     */
    RenderPassBuilder& add_color_attachment(uint32_t attachment_index);
    
    /**
     * @brief Set depth attachment for current subpass
     */
    RenderPassBuilder& set_depth_attachment(uint32_t attachment_index);
    
    /**
     * @brief Build the render pass
     */
    std::unique_ptr<VulkanRenderPass> build(const std::string& debug_name = "RenderPass");
    
    /**
     * @brief Get number of color attachments in current subpass
     */
    uint32_t get_color_attachment_count() const;

private:
    VkDevice device;
    std::vector<AttachmentDescription> attachments;
    std::vector<SubpassDescription> subpasses;
};

/**
 * @brief Common render passes
 */
class RenderPasses {
public:
    /**
     * @brief Create simple color output render pass (for swapchain)
     */
    static std::unique_ptr<VulkanRenderPass> create_simple_color_pass(
        VkDevice device,
        VkFormat color_format,
        const std::string& debug_name = "SimpleColorPass");
    
    /**
     * @brief Create deferred rendering G-Buffer render pass
     * (4 color + 1 depth attachment)
     */
    static std::unique_ptr<VulkanRenderPass> create_g_buffer_pass(
        VkDevice device,
        VkFormat color_format,
        VkFormat depth_format,
        const std::string& debug_name = "GBufferPass");
};

}  // namespace gws::renderer::gpu
