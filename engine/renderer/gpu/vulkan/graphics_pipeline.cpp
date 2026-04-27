#include "graphics_pipeline.h"
#include "logging/logger.h"
#include <cassert>

namespace gws::renderer::gpu {

VulkanPipeline::VulkanPipeline(VkDevice device, VkPipeline pipeline, VkPipelineLayout layout)
    : device(device), pipeline(pipeline), layout(layout) {
}

VulkanPipeline::~VulkanPipeline() {
    if (pipeline != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
    }
    if (layout != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, layout, nullptr);
    }
}

PipelineBuilder::PipelineBuilder(VkDevice device) : device(device) {
    // Initialize default viewport
    viewport = {0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f};
    scissor = {{0, 0}, {1920, 1080}};
    has_viewport = true;
    has_scissor = true;
}

PipelineBuilder& PipelineBuilder::add_shader(VulkanShader* shader) {
    assert(shader && "Shader cannot be null");
    
    VkPipelineShaderStageCreateInfo stage_info{};
    stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_info.stage = shader->get_stage_flags();
    stage_info.module = shader->get_module();
    stage_info.pName = shader->get_entry_point().c_str();
    
    shader_stages.push_back(stage_info);
    return *this;
}

PipelineBuilder& PipelineBuilder::set_render_pass(VulkanRenderPass* pass) {
    assert(pass && "Render pass cannot be null");
    render_pass = pass;
    return *this;
}

PipelineBuilder& PipelineBuilder::add_vertex_binding(const VertexBindingDescription& binding) {
    vertex_bindings.push_back(binding);
    return *this;
}

PipelineBuilder& PipelineBuilder::add_vertex_attribute(const VertexAttributeDescription& attribute) {
    vertex_attributes.push_back(attribute);
    return *this;
}

PipelineBuilder& PipelineBuilder::set_viewport(float x, float y, float width, float height) {
    viewport = {x, y, width, height, 0.0f, 1.0f};
    has_viewport = true;
    return *this;
}

PipelineBuilder& PipelineBuilder::set_scissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    scissor = {{x, y}, {width, height}};
    has_scissor = true;
    return *this;
}

PipelineBuilder& PipelineBuilder::set_depth_test(bool enabled) {
    depth_test_enabled = enabled;
    return *this;
}

PipelineBuilder& PipelineBuilder::set_depth_write(bool enabled) {
    depth_write_enabled = enabled;
    return *this;
}

PipelineBuilder& PipelineBuilder::set_depth_compare(VkCompareOp compare_op) {
    depth_compare_op = compare_op;
    return *this;
}

PipelineBuilder& PipelineBuilder::set_cull_mode(VkCullModeFlags cull) {
    cull_mode = cull;
    return *this;
}

PipelineBuilder& PipelineBuilder::set_polygon_mode(VkPolygonMode mode) {
    polygon_mode = mode;
    return *this;
}

PipelineBuilder& PipelineBuilder::set_topology(VkPrimitiveTopology top) {
    topology = top;
    return *this;
}

std::unique_ptr<VulkanPipeline> PipelineBuilder::build() {
    assert(render_pass && "Render pass must be set");
    assert(!shader_stages.empty() && "At least one shader stage must be added");
    
    // Create pipeline layout
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 0;
    layout_info.pSetLayouts = nullptr;
    
    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &layout) != VK_SUCCESS) {
        GWS_LOG_ERROR("Failed to create pipeline layout");
        return nullptr;
    }
    
    // Vertex input state
    std::vector<VkVertexInputBindingDescription> vk_bindings;
    for (const auto& binding : vertex_bindings) {
        VkVertexInputBindingDescription vk_binding{};
        vk_binding.binding = binding.binding;
        vk_binding.stride = binding.stride;
        vk_binding.inputRate = binding.input_rate;
        vk_bindings.push_back(vk_binding);
    }
    
    std::vector<VkVertexInputAttributeDescription> vk_attributes;
    for (const auto& attribute : vertex_attributes) {
        VkVertexInputAttributeDescription vk_attr{};
        vk_attr.location = attribute.location;
        vk_attr.binding = attribute.binding;
        vk_attr.format = attribute.format;
        vk_attr.offset = attribute.offset;
        vk_attributes.push_back(vk_attr);
    }
    
    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = vk_bindings.size();
    vertex_input_info.pVertexBindingDescriptions = vk_bindings.data();
    vertex_input_info.vertexAttributeDescriptionCount = vk_attributes.size();
    vertex_input_info.pVertexAttributeDescriptions = vk_attributes.data();
    
    // Input assembly state
    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = topology;
    input_assembly.primitiveRestartEnable = VK_FALSE;
    
    // Rasterization state
    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.depthClampEnable = VK_FALSE;
    rasterization.rasterizerDiscardEnable = VK_FALSE;
    rasterization.polygonMode = polygon_mode;
    rasterization.cullMode = cull_mode;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.depthBiasEnable = VK_FALSE;
    rasterization.lineWidth = 1.0f;
    
    // Depth-stencil state
    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = depth_test_enabled ? VK_TRUE : VK_FALSE;
    depth_stencil.depthWriteEnable = depth_write_enabled ? VK_TRUE : VK_FALSE;
    depth_stencil.depthCompareOp = depth_compare_op;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.stencilTestEnable = VK_FALSE;
    
    // Color blend state
    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_FALSE;
    
    VkPipelineColorBlendStateCreateInfo color_blend{};
    color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.logicOpEnable = VK_FALSE;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments = &color_blend_attachment;
    
    // Viewport state
    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = has_viewport ? &viewport : nullptr;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = has_scissor ? &scissor : nullptr;
    
    // Multisample state
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisample.sampleShadingEnable = VK_FALSE;
    
    // Graphics pipeline
    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = shader_stages.size();
    pipeline_info.pStages = shader_stages.data();
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterization;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blend;
    pipeline_info.layout = layout;
    pipeline_info.renderPass = render_pass->get_handle();
    pipeline_info.subpass = 0;
    
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline) != VK_SUCCESS) {
        GWS_LOG_ERROR("Failed to create graphics pipeline");
        vkDestroyPipelineLayout(device, layout, nullptr);
        return nullptr;
    }
    
    auto result = std::make_unique<VulkanPipeline>(device, pipeline, layout);
    GWS_LOG_INFO("✅ Graphics pipeline created ({} shaders)", shader_stages.size());
    
    return result;
}

}  // namespace gws::renderer::gpu
