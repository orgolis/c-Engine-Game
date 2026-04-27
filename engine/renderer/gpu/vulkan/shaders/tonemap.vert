#version 450

// Fullscreen-triangle vertex shader for post-processing passes.
// Emits a single oversized triangle that covers the viewport; the
// rasterizer clips it to screen. UVs are derived from gl_VertexIndex
// so no vertex buffer is required — draw with vkCmdDraw(cmd, 3, 1, 0, 0).
//
// Vertex layout (clip space, with Vulkan's flipped Y handled below):
//   0: ( 3, -1)   uv = (2, 0)
//   1: (-1, -1)   uv = (0, 0)
//   2: (-1,  3)   uv = (0, 2)

layout(location = 0) out vec2 outTexCoord;

void main() {
    outTexCoord = vec2((gl_VertexIndex == 0) ? 2.0 : 0.0,
                       (gl_VertexIndex == 2) ? 2.0 : 0.0);
    gl_Position = vec4(outTexCoord * 2.0 - 1.0, 0.0, 1.0);
}
