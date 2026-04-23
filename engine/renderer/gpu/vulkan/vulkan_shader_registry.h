/**
 * @file vulkan_shader_registry.h
 * @brief Shader registry and caching system for deferred rendering
 */

#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

namespace gws::renderer::gpu {

class VulkanDevice;

/**
 * @enum ShaderStage
 * @brief Shader stage types
 */
enum class ShaderStage {
    Vertex,
    Fragment,
    Geometry,
    Compute,
    TessControl,
    TessEval,
};

/**
 * @struct ShaderModule
 * @brief Compiled shader module
 */
struct ShaderModule {
    VkShaderModule handle = VK_NULL_HANDLE;
    std::string name;
    ShaderStage stage;
    std::vector<uint32_t> spirv_code;
};

/**
 * @struct PipelineShaders
 * @brief Collection of shaders for a graphics pipeline
 */
struct PipelineShaders {
    std::shared_ptr<ShaderModule> vertex;
    std::shared_ptr<ShaderModule> fragment;
    std::shared_ptr<ShaderModule> geometry;
};

/**
 * @class VulkanShaderRegistry
 * @brief Manages shader compilation, caching, and pipeline creation
 */
class VulkanShaderRegistry {
public:
    VulkanShaderRegistry() = default;
    ~VulkanShaderRegistry();
    
    // Deleted copy operations
    VulkanShaderRegistry(const VulkanShaderRegistry&) = delete;
    VulkanShaderRegistry& operator=(const VulkanShaderRegistry&) = delete;
    
    // Allow move operations
    VulkanShaderRegistry(VulkanShaderRegistry&&) = default;
    VulkanShaderRegistry& operator=(VulkanShaderRegistry&&) = default;
    
    /**
     * @brief Initialize shader registry
     * @param device Vulkan device
     * @return True if successful
     */
    bool initialize(VulkanDevice* device);
    
    /**
     * @brief Compile shader from GLSL source
     * @param source GLSL source code
     * @param stage Shader stage
     * @param name Shader name for caching
     * @return Compiled shader module
     */
    std::shared_ptr<ShaderModule> compile_glsl(const std::string& source,
                                              ShaderStage stage,
                                              const std::string& name);
    
    /**
     * @brief Load shader from file
     * @param path File path to GLSL shader
     * @param stage Shader stage
     * @return Compiled shader module
     */
    std::shared_ptr<ShaderModule> load_shader(const std::string& path, ShaderStage stage);
    
    /**
     * @brief Get cached shader
     * @param name Shader name
     * @return Cached shader module or nullptr
     */
    std::shared_ptr<ShaderModule> get_shader(const std::string& name);
    
    /**
     * @brief Create graphics pipeline
     * @param shaders Pipeline shaders
     * @param layout Pipeline layout
     * @param render_pass Render pass
     * @return Created graphics pipeline
     */
    VkPipeline create_graphics_pipeline(const PipelineShaders& shaders,
                                       VkPipelineLayout layout,
                                       VkRenderPass render_pass);
    
    /**
     * @brief Get built-in shader source
     * @param name Shader name (e.g., "geometry_pass.vert")
     * @return GLSL source code
     */
    std::string get_builtin_shader(const std::string& name);
    
    /**
     * @brief Clear shader cache
     */
    void clear_cache();

private:
    VulkanDevice* device_ = nullptr;
    std::unordered_map<std::string, std::shared_ptr<ShaderModule>> shader_cache_;
    
    // Helper functions
    std::vector<uint32_t> compile_glsl_to_spirv(const std::string& source, 
                                               ShaderStage stage,
                                               const std::string& name);
    
    VkShaderStageFlagBits stage_to_vk(ShaderStage stage);
    
    void cleanup_cache();
};

} // namespace gws::renderer::gpu
