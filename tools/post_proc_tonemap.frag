#version 450
// Post-process tonemap shader. Applies bloom add, ACES filmic tonemap,
// contrast / saturation / gamma. Exposure now comes from a persistent
// storage buffer written by the auto-exposure compute pass; if the
// buffer's value is 0 (uninitialized) the push-constant fallback wins.

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inputTexture;
layout(set = 0, binding = 1) uniform sampler2D bloomTexture;
layout(set = 0, binding = 2) readonly buffer ExposureBuffer {
    float exposure;
    float _pad0;
    float _pad1;
    float _pad2;
} autoExposure;

layout(push_constant) uniform TonemapConstants {
    float exposure;          // fallback, used when autoExposure.exposure <= 0
    float gamma;
    float contrast;
    float saturation;
    float bloomIntensity;
    float _pad0;
    float _pad1;
    float _pad2;
} pc;

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
    vec3 bloom = texture(bloomTexture, inTexCoord).rgb;
    color += bloom * pc.bloomIntensity;

    float live_exp = autoExposure.exposure;
    float exp_used = live_exp > 0.0 ? live_exp : pc.exposure;
    color *= exp_used;

    color = ACESFilm(color);
    color = mix(vec3(0.5), color, pc.contrast);
    float lum = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(lum), color, pc.saturation);
    color = pow(color, vec3(1.0 / pc.gamma));
    outColor = vec4(color, 1.0);
}
