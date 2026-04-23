#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include <filesystem>

namespace gws::renderer::gpu {

/**
 * @brief Shader compilation error information
 */
struct ShaderCompileError {
    std::string message;
    std::string file;
    uint32_t line = 0;
    uint32_t column = 0;
    bool is_warning = false;
};

/**
 * @brief Result of shader compilation
 */
struct ShaderCompileResult {
    bool success = false;
    std::vector<uint32_t> spirv_code;
    std::vector<ShaderCompileError> errors;
    std::string debug_info;
};

/**
 * @brief Shader type enumeration
 */
enum class ShaderType {
    VERTEX,
    FRAGMENT,
    GEOMETRY,
    TESSELLATION_CONTROL,
    TESSELLATION_EVALUATION,
    COMPUTE,
};

/**
 * @brief Vulkan Shader Module wrapper
 */
class VulkanShader {
public:
    VulkanShader(VkDevice device, VkShaderModule module, ShaderType type,
                 const std::string& entry_point = "main");
    ~VulkanShader();
    
    VkShaderModule get_module() const { return module; }
    ShaderType get_type() const { return type; }
    const std::string& get_entry_point() const { return entry_point; }
    VkShaderStageFlagBits get_stage_flags() const;

private:
    VkDevice device;
    VkShaderModule module;
    ShaderType type;
    std::string entry_point;
};

/**
 * @brief GLSL to SPIR-V Shader Compiler
 * 
 * Compiles GLSL shader code to SPIR-V format for Vulkan.
 * Features:
 * - Automatic shader type detection
 * - Error reporting with source locations
 * - Shader caching
 * - Optimization levels
 */
class ShaderCompiler {
public:
    ShaderCompiler();
    ~ShaderCompiler();
    
    /**
     * @brief Compile GLSL code to SPIR-V
     * @param glsl_code GLSL source code
     * @param shader_type Type of shader (vertex, fragment, etc.)
     * @param optimize Optimization level (0-3, default 2)
     * @return Compilation result with SPIR-V or errors
     */
    ShaderCompileResult compile(const std::string& glsl_code,
                               ShaderType shader_type,
                               uint32_t optimize = 2);
    
    /**
     * @brief Compile GLSL file to SPIR-V
     * @param file_path Path to GLSL source file
     * @param shader_type Type of shader
     * @param optimize Optimization level
     * @return Compilation result
     */
    ShaderCompileResult compile_file(const std::filesystem::path& file_path,
                                    ShaderType shader_type,
                                    uint32_t optimize = 2);
    
    /**
     * @brief Create a Vulkan shader module from compiled SPIR-V
     * @param device Vulkan device
     * @param result Compilation result
     * @param entry_point Shader entry point name
     * @return Pointer to shader module, nullptr on failure
     */
    std::unique_ptr<VulkanShader> create_shader_module(
        VkDevice device,
        const ShaderCompileResult& result,
        ShaderType shader_type,
        const std::string& entry_point = "main");
    
    /**
     * @brief Get the VkShaderStageFlagBits for a shader type
     */
    static VkShaderStageFlagBits get_vk_stage(ShaderType type);
    
    /**
     * @brief Get GLSL file extension for shader type
     */
    static std::string get_file_extension(ShaderType type);
    
    /**
     * @brief Enable/disable validation
     */
    void set_validation_enabled(bool enabled) { validation_enabled = enabled; }

private:
    bool validation_enabled = true;
    
    /**
     * @brief Internal compilation using glslang
     */
    ShaderCompileResult compile_internal(const std::string& glsl_code,
                                        ShaderType shader_type,
                                        const std::string& source_name,
                                        uint32_t optimize);
};

}  // namespace gws::renderer::gpu
