// Extracted from vulkan_g_buffer.cpp (inline R"GLSL" literal).
// Verified: this compiles to SPIR-V semantically identical to the kGbufferDemoFragSpv
// array that ships in the generated header, so moving it here changed
// nothing. Edit HERE; `gws shaders` compiles it for immediate iteration,
// and tools/spv_to_header.py regenerates the baked header for shipping.
#version 450
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inAlbedo;
layout(location = 3) in float inMetallic;
layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outAlbedo;
layout(location = 3) out vec4 outMaterial;
void main() {
    outPosition = vec4(inWorldPos, 1.0);
    outNormal   = vec4(normalize(inNormal), 0.5); // .a = roughness 0.5
    outAlbedo   = vec4(inAlbedo, inMetallic);
    outMaterial = vec4(0.0, 1.0, 0.0, 0.0);        // ID, AO, emission, reserved
}
