#include "shader_compiler.h"
#include "logging/logger.h"
#include <fstream>

// glslang is only available when linking against MSVC-built Vulkan SDK libs.
// GCC/MinGW cannot link MSVC-ABI static libraries, so runtime GLSL->SPIR-V
// compilation is disabled on those toolchains. Pre-compiled SPIR-V must be used.
#ifdef _MSC_VER
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <cstdint>
#define GWS_HAS_GLSLANG 1
#endif

namespace gws::renderer::gpu {

// ============================================================================
// VulkanShader Implementation (toolchain-independent)
// ============================================================================

VulkanShader::VulkanShader(VkDevice device, VkShaderModule module, ShaderType type,
                          const std::string& entry_point)
    : device(device), module(module), type(type), entry_point(entry_point) {
}

VulkanShader::~VulkanShader() {
    if (module != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, module, nullptr);
    }
}

VkShaderStageFlagBits VulkanShader::get_stage_flags() const {
    return ShaderCompiler::get_vk_stage(type);
}

// ============================================================================
// ShaderCompiler Implementation
// ============================================================================

ShaderCompiler::ShaderCompiler() {
#ifdef GWS_HAS_GLSLANG
    glslang::InitializeProcess();
    GWS_LOG_INFO("glslang shader compiler initialized");
#else
    GWS_LOG_WARN("Shader compiler built without glslang (GCC/MinGW). Use pre-compiled SPIR-V.");
#endif
}

ShaderCompiler::~ShaderCompiler() {
#ifdef GWS_HAS_GLSLANG
    glslang::FinalizeProcess();
#endif
}

VkShaderStageFlagBits ShaderCompiler::get_vk_stage(ShaderType type) {
    switch (type) {
        case ShaderType::VERTEX:
            return VK_SHADER_STAGE_VERTEX_BIT;
        case ShaderType::FRAGMENT:
            return VK_SHADER_STAGE_FRAGMENT_BIT;
        case ShaderType::GEOMETRY:
            return VK_SHADER_STAGE_GEOMETRY_BIT;
        case ShaderType::TESSELLATION_CONTROL:
            return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        case ShaderType::TESSELLATION_EVALUATION:
            return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        case ShaderType::COMPUTE:
            return VK_SHADER_STAGE_COMPUTE_BIT;
        default:
            return VK_SHADER_STAGE_FRAGMENT_BIT;
    }
}

std::string ShaderCompiler::get_file_extension(ShaderType type) {
    switch (type) {
        case ShaderType::VERTEX:              return ".vert";
        case ShaderType::FRAGMENT:            return ".frag";
        case ShaderType::GEOMETRY:            return ".geom";
        case ShaderType::TESSELLATION_CONTROL: return ".tesc";
        case ShaderType::TESSELLATION_EVALUATION: return ".tese";
        case ShaderType::COMPUTE:             return ".comp";
        default:                              return ".glsl";
    }
}

ShaderCompileResult ShaderCompiler::compile(const std::string& glsl_code,
                                           ShaderType shader_type,
                                           uint32_t optimize) {
    return compile_internal(glsl_code, shader_type, "<inline>", optimize);
}

ShaderCompileResult ShaderCompiler::compile_file(const std::filesystem::path& file_path,
                                               ShaderType shader_type,
                                               uint32_t optimize) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        GWS_LOG_ERROR("Failed to open shader file: {}", file_path.string());
        ShaderCompileResult result;
        result.success = false;
        result.errors.push_back({"Failed to open file", file_path.string(), 0, 0, false});
        return result;
    }

    std::string glsl_code((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    return compile_internal(glsl_code, shader_type, file_path.string(), optimize);
}

ShaderCompileResult ShaderCompiler::compile_internal(const std::string& glsl_code,
                                                    ShaderType shader_type,
                                                    const std::string& source_name,
                                                    uint32_t optimize) {
#ifdef GWS_HAS_GLSLANG
    ShaderCompileResult result;

    EShLanguage stage;
    switch (shader_type) {
        case ShaderType::VERTEX:                  stage = EShLangVertex; break;
        case ShaderType::FRAGMENT:                stage = EShLangFragment; break;
        case ShaderType::GEOMETRY:                stage = EShLangGeometry; break;
        case ShaderType::TESSELLATION_CONTROL:    stage = EShLangTessControl; break;
        case ShaderType::TESSELLATION_EVALUATION: stage = EShLangTessEvaluation; break;
        case ShaderType::COMPUTE:                 stage = EShLangCompute; break;
        default:
            result.errors.push_back({"Unknown shader type", source_name, 0, 0, false});
            return result;
    }

    glslang::TShader shader(stage);
    const char* shader_cstr = glsl_code.c_str();
    shader.setStrings(&shader_cstr, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_4);

    EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules |
                                        (validation_enabled ? EShMsgDefault : 0));

    if (!shader.parse(GetDefaultResources(), 100, false, messages)) {
        result.errors.push_back({shader.getInfoLog(), source_name, 0, 0, false});
        result.success = false;
        GWS_LOG_ERROR("Shader compilation failed: {}\n{}", source_name, shader.getInfoLog());
        return result;
    }

    glslang::TProgram program;
    program.addShader(&shader);

    if (!program.link(messages)) {
        result.errors.push_back({program.getInfoLog(), source_name, 0, 0, false});
        result.success = false;
        GWS_LOG_ERROR("Shader linking failed: {}\n{}", source_name, program.getInfoLog());
        return result;
    }

    std::vector<uint32_t> spirv;
    glslang::SpvOptions spv_options;
    spv_options.generateDebugInfo = validation_enabled;
    spv_options.disableOptimizer = (optimize == 0);
    spv_options.optimizeSize = (optimize >= 3);

    glslang::GlslangToSpv(*program.getIntermediate(stage), spirv, &spv_options);

    result.success = true;
    result.spirv_code = spirv;

    GWS_LOG_INFO("Shader compiled: {} ({} bytes SPIR-V)", source_name, spirv.size() * 4);
    return result;

#else
    // No glslang — cannot compile GLSL at runtime. Use pre-compiled SPIR-V files.
    (void)glsl_code; (void)shader_type; (void)optimize;
    ShaderCompileResult result;
    result.success = false;
    result.errors.push_back({
        "Runtime GLSL compilation not available (built with GCC). Load pre-compiled SPIR-V instead.",
        source_name, 0, 0, false
    });
    GWS_LOG_ERROR("Shader compilation unavailable: {}", source_name);
    return result;
#endif
}

std::unique_ptr<VulkanShader> ShaderCompiler::create_shader_module(
    VkDevice device,
    const ShaderCompileResult& result,
    ShaderType shader_type,
    const std::string& entry_point) {

    if (!result.success || result.spirv_code.empty()) {
        GWS_LOG_ERROR("Cannot create shader module from failed compilation");
        return nullptr;
    }

    VkShaderModuleCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = result.spirv_code.size() * sizeof(uint32_t);
    create_info.pCode = result.spirv_code.data();

    VkShaderModule vk_module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &create_info, nullptr, &vk_module) != VK_SUCCESS) {
        GWS_LOG_ERROR("Failed to create shader module");
        return nullptr;
    }

    return std::make_unique<VulkanShader>(device, vk_module, shader_type, entry_point);
}

}  // namespace gws::renderer::gpu
