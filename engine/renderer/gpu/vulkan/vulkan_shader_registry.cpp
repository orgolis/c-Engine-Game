/**
 * @file vulkan_shader_registry.cpp
 * @brief Shader registry implementation
 */

#include "vulkan_shader_registry.h"
#include "vulkan_device.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

namespace gws::renderer::gpu {

VulkanShaderRegistry::~VulkanShaderRegistry() {
    cleanup_cache();
}

bool VulkanShaderRegistry::initialize(VulkanDevice* device) {
    device_ = device;
    
    // Initialize glslang
    glslang::InitializeProcess();
    
    spdlog::info("VulkanShaderRegistry initialized");
    return true;
}

std::shared_ptr<ShaderModule> VulkanShaderRegistry::compile_glsl(const std::string& source,
                                                                 ShaderStage stage,
                                                                 const std::string& name) {
    // Check cache
    auto cached = get_shader(name);
    if (cached) {
        return cached;
    }
    
    try {
        // Compile GLSL to SPIR-V
        auto spirv = compile_glsl_to_spirv(source, stage, name);
        
        // Create shader module
        VkShaderModuleCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        create_info.codeSize = spirv.size() * sizeof(uint32_t);
        create_info.pCode = spirv.data();
        
        VkShaderModule module;
        if (vkCreateShaderModule(device_->get_device(), &create_info, nullptr, &module) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create shader module");
        }
        
        auto shader = std::make_shared<ShaderModule>();
        shader->handle = module;
        shader->name = name;
        shader->stage = stage;
        shader->spirv_code = spirv;
        
        // Cache the shader
        shader_cache_[name] = shader;
        
        spdlog::debug("Compiled shader: {} ({} bytes SPIR-V)", name, spirv.size() * sizeof(uint32_t));
        return shader;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to compile shader {}: {}", name, e.what());
        return nullptr;
    }
}

std::shared_ptr<ShaderModule> VulkanShaderRegistry::load_shader(const std::string& path, 
                                                               ShaderStage stage) {
    // Check cache
    auto cached = get_shader(path);
    if (cached) {
        return cached;
    }
    
    try {
        // Read file
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open shader file: " + path);
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string source = buffer.str();
        file.close();
        
        // Compile
        return compile_glsl(source, stage, path);
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to load shader from {}: {}", path, e.what());
        return nullptr;
    }
}

std::shared_ptr<ShaderModule> VulkanShaderRegistry::get_shader(const std::string& name) {
    auto it = shader_cache_.find(name);
    if (it != shader_cache_.end()) {
        return it->second;
    }
    return nullptr;
}

VkPipeline VulkanShaderRegistry::create_graphics_pipeline(const PipelineShaders& shaders,
                                                        VkPipelineLayout layout,
                                                        VkRenderPass render_pass) {
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    
    if (shaders.vertex) {
        VkPipelineShaderStageCreateInfo stage{};
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        stage.module = shaders.vertex->handle;
        stage.pName = "main";
        stages.push_back(stage);
    }
    
    if (shaders.fragment) {
        VkPipelineShaderStageCreateInfo stage{};
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stage.module = shaders.fragment->handle;
        stage.pName = "main";
        stages.push_back(stage);
    }
    
    if (shaders.geometry) {
        VkPipelineShaderStageCreateInfo stage{};
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
        stage.module = shaders.geometry->handle;
        stage.pName = "main";
        stages.push_back(stage);
    }
    
    // Vertex input (empty for now)
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    
    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;
    
    // Viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = 1920.0f;
    viewport.height = 1080.0f;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {1920, 1080};
    
    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;
    
    // Rasterization
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    
    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    // Color blending
    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_FALSE;
    
    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;
    
    // Depth stencil
    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.stencilTestEnable = VK_FALSE;
    
    // Create pipeline
    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = stages.size();
    pipeline_info.pStages = stages.data();
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.layout = layout;
    pipeline_info.renderPass = render_pass;
    pipeline_info.subpass = 0;
    
    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(device_->get_device(), VK_NULL_HANDLE, 1, &pipeline_info, 
                                 nullptr, &pipeline) != VK_SUCCESS) {
        spdlog::error("Failed to create graphics pipeline");
        return VK_NULL_HANDLE;
    }
    
    return pipeline;
}

std::string VulkanShaderRegistry::get_builtin_shader(const std::string& name) {
    // Built-in shaders embedded as strings
    // In production, these would be loaded from actual files
    // For now, return placeholder strings
    
    if (name == "geometry_pass.vert") {
        return R"(
#version 450
layout(location = 0) in vec3 inPosition;
void main() {
    gl_Position = vec4(inPosition, 1.0);
}
)";
    }
    
    spdlog::warn("Built-in shader not found: {}", name);
    return "";
}

void VulkanShaderRegistry::clear_cache() {
    cleanup_cache();
}

std::vector<uint32_t> VulkanShaderRegistry::compile_glsl_to_spirv(const std::string& source,
                                                                  ShaderStage stage,
                                                                  const std::string& name) {
    // Determine glslang shader type
    EShLanguage language;
    switch (stage) {
        case ShaderStage::Vertex:
            language = EShLangVertex;
            break;
        case ShaderStage::Fragment:
            language = EShLangFragment;
            break;
        case ShaderStage::Geometry:
            language = EShLangGeometry;
            break;
        case ShaderStage::Compute:
            language = EShLangCompute;
            break;
        default:
            throw std::runtime_error("Unsupported shader stage");
    }
    
    // Create shader
    glslang::TShader shader(language);
    const char* source_cstr = source.c_str();
    shader.setStrings(&source_cstr, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, language, glslang::EShClientVulkan, 450);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
    
    // Compile
    if (!shader.parse(&glslang::DefaultTBuiltInResource, 450, false, EShMsgDefault)) {
        std::string error = "Shader compilation failed:\n";
        error += shader.getInfoLog();
        error += "\n";
        error += shader.getInfoDebugLog();
        throw std::runtime_error(error);
    }
    
    // Link program
    glslang::TProgram program;
    program.addShader(&shader);
    
    if (!program.link(EShMsgDefault)) {
        std::string error = "Shader linking failed:\n";
        error += program.getInfoLog();
        error += "\n";
        error += program.getInfoDebugLog();
        throw std::runtime_error(error);
    }
    
    // Convert to SPIR-V
    std::vector<uint32_t> spirv;
    spv::SpvBuildLogger logger;
    glslang::GlslangToSpv(*program.getIntermediate(language), spirv, &logger);
    
    if (spirv.empty()) {
        throw std::runtime_error("Failed to generate SPIR-V code");
    }
    
    return spirv;
}

VkShaderStageFlagBits VulkanShaderRegistry::stage_to_vk(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:       return VK_SHADER_STAGE_VERTEX_BIT;
        case ShaderStage::Fragment:     return VK_SHADER_STAGE_FRAGMENT_BIT;
        case ShaderStage::Geometry:     return VK_SHADER_STAGE_GEOMETRY_BIT;
        case ShaderStage::Compute:      return VK_SHADER_STAGE_COMPUTE_BIT;
        case ShaderStage::TessControl:  return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        case ShaderStage::TessEval:     return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        default:                        return VK_SHADER_STAGE_VERTEX_BIT;
    }
}

void VulkanShaderRegistry::cleanup_cache() {
    VkDevice device = device_ ? device_->get_device() : VK_NULL_HANDLE;
    if (!device) return;
    
    for (auto& [name, shader] : shader_cache_) {
        if (shader && shader->handle != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, shader->handle, nullptr);
        }
    }
    shader_cache_.clear();
}

} // namespace gws::renderer::gpu
