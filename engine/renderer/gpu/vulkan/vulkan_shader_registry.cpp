/**
 * @file vulkan_shader_registry.cpp
 * @brief Shader registry implementation
 */

#include <chrono>
#include "vulkan_shader_registry.h"
#include "vulkan_device.h"
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#include <windows.h>   // GetModuleFileNameW, for the shader-override directory
#endif
#ifdef _MSC_VER
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <cstdint>
#define GWS_HAS_GLSLANG 1
#endif

namespace gws::renderer::gpu {

VulkanShaderRegistry::~VulkanShaderRegistry() {
    cleanup_cache();
}

bool VulkanShaderRegistry::initialize(VulkanDevice* device) {
    device_ = device;
#ifdef GWS_HAS_GLSLANG
    glslang::InitializeProcess();
#endif
    spdlog::info("VulkanShaderRegistry initialized");
    return true;
}

// Runtime-GLSL cost accounting. 17 call sites compile GLSL at pass-creation
// time; this measures what that actually costs so the decision to convert them
// to precompiled SPIR-V is evidence-led rather than inherited from another
// engine's problem. Read by the editor's --startup-probe.
namespace {
    double g_glsl_compile_ms = 0.0;
    int    g_glsl_compiles   = 0;
}
double gws_runtime_glsl_ms()     { return g_glsl_compile_ms; }
int    gws_runtime_glsl_count()  { return g_glsl_compiles; }

std::shared_ptr<ShaderModule> VulkanShaderRegistry::compile_glsl(const std::string& source,
                                                                 ShaderStage stage,
                                                                 const std::string& name) {
    // Check cache
    auto cached = get_shader(name);
    if (cached) {
        return cached;
    }
    const auto gws_t0 = std::chrono::steady_clock::now();
    struct Acc {
        std::chrono::steady_clock::time_point t;
        ~Acc() {
            g_glsl_compile_ms += std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - t).count();
            ++g_glsl_compiles;
        }
    } gws_acc{gws_t0};
    
    try {
        // Compile GLSL to SPIR-V
        auto spirv = compile_glsl_to_spirv(source, stage, name);

        // Empty means "runtime GLSL compilation is not available in this build"
        // — an expected condition, not a failure. The caller falls back to
        // precompiled SPIR-V. Reported ONCE at info level: previously each of
        // the 17 call sites logged `[error] Failed to compile shader ...` on
        // every startup, 14 lines of noise describing normal behaviour. An
        // error channel that always fires is an error channel nobody reads.
        if (spirv.empty()) {
            static bool announced = false;
            if (!announced) {
                announced = true;
                spdlog::info("[shaders] runtime GLSL compilation unavailable in this build; "
                             "using precompiled SPIR-V (this is the normal path)");
            }
            return nullptr;
        }

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

namespace {

// ---------------------------------------------------------------------------
// Shader override: prefer a .spv sitting on disk over the array baked into the
// binary.
//
// WHY. Editing a shader today costs a measured ~18 s: regenerate the header with
// spv_to_header.py, then rebuild, because touching a generated header forces a
// recompile of everything that includes it plus a relink of the editor. The
// budget for shader-edit-to-screen is 2 s. Loading the .spv directly removes the
// header regeneration AND the rebuild, leaving glslangValidator plus a restart.
//
// SHIPPING BEHAVIOUR IS UNCHANGED. A release install has no override directory,
// so every shader comes from the baked array exactly as before. This adds a
// development path; it does not move the shipping one.
// ---------------------------------------------------------------------------
std::filesystem::path shader_override_dir() {
    static const std::filesystem::path dir = [] {
        namespace fs = std::filesystem;
        std::error_code ec;
        // Explicit wins: an env var is how CI and a graphics programmer point
        // this at a scratch directory without touching the install.
        if (const char* e = std::getenv("GWS_SHADER_DIR")) {
            const fs::path p(e);
            if (fs::is_directory(p, ec)) return p;
        }
        // Then a `shaders/` folder next to the executable, which is where a
        // developer would drop hand-compiled output.
        fs::path exe_dir;
#ifdef _WIN32
        wchar_t buf[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, buf, MAX_PATH))
            exe_dir = fs::path(buf).parent_path();
#endif
        if (!exe_dir.empty()) {
            const fs::path p = exe_dir / "shaders";
            if (fs::is_directory(p, ec)) return p;
        }
        return fs::path{};
    }();
    return dir;
}

// Read <override_dir>/<name>.spv, or empty if there is nothing usable there.
std::vector<uint32_t> load_spirv_override(const std::string& name) {
    namespace fs = std::filesystem;
    const fs::path dir = shader_override_dir();
    if (dir.empty()) return {};

    std::error_code ec;
    const fs::path file = dir / (name + ".spv");
    if (!fs::exists(file, ec)) return {};

    const auto bytes = fs::file_size(file, ec);
    // SPIR-V is a stream of 32-bit words and starts with the magic number
    // 0x07230203. Checking both means a truncated or half-written file is
    // rejected here rather than crashing the driver, which is the failure mode
    // that would make people distrust the whole feature.
    if (ec || bytes < 20 || (bytes % 4) != 0) {
        spdlog::warn("[shaders] ignoring override '{}': not a whole number of SPIR-V words",
                     file.string());
        return {};
    }

    std::ifstream f(file, std::ios::binary);
    if (!f) return {};
    std::vector<uint32_t> code(static_cast<size_t>(bytes) / 4);
    f.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(bytes));
    if (!f) {
        spdlog::warn("[shaders] ignoring override '{}': short read", file.string());
        return {};
    }
    if (code[0] != 0x07230203u) {
        spdlog::warn("[shaders] ignoring override '{}': bad SPIR-V magic 0x{:08x}",
                     file.string(), code[0]);
        return {};
    }
    return code;
}

}  // namespace

