#version 450
layout(location = 0) out vec2 outTexCoord;
void main() {
    outTexCoord = vec2((gl_VertexIndex == 0) ? 2.0 : 0.0,
                       (gl_VertexIndex == 2) ? 2.0 : 0.0);
    gl_Position = vec4(outTexCoord * 2.0 - 1.0, 0.0, 1.0);
}
