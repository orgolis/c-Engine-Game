#version 450

// Lighting Pass Vertex Shader
// Simple full-screen quad rendering

layout(location = 0) out vec2 outTexCoord;

void main() {
    // Generate full-screen quad vertices
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    outTexCoord = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
