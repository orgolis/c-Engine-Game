#version 450
// FXAA-Lite (Lottes, NVIDIA whitepaper). Single-pass edge-detect AA on the
// already-tonemapped LDR target. Reads a private intermediate image (set
// up by the caller as a copy of the tonemap output), writes the smoothed
// result back to the main output target.
//
// Two main steps:
//   1. Sample the centre pixel and its 4 diagonal neighbours, compute
//      luminance, derive a gradient direction across the edge.
//   2. Sample along that direction with a small kernel; pick between two
//      candidate blends based on whether the heavier blend's luminance
//      stays inside the local min/max range (anti-overshoot).
//
// FXAA is cheap and runs on the colour buffer alone — no normal/depth
// inputs — so it slots in after tonemap without touching G-Buffer state.

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inputTexture;

layout(push_constant) uniform PC {
    vec2 rcpFrame;  // 1.0 / (width, height) — texel offset
    float _pad0;
    float _pad1;
} pc;

float luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec2 uv = inUV;
    vec2 rcp = pc.rcpFrame;

    vec3 rgbNW = textureLod(inputTexture, uv + vec2(-1.0, -1.0) * rcp, 0.0).rgb;
    vec3 rgbNE = textureLod(inputTexture, uv + vec2( 1.0, -1.0) * rcp, 0.0).rgb;
    vec3 rgbSW = textureLod(inputTexture, uv + vec2(-1.0,  1.0) * rcp, 0.0).rgb;
    vec3 rgbSE = textureLod(inputTexture, uv + vec2( 1.0,  1.0) * rcp, 0.0).rgb;
    vec3 rgbM  = textureLod(inputTexture, uv, 0.0).rgb;

    float lumaNW = luma(rgbNW);
    float lumaNE = luma(rgbNE);
    float lumaSW = luma(rgbSW);
    float lumaSE = luma(rgbSE);
    float lumaM  = luma(rgbM);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    // Bail out early on regions with no contrast — saves bandwidth and
    // avoids creating artefacts in flat areas.
    const float EDGE_THRESHOLD     = 0.0312; // ~1/32
    const float EDGE_THRESHOLD_MIN = 0.0833; // ~1/12
    float range = lumaMax - lumaMin;
    if (range < max(EDGE_THRESHOLD, lumaMax * EDGE_THRESHOLD_MIN)) {
        outColor = vec4(rgbM, 1.0);
        return;
    }

    // Gradient direction across the edge (perpendicular to the edge).
    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    const float DIR_REDUCE_MIN = 1.0 / 128.0;
    const float DIR_REDUCE_MUL = 1.0 / 8.0;
    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 * DIR_REDUCE_MUL,
                          DIR_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);

    const float SPAN_MAX = 8.0;
    dir = clamp(dir * rcpDirMin, vec2(-SPAN_MAX), vec2(SPAN_MAX)) * rcp;

    // Two candidate blends — A is a 2-tap, B adds two outer taps for a
    // smoother result. B is only used if its luminance stays inside the
    // local min/max range (i.e., it doesn't overshoot the edge).
    vec3 rgbA = 0.5 * (
        textureLod(inputTexture, uv + dir * (1.0 / 3.0 - 0.5), 0.0).rgb +
        textureLod(inputTexture, uv + dir * (2.0 / 3.0 - 0.5), 0.0).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        textureLod(inputTexture, uv + dir * -0.5, 0.0).rgb +
        textureLod(inputTexture, uv + dir *  0.5, 0.0).rgb);

    float lumaB = luma(rgbB);
    if (lumaB < lumaMin || lumaB > lumaMax) {
        outColor = vec4(rgbA, 1.0);
    } else {
        outColor = vec4(rgbB, 1.0);
    }
}
