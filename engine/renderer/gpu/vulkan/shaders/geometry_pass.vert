#version 450

// Geometry Pass Vertex Shader
// Outputs vertex attributes to G-Buffer

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec4 inColor;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 projection;
} pc;

layout(location = 0) out VertexOutput {
    vec3 position;
    vec3 normal;
    vec4 tangent;
    vec2 texCoord;
    vec4 color;
} vout;

void main() {
    // Transform position to world space
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    vout.position = worldPos.xyz;
    
    // Transform normal to world space
    mat3 normalMatrix = transpose(inverse(mat3(pc.model)));
    vout.normal = normalize(normalMatrix * inNormal);
    
    // Transform tangent to world space
    vout.tangent = vec4(normalize(normalMatrix * inTangent.xyz), inTangent.w);
    
    // Pass through texture coordinates and color
    vout.texCoord = inTexCoord;
    vout.color = inColor;
    
    // Project to screen space
    gl_Position = pc.projection * pc.view * worldPos;
}
