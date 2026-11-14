#version 450
// Bloom chain, down: one mip from the one above (or, first, from the lit HDR
// image with the bright-pass). A 13-tap filter (the "Call of Duty: Advanced
// Warfare" downsample) keeps it stable as the camera moves -- no shimmering
// specks. The bright-pass is measured on the EXPOSED image (the exposure the
// tone map will use), so only what will look brighter than white glows,
// whatever the scene's brightness; the colour handed on stays unexposed.
layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D src;
layout(set = 0, binding = 1) readonly buffer ExposureBuffer { float exposure; float p0; float p1; float p2; } autoExposure;
layout(push_constant) uniform P {
    vec2  srcTexel;        // 1 / the source's size
    float threshold;       // exposed brightness where it starts to glow
    float knee;            // soft knee width
    float prefilter;       // 1: the first pass (bright-pass), 0: plain downsample
    float staticExposure;  // when the auto-exposure has no value
    float clampMax;        // fireflies: no single pixel brighter than this (exposed)
    float pad;
} pc;

vec3 bright(vec3 c, float e) {
    vec3 x = min(c * e, vec3(pc.clampMax));
    float br = max(x.r, max(x.g, x.b));
    float rq = clamp(br - pc.threshold + pc.knee, 0.0, 2.0 * pc.knee);
    rq = (rq * rq) / (4.0 * pc.knee + 1e-5);
    float w = max(rq, br - pc.threshold) / max(br, 1e-5);
    return x * w / max(e, 1e-5);
}

void main() {
    vec2 t = pc.srcTexel;
    vec2 uv = inTexCoord;
    vec3 a = texture(src, uv + t * vec2(-2.0, -2.0)).rgb;
    vec3 b = texture(src, uv + t * vec2( 0.0, -2.0)).rgb;
    vec3 c = texture(src, uv + t * vec2( 2.0, -2.0)).rgb;
    vec3 d = texture(src, uv + t * vec2(-1.0, -1.0)).rgb;
    vec3 e = texture(src, uv + t * vec2( 1.0, -1.0)).rgb;
    vec3 f = texture(src, uv + t * vec2(-2.0,  0.0)).rgb;
    vec3 g = texture(src, uv).rgb;
    vec3 h = texture(src, uv + t * vec2( 2.0,  0.0)).rgb;
    vec3 i = texture(src, uv + t * vec2(-1.0,  1.0)).rgb;
    vec3 j = texture(src, uv + t * vec2( 1.0,  1.0)).rgb;
    vec3 k = texture(src, uv + t * vec2(-2.0,  2.0)).rgb;
    vec3 l = texture(src, uv + t * vec2( 0.0,  2.0)).rgb;
    vec3 m = texture(src, uv + t * vec2( 2.0,  2.0)).rgb;
    if (pc.prefilter > 0.5) {
        float ex = autoExposure.exposure > 0.0 ? autoExposure.exposure : pc.staticExposure;
        a = bright(a, ex); b = bright(b, ex); c = bright(c, ex); d = bright(d, ex); e = bright(e, ex);
        f = bright(f, ex); g = bright(g, ex); h = bright(h, ex); i = bright(i, ex); j = bright(j, ex);
        k = bright(k, ex); l = bright(l, ex); m = bright(m, ex);
    }
    vec3 o = (d + e + i + j) * 0.125
           + (a + b + g + f) * 0.03125 + (b + c + h + g) * 0.03125
           + (f + g + l + k) * 0.03125 + (g + h + m + l) * 0.03125;
    outColor = vec4(o, 1.0);
}
