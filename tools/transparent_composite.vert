#version 450
// Fullscreen triangle covering the viewport, no vertex inputs.
layout(location = 0) out vec2 outUV;
void main() {
    outUV = vec2((gl_VertexIndex == 1) ? 2.0 : 0.0,
                 (gl_VertexIndex == 2) ? 2.0 : 0.0);
    gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
}
