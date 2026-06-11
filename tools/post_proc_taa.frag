#version 450
layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D currentTex;
layout(set = 0, binding = 1) uniform sampler2D historyTex;
layout(push_constant) uniform TaaConstants {
    float blendFactor;
    float _pad[3];
} pc;
void main() {
    vec3 current = texture(currentTex, inTexCoord).rgb;
    vec3 history = texture(historyTex, inTexCoord).rgb;
    outColor = vec4(mix(history, current, clamp(pc.blendFactor, 0.0, 1.0)), 1.0);
}
