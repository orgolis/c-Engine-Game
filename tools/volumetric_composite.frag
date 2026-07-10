#version 450
// Joint bilateral upsample of the half-res in-scatter into the HDR target.
// A 3x3 neighbourhood of half-res taps is weighted by spatial falloff AND
// world-position similarity (the G-Buffer position acts as an edge-stop), so
// the half-res dither grain is smoothed away while shaft edges stay crisp at
// geometry silhouettes (no bleeding across depth discontinuities). Bound with
// an ADDITIVE blend (src ONE, dst ONE) so the result adds onto the lit scene.
layout(location = 0) in vec2 vUv;
layout(set = 0, binding = 0) uniform sampler2D scatterTex;   // half-res in-scatter (nearest)
layout(set = 0, binding = 1) uniform sampler2D positionTex;  // full-res world position (nearest)
layout(location = 0) out vec4 outColor;

void main() {
    vec2 texel = 1.0 / vec2(textureSize(scatterTex, 0));
    vec3 center_pos = texture(positionTex, vUv).xyz;

    vec3  sum  = vec3(0.0);
    float wsum = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 uv = vUv + vec2(float(x), float(y)) * texel;
            vec3 s  = texture(scatterTex,  uv).rgb;
            vec3 p  = texture(positionTex, uv).xyz;
            float spatial = exp(-float(x * x + y * y) * 0.6);
            vec3  dpos = p - center_pos;
            // World-distance edge-stop: same-surface taps (small dpos) are
            // kept; silhouette taps (large dpos) are rejected. ~1.7u sigma.
            float range = exp(-dot(dpos, dpos) * 0.35);
            float w = spatial * range;
            sum  += s * w;
            wsum += w;
        }
    }
    outColor = vec4(sum / max(wsum, 1e-4), 1.0);
}
