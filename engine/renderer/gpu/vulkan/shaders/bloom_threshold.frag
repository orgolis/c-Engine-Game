#version 450

// Bloom Threshold Shader
// Extracts bright pixels for bloom effect

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inputTexture;

layout(push_constant) uniform BloomConstants {
    float threshold;
    float softKnee;
    float intensity;
    float padding;
} pc;

// Linear color space conversion
vec3 toLinear(vec3 color) {
    return color * color;
}

// Calculate luminance
float luminance(vec3 color) {
    return dot(color, vec3(0.299, 0.587, 0.114));
}

// Soft knee function for smooth transition
float softThreshold(float value, float threshold, float softKnee) {
    float halfKnee = softKnee * 0.5;
    float curve = clamp((value - threshold + halfKnee) / (softKnee + 0.0001), 0.0, 1.0);
    return mix(0.0, value, curve * curve);
}

void main() {
    vec3 color = texture(inputTexture, inTexCoord).rgb;
    
    // Convert to linear
    color = toLinear(color);
    
    // Calculate luminance
    float lum = luminance(color);
    
    // Apply soft threshold
    float bloom = softThreshold(lum, pc.threshold, pc.softKnee);
    
    // Scale by intensity
    color *= bloom * pc.intensity;
    
    outColor = vec4(color, 1.0);
}
