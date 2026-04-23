#version 450

// Shadow Depth Vertex Shader
// Renders scene depth from light's perspective

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec4 inColor;

layout(push_constant) uniform PushConstants {
    mat4 lightViewProj;
    mat4 model;
    float depthBias;
    uint cascadeIndex;
} pc;

layout(location = 0) out VertexOutput {
    vec4 position;
    vec2 texCoord;
} vout;

void main() {
    // Transform to light space
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    vec4 lightSpacePos = pc.lightViewProj * worldPos;
    
    gl_Position = lightSpacePos;
    vout.position = lightSpacePos;
    vout.texCoord = inTexCoord;
}
