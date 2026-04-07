#pragma once

#include <string>
#include <memory>
#include <cstdint>
#include <glm/glm.hpp>

namespace schizo::renderer {

// Forward declaration
class RenderDevice;

/**
 * @class ShaderProgram
 * @brief Manages shader compilation and linking
 */
class ShaderProgram {
public:
    ShaderProgram();
    ~ShaderProgram();
    
    // Deleted copy operations
    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;
    
    // Allow move operations
    ShaderProgram(ShaderProgram&&) = default;
    ShaderProgram& operator=(ShaderProgram&&) = default;
    
    /**
     * Compile and link vertex and fragment shaders
     * @param device Graphics device for compilation
     * @param vertex_source Vertex shader GLSL source code
     * @param fragment_source Fragment shader GLSL source code
     * @return true if successful, false if compilation/linking failed
     */
    bool Compile(RenderDevice* device, const std::string& vertex_source, const std::string& fragment_source);
    
    /**
     * Bind this shader program for rendering
     */
    void Use(RenderDevice* device) const;
    
    /**
     * Get the program handle
     */
    uint32_t GetHandle() const { return program_; }
    
    /**
     * Set uniform variables
     */
    void SetUniform1f(RenderDevice* device, const std::string& name, float value) const;
    void SetUniform2f(RenderDevice* device, const std::string& name, float x, float y) const;
    void SetUniform3f(RenderDevice* device, const std::string& name, float x, float y, float z) const;
    void SetUniform4f(RenderDevice* device, const std::string& name, float x, float y, float z, float w) const;
    void SetUniform1i(RenderDevice* device, const std::string& name, int value) const;
    void SetUniformMat4(RenderDevice* device, const std::string& name, const glm::mat4& mat) const;
    
    /**
     * Check if compilation succeeded
     */
    bool IsValid() const { return program_ != 0; }
    
    /**
     * Get last compilation/linking error
     */
    std::string GetLastError() const { return last_error_; }
    
    // Factory methods for default shaders
    /**
     * Create a basic colored mesh shader
     */
    static std::unique_ptr<ShaderProgram> CreateBasicShader(RenderDevice* device);
    
    /**
     * Create a simple single-color shader
     */
    static std::unique_ptr<ShaderProgram> CreateSolidShader(RenderDevice* device);

private:
    uint32_t program_ = 0;
    uint32_t vertex_shader_ = 0;
    uint32_t fragment_shader_ = 0;
    mutable std::string last_error_;
    
    void Cleanup(RenderDevice* device);
};

} // namespace schizo::renderer
