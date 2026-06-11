#version 450
// Combined final colour effects, applied after FXAA on the LDR image.
// Order of operations:
//   1. Lens distortion  — warp the sampling UV (barrel / pincushion)
//   2. Chromatic aberr.  — per-channel radial UV offset
//   3. Sharpen           — unsharp mask from 4-neighbour blur
//   4. Color grade       — temperature/tint (white balance), contrast,
//                          brightness, saturation
//   5. Vignette          — corner darkening
//   6. Film grain        — animated noise
// Each stage is gated by its push-constant value (0 / identity = off), so
// the single pass covers everything; the C++ side skips the pass entirely
// when no effect is active.

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inputTexture;

layout(push_constant) uniform PC {
    vec2  texelSize;        // 1 / resolution
    float chromatic;        // aberration strength
    float vignette;         // vignette strength 0..1
    float vignetteRadius;   // distance where darkening starts
    float grain;            // grain strength
    float time;             // seconds (animates grain)
    float sharpen;          // unsharp amount (0 = off)
    float lensDistort;      // barrel(+)/pincushion(-) amount
    float temperature;      // white balance warm(+)/cool(-)
    float tint;             // green(-)/magenta(+)
    float saturation;       // 1 = unchanged
    float contrast;         // 1 = unchanged
    float brightness;       // 1 = unchanged
    float posterize;        // color levels (0 = off, e.g. 8)
    float pixelate;         // block size in px (0 = off)
    float scanline;         // CRT scanline strength (0 = off)
} pc;

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main() {
    vec2 uv = inUV;
    vec2 res = 1.0 / pc.texelSize;

    // 0. Pixelate — snap the sampling UV to a coarse block grid.
    if (pc.pixelate > 1.0) {
        vec2 grid = res / pc.pixelate;
        uv = (floor(uv * grid) + 0.5) / grid;
    }

    // 1. Lens distortion — displace UV radially by r^2.
    if (pc.lensDistort != 0.0) {
        vec2 cc = uv - 0.5;
        float r2 = dot(cc, cc);
        uv = 0.5 + cc * (1.0 + pc.lensDistort * r2);
    }

    vec2 toCenter = uv - vec2(0.5);
    float dist = length(toCenter);

    // 2. Chromatic aberration.
    vec3 color;
    if (pc.chromatic > 0.0) {
        vec2 off = toCenter * pc.chromatic * dist * 4.0;
        color.r = texture(inputTexture, uv - off).r;
        color.g = texture(inputTexture, uv).g;
        color.b = texture(inputTexture, uv + off).b;
    } else {
        color = texture(inputTexture, uv).rgb;
    }

    // 3. Sharpen — unsharp mask against a 4-tap cross blur.
    if (pc.sharpen > 0.0) {
        vec3 n = texture(inputTexture, uv + vec2(0.0,  pc.texelSize.y)).rgb;
        vec3 s = texture(inputTexture, uv + vec2(0.0, -pc.texelSize.y)).rgb;
        vec3 e = texture(inputTexture, uv + vec2( pc.texelSize.x, 0.0)).rgb;
        vec3 w = texture(inputTexture, uv + vec2(-pc.texelSize.x, 0.0)).rgb;
        vec3 blur = (n + s + e + w) * 0.25;
        color += (color - blur) * pc.sharpen;
    }

    // 4. Color grade.
    //    a) white balance: warm pushes R up / B down; tint shifts green<->magenta.
    color.r *= 1.0 + pc.temperature * 0.15;
    color.b *= 1.0 - pc.temperature * 0.15;
    color.g *= 1.0 - pc.tint * 0.15;
    //    b) contrast around mid-grey, then brightness scale.
    color = (color - 0.5) * pc.contrast + 0.5;
    color *= pc.brightness;
    //    c) saturation.
    color = mix(vec3(luma(color)), color, pc.saturation);
    color = max(color, vec3(0.0));

    // 4d. Posterize — quantise colour into N bands (cel / retro look).
    if (pc.posterize >= 2.0) {
        color = floor(color * pc.posterize + 0.5) / pc.posterize;
    }

    // 5. Vignette.
    if (pc.vignette > 0.0) {
        float v = smoothstep(pc.vignetteRadius, pc.vignetteRadius * 0.5, dist);
        color *= mix(1.0, v, pc.vignette);
    }

    // 5b. Scanlines — CRT-style alternating row darkening.
    if (pc.scanline > 0.0) {
        float s = 0.5 + 0.5 * sin(inUV.y * res.y * 3.14159265);
        color *= mix(1.0, s, pc.scanline);
    }

    // 6. Film grain.
    if (pc.grain > 0.0) {
        float gn = hash(inUV * 1024.0 + fract(pc.time) * 100.0) - 0.5;
        color += gn * pc.grain;
    }

    outColor = vec4(color, 1.0);
}
