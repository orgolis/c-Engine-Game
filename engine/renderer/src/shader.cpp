#include "shader.h"
#include "render_device.h"
#include <spdlog/spdlog.h>
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

namespace schizo::renderer {

ShaderProgram::ShaderProgram() = default;

ShaderProgram::~ShaderProgram() = default;

void ShaderProgram::Cleanup(RenderDevice* device) {
    if (device && program_) {
        if (vertex_shader_) device->DeleteShader(vertex_shader_);
        if (fragment_shader_) device->DeleteShader(fragment_shader_);
        device->DeleteShader(program_);  // DeleteShader handles both shaders and programs
    }
    program_ = vertex_shader_ = fragment_shader_ = 0;
}

bool ShaderProgram::Compile(RenderDevice* device, const std::string& vertex_source, const std::string& fragment_source) {
    if (!device) {
        last_error_ = "Invalid render device";
        return false;
    }

    // Compile vertex shader
    vertex_shader_ = device->CompileShader(ShaderStage::Vertex, vertex_source);
    if (!vertex_shader_) {
        last_error_ = device->GetLastError();
        spdlog::error("Vertex shader compilation failed: {}", last_error_);
        Cleanup(device);
        return false;
    }

    // Compile fragment shader
    fragment_shader_ = device->CompileShader(ShaderStage::Fragment, fragment_source);
    if (!fragment_shader_) {
        last_error_ = device->GetLastError();
        spdlog::error("Fragment shader compilation failed: {}", last_error_);
        Cleanup(device);
        return false;
    }

    // Link program
    uint32_t stages[] = { vertex_shader_, fragment_shader_ };
    program_ = device->LinkProgram(stages, 2);
    if (!program_) {
        last_error_ = device->GetLastError();
        spdlog::error("Program linking failed: {}", last_error_);
        Cleanup(device);
        return false;
    }

    spdlog::info("ShaderProgram compiled and linked successfully");
    return true;
}

void ShaderProgram::Use(RenderDevice* device) const {
    if (device && program_) {
        device->UseProgram(program_);
    }
}

void ShaderProgram::SetUniform1f(RenderDevice* device, const std::string& name, float value) const {
    if (!device || !program_) return;
    // TODO: Implement uniform setting via RenderDevice
    (void)name;
    (void)value;
}

void ShaderProgram::SetUniform2f(RenderDevice* device, const std::string& name, float x, float y) const {
    if (!device || !program_) return;
    // TODO: Implement uniform setting via RenderDevice
    (void)name;
    (void)x;
    (void)y;
}

void ShaderProgram::SetUniform3f(RenderDevice* device, const std::string& name, float x, float y, float z) const {
    if (!device || !program_) return;
    // TODO: Implement uniform setting via RenderDevice
    (void)name;
    (void)x;
    (void)y;
    (void)z;
}

void ShaderProgram::SetUniform4f(RenderDevice* device, const std::string& name, float x, float y, float z, float w) const {
    if (!device || !program_) return;
    // TODO: Implement uniform setting via RenderDevice
    (void)name;
    (void)x;
    (void)y;
    (void)z;
    (void)w;
}

void ShaderProgram::SetUniform1i(RenderDevice* device, const std::string& name, int value) const {
    if (!device || !program_) return;
    // TODO: Implement uniform setting via RenderDevice
    (void)name;
    (void)value;
}

void ShaderProgram::SetUniformMat4(RenderDevice* device, const std::string& name, const glm::mat4& mat) const {
    if (!device || !program_) return;
    
    // Get uniform location and set value
    // We need to use OpenGL directly for this
    GLint loc = glGetUniformLocation(program_, name.c_str());
    if (loc != -1) {
        glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mat));
    }
}

std::unique_ptr<ShaderProgram> ShaderProgram::CreateBasicShader(RenderDevice* device) {
    // DEBUG: Ultra-simple shader - solid white triangle in clip space
    const std::string vertex_src = R"(
#version 450 core

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;

out vec4 vertex_color;

void main() {
    // Direct clip space positioning  
    gl_Position = vec4(in_position.xy * 0.5, 0.0, 1.0);
    vertex_color = vec4(1.0, 1.0, 1.0, 1.0);  // White color
}
)";

    const std::string fragment_src = R"(
#version 450 core

in vec4 vertex_color;
out vec4 out_color;

void main() {
    out_color = vec4(1.0, 0.0, 0.0, 1.0);  // Red output
}
)";

    auto shader = std::make_unique<ShaderProgram>();
    if (!shader->Compile(device, vertex_src, fragment_src)) {
        spdlog::error("Failed to create basic shader: {}", shader->GetLastError());
        return nullptr;
    }
    
    spdlog::info("Created basic shader program");
    return shader;
}

std::unique_ptr<ShaderProgram> ShaderProgram::CreateSolidShader(RenderDevice* device) {
    const std::string vertex_src = R"(
#version 450 core

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;

void main() {
    gl_Position = vec4(in_position, 1.0);
}
)";

    const std::string fragment_src = R"(
#version 450 core

uniform vec4 color;
out vec4 out_color;

void main() {
    out_color = vec4(1.0, 0.0, 0.0, 1.0);  // Red
}
)";

    auto shader = std::make_unique<ShaderProgram>();
    if (!shader->Compile(device, vertex_src, fragment_src)) {
        spdlog::error("Failed to create solid shader: {}", shader->GetLastError());
        return nullptr;
    }
    
    spdlog::info("Created solid shader program");
    return shader;
}

} // namespace schizo::renderer
