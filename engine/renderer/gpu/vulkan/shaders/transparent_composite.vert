// Extracted from vulkan_transparent_pass.cpp (inline R"GLSL" literal).
// Verified: this compiles to SPIR-V semantically identical to the kTransparentCompositeVertSpv
// array that ships in the generated header, so moving it here changed
// nothing. Edit HERE; `gws shaders` compiles it for immediate iteration,
// and tools/spv_to_header.py regenerates the baked header for shipping.
#version 450
layout(location = 0) out vec2 outUV;
void main() {
    outUV = vec2((gl_VertexIndex == 1) ? 2.0 : 0.0,
                 (gl_VertexIndex == 2) ? 2.0 : 0.0);
    gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
}
