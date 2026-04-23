#version 450

// Shadow Depth Fragment Shader
// Simple fragment shader that just outputs depth
// Actual depth is written to depth attachment

layout(location = 0) in VertexOutput {
    vec4 position;
    vec2 texCoord;
} vin;

void main() {
    // Depth is automatically written to depth attachment
    // This shader just ensures the fragment passes
    gl_FragDepth = gl_FragCoord.z;
}
