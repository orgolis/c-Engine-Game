// Extracted from vulkan_g_buffer.cpp (inline R"GLSL" literal).
// Verified: this compiles to SPIR-V semantically identical to the kGbufferDemoVertSpv
// array that ships in the generated header, so moving it here changed
// nothing. Edit HERE; `gws shaders` compiles it for immediate iteration,
// and tools/spv_to_header.py regenerates the baked header for shipping.
#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inAlbedo;
layout(push_constant) uniform Constants {
    mat4 mvp;
    vec4 albedoMetallic; // overrides per-vertex albedo with this if alpha > 0; metallic = .a
} pc;
layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outAlbedo;
layout(location = 3) out float outMetallic;
void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    outWorldPos = inPosition;
    outNormal   = inNormal;
    outAlbedo   = (pc.albedoMetallic.r + pc.albedoMetallic.g + pc.albedoMetallic.b > 0.0)
                     ? pc.albedoMetallic.rgb : inAlbedo;
    outMetallic = pc.albedoMetallic.a;
}
