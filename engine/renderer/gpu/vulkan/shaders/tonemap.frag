#version 450

// Post-Processing Tone Mapping Shader
// Applies ACES tone mapping and gamma correction

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inputTexture;

layout(push_constant) uniform TonemapConstants {
    float exposure;
    float gamma;
    float contrast;
    float saturation;
} pc;

// ACES tone mapping operator
vec3 ACESFilm(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 color = texture(inputTexture, inTexCoord).rgb;
    
    // Apply exposure
    color *= pc.exposure;
    
    // Apply ACES tone mapping
    color = ACESFilm(color);
    
    // Apply contrast
    color = mix(vec3(0.5), color, pc.contrast);
    
    // Apply saturation
    float lum = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(lum), color, pc.saturation);
    
    // Gamma correction
    color = pow(color, vec3(1.0 / pc.gamma));
    
    outColor = vec4(color, 1.0);
}
