// Extracted from vulkan_transparent_pass.cpp (inline R"GLSL" literal).
// Verified: this compiles to SPIR-V semantically identical to the kTransparentVertSpv
// array that ships in the generated header, so moving it here changed
// nothing. Edit HERE; `gws shaders` compiles it for immediate iteration,
// and tools/spv_to_header.py regenerates the baked header for shipping.
#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;
layout(push_constant) uniform PC { mat4 mvp; mat4 model; } pc;
layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec3 outTangent;
layout(location = 4) out vec3 outBitangent;
void main() {
    vec4 wp = pc.model * vec4(inPosition, 1.0);
    outWorldPos = wp.xyz;
    mat3 nrm = mat3(pc.model);
    outNormal    = normalize(nrm * inNormal);
    outTangent   = normalize(nrm * inTangent.xyz);
    outBitangent = normalize(cross(outNormal, outTangent) * inTangent.w);
    outUV = inUV;
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
}
