#pragma once

#include "shader_compiler.h"
#include "render_pass.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <glm/glm.hpp>

namespace gws::renderer::gpu {

/**
 * @brief Vertex binding description
 */
struct VertexBindingDescription {
    uint32_t binding;
    uint32_t stride;
    VkVertexInputRate input_rate = VK_VERTEX_INPUT_RATE_VERTEX;
};

/**
 * @brief Vertex attribute description
 */
struct VertexAttributeDescription {
    uint32_t location;
    uint32_t binding;
    VkFormat format;
    uint32_t offset;
};

/**
 * @brief Vulkan Graphics Pipeline wrapper
 */
class VulkanPipeline {
public:
    VulkanPipeline(VkDevice device, VkPipeline pipeline, VkPipelineLayout layout);
    ~VulkanPipeline();
    
    VkPipeline get_handle() const { return pipeline; }
    VkPipelineLayout get_layout() const { return layout; }

private:
    VkDevice device;
    VkPipeline pipeline;
    VkPipelineLayout layout;
};

/**
 * @brief Graphics Pipeline Builder - Fluent API for pipeline creation
 * 
 * Example:
 * ```cpp
 * auto pipeline = PipelineBuilder(device)
 *     .add_shader(vertex_shader)
 *     .add_shader(fragment_shader)
 *     .set_render_pass(render_pass)
 *     .set_viewport(0, 0, 1920, 1080)
 *     .enable_depth_test()
 *     .set_cull_mode(VK_CULL_MODE_BACK_BIT)
 *     .build();
 * ```
 */
class PipelineBuilder {
public:
    PipelineBuilder(VkDevice device);
    ~PipelineBuilder() = default;
    
    /**
     * @brief Add a shader stage to pipeline
     */
    PipelineBuilder& add_shader(VulkanShader* shader);
    
    /**
     * @brief Set render pass for pipeline
     */
    PipelineBuilder& set_render_pass(VulkanRenderPass* render_pass);
    
    /**
     * @brief Add vertex binding description
     */
    PipelineBuilder& add_vertex_binding(const VertexBindingDescription& binding);
    
    /**
     * @brief Add vertex attribute description
     */
    PipelineBuilder& add_vertex_attribute(const VertexAttributeDescription& attribute);
    
    /**
     * @brief Set viewport
     */
    PipelineBuilder& set_viewport(float x, float y, float width, float height);
    
    /**
     * @brief Set scissor rect
     */
    PipelineBuilder& set_scissor(int32_t x, int32_t y, uint32_t width, uint32_t height);
    
    /**
     * @brief Enable/disable depth testing
     */
    PipelineBuilder& set_depth_test(bool enabled);
    
    /**
     * @brief Enable/disable depth write
     */
    PipelineBuilder& set_depth_write(bool enabled);
    
    /**
     * @brief Set depth compare operation
     */
    PipelineBuilder& set_depth_compare(VkCompareOp compare_op);
    
    /**
     * @brief Set cull mode
     */
    PipelineBuilder& set_cull_mode(VkCullModeFlags cull_mode);
    
    /**
     * @brief Set polygon mode (fill, line, point)
     */
    PipelineBuilder& set_polygon_mode(VkPolygonMode polygon_mode);
    
    /**
     * @brief Set topology (triangle, line, point, etc.)
     */
    PipelineBuilder& set_topology(VkPrimitiveTopology topology);
    
    /**
     * @brief Build the pipeline
     */
    std::unique_ptr<VulkanPipeline> build();

private:
    VkDevice device;
    std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
    VulkanRenderPass* render_pass = nullptr;
    
    std::vector<VertexBindingDescription> vertex_bindings;
    std::vector<VertexAttributeDescription> vertex_attributes;
    
    VkViewport viewport{};
    VkRect2D scissor{};
    bool has_viewport = false;
    bool has_scissor = false;
    
    // Fixed-function state
    bool depth_test_enabled = true;
    bool depth_write_enabled = true;
    VkCompareOp depth_compare_op = VK_COMPARE_OP_LESS;
    VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
    VkPolygonMode polygon_mode = VK_POLYGON_MODE_FILL;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
};

}  // namespace gws::renderer::gpu
