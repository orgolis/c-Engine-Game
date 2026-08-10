// Extracted from vulkan_shadow_map.cpp (inline R"GLSL" literal).
// Verified: this compiles to SPIR-V semantically identical to the kShadowCasterVertSpv
// array that ships in the generated header, so moving it here changed
// nothing. Edit HERE; `gws shaders` compiles it for immediate iteration,
// and tools/spv_to_header.py regenerates the baked header for shipping.
#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;
layout(push_constant) uniform PC { mat4 mvp; } pc;
layout(location = 0) out vec2 outUV;
void main() {
    outUV = inUV;
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
}
