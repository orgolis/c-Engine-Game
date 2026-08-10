// Extracted from vulkan_transparent_pass.cpp (inline R"GLSL" literal).
// Verified: this compiles to SPIR-V semantically identical to the kTransparentCompositeFragSpv
// array that ships in the generated header, so moving it here changed
// nothing. Edit HERE; `gws shaders` compiles it for immediate iteration,
// and tools/spv_to_header.py regenerates the baked header for shipping.
#version 450
layout(location = 0) in vec2 inUV;
layout(set = 0, binding = 0) uniform sampler2D accumTex;
layout(set = 0, binding = 1) uniform sampler2D revealTex;
layout(location = 0) out vec4 outColor;
void main() {
    vec4  accum     = texture(accumTex,  inUV);
    float revealage = texture(revealTex, inUV).r;
    if (revealage >= 1.0 - 1e-5) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec3 average = accum.rgb / max(accum.a, 1e-4);
    outColor = vec4(average, revealage);
}
