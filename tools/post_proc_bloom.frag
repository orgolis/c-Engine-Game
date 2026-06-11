#version 450
layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D inputTexture;
layout(push_constant) uniform BloomConstants {
    vec2  texelSize;
    float threshold;
    float intensity;
} pc;

vec3 brightPass(vec3 c) {
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float over = max(0.0, lum - pc.threshold);
    float k = over / max(lum, 1e-4);
    return c * k;
}

void main() {
    const float w[5] = float[5](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 sum = brightPass(texture(inputTexture, inTexCoord).rgb) * w[0];
    for (int i = 1; i < 5; ++i) {
        vec2 off = vec2(float(i)) * pc.texelSize;
        sum += brightPass(texture(inputTexture, inTexCoord + vec2(off.x, 0.0)).rgb) * w[i] * 0.5;
        sum += brightPass(texture(inputTexture, inTexCoord - vec2(off.x, 0.0)).rgb) * w[i] * 0.5;
        sum += brightPass(texture(inputTexture, inTexCoord + vec2(0.0, off.y)).rgb) * w[i] * 0.5;
        sum += brightPass(texture(inputTexture, inTexCoord - vec2(0.0, off.y)).rgb) * w[i] * 0.5;
    }
    outColor = vec4(sum * pc.intensity, 1.0);
}