std::shared_ptr<ShaderModule> VulkanShaderRegistry::create_from_spirv(const uint32_t* data,
                                                                       uint32_t size_bytes,
                                                                       ShaderStage stage,
                                                                       const std::string& name) {
    auto cached = get_shader(name);
    if (cached) return cached;

    // A .spv on disk beats the baked array. Anything wrong with the file falls
    // back to the array rather than failing the pass, so a bad override degrades
    // to "my edit did not apply" instead of a black screen.
    const std::vector<uint32_t> override_code = load_spirv_override(name);
    const bool overridden = !override_code.empty();
    if (overridden) {
        data       = override_code.data();
        size_bytes = static_cast<uint32_t>(override_code.size() * sizeof(uint32_t));
        spdlog::info("[shaders] '{}' loaded from disk override ({} bytes)", name, size_bytes);
    }

    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = size_bytes;
    ci.pCode    = data;

    VkShaderModule module;
    if (vkCreateShaderModule(device_->get_device(), &ci, nullptr, &module) != VK_SUCCESS) {
        spdlog::error("create_from_spirv: failed to create VkShaderModule for {}", name);
        return nullptr;
    }

    auto shader = std::make_shared<ShaderModule>();
    shader->handle = module;
    shader->name   = name;
    shader->stage  = stage;
    shader->spirv_code.assign(data, data + size_bytes / sizeof(uint32_t));
    shader_cache_[name] = shader;

    if (!overridden)
        spdlog::debug("Loaded pre-compiled SPIR-V: {} ({} bytes)", name, size_bytes);
    return shader;
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
#ifdef GWS_HAS_GLSLANG
    EShLanguage language;
    switch (stage) {
        case ShaderStage::Vertex:   language = EShLangVertex;   break;
        case ShaderStage::Fragment: language = EShLangFragment; break;
        case ShaderStage::Geometry: language = EShLangGeometry; break;
        case ShaderStage::Compute:  language = EShLangCompute;  break;
        default: throw std::runtime_error("Unsupported shader stage");
    }

    glslang::TShader shader(language);
    const char* source_cstr = source.c_str();
    shader.setStrings(&source_cstr, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, language, glslang::EShClientVulkan, 450);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);

    if (!shader.parse(GetDefaultResources(), 450, false, EShMsgDefault)) {
        std::string error = "Shader compilation failed:\n";
        error += shader.getInfoLog(); error += "\n"; error += shader.getInfoDebugLog();
        throw std::runtime_error(error);
    }

    glslang::TProgram program;
    program.addShader(&shader);

    if (!program.link(EShMsgDefault)) {
        std::string error = "Shader linking failed:\n";
        error += program.getInfoLog(); error += "\n"; error += program.getInfoDebugLog();
        throw std::runtime_error(error);
    }

    std::vector<uint32_t> spirv;
    spv::SpvBuildLogger logger;
    glslang::GlslangToSpv(*program.getIntermediate(language), spirv, &logger);

    if (spirv.empty()) throw std::runtime_error("Failed to generate SPIR-V code");
    return spirv;

#else
    // Not an error, and not exceptional. This toolchain builds without glslang,
    // so runtime GLSL compilation is simply unavailable and every call site
    // falls back to precompiled SPIR-V — which is the path the engine actually
    // ships on. Returning empty (rather than throwing) says "unavailable" once,
    // instead of raising 14 exceptions per startup that are all caught and all
    // logged as errors. See the note in compile_glsl().
    (void)source; (void)stage; (void)name;
    return {};
#endif
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
