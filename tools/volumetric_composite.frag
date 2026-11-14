#version 450
// Joint bilateral upsample of the half-res in-scatter into the HDR target.
// A 3x3 neighbourhood of half-res taps is weighted by spatial falloff AND
// world-position similarity (rebuilt from depth -- F5 -- it acts as an edge-stop), so
// the half-res dither grain is smoothed away while shaft edges stay crisp at
// geometry silhouettes (no bleeding across depth discontinuities). Bound with
// an ADDITIVE blend (src ONE, dst ONE) so the result adds onto the lit scene.
layout(location = 0) in vec2 vUv;
layout(set = 0, binding = 0) uniform sampler2D scatterTex;   // half-res in-scatter (nearest)
layout(set = 0, binding = 1) uniform sampler2D depthTex;     // full-res depth (nearest)
// The light-shaft pass's own UBO; only inv_view_proj is read here.
layout(set = 0, binding = 2, std140) uniform Params {
    mat4 inv_view_proj;
    mat4 shadow_matrix;
    vec4 camera_pos;
    vec4 sun_dir;
    vec4 sun_color;
    vec4 params;
    vec4 misc;
    vec4 cloud_params;
} ub;
layout(location = 0) out vec4 outColor;

// World position from depth (F5) -- this pass used to read a position target.
vec3 worldPos(vec2 uv) {
    float d = texture(depthTex, uv).r;
    vec4 h = ub.inv_view_proj * vec4(uv * 2.0 - 1.0, d, 1.0);
    return h.xyz / h.w;
}

void main() {
    vec2 texel = 1.0 / vec2(textureSize(scatterTex, 0));
    // The sky (depth 1) has no surface: every sky pixel reconstructs to the far
    // plane, far from its neighbours, so a world-distance edge-stop kept none
    // of them -- the sky showed the march's raw dither as white speckles. Sky
    // taps now blend with sky taps (and never with geometry).
    const float kSky = 1.0 - 1e-5;
    bool center_sky = texture(depthTex, vUv).r >= kSky;
    vec3 center_pos = worldPos(vUv);

    vec3  sum  = vec3(0.0);
    float wsum = 0.0;
    // 5x5 half-res taps: the dither grain smoothed into continuous shafts.
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 uv = vUv + vec2(float(x), float(y)) * texel;
            vec3 s  = texture(scatterTex,  uv).rgb;
            bool sky = texture(depthTex, uv).r >= kSky;
            float spatial = exp(-float(x * x + y * y) * 0.3);
            float range;
            if (center_sky || sky) {
                range = (center_sky == sky) ? 1.0 : 0.0;
            } else {
                // World-distance edge-stop: same-surface taps (small dpos) are
                // kept; silhouette taps (large dpos) are rejected. ~1.7u sigma.
                vec3 dpos = worldPos(uv) - center_pos;
                range = exp(-dot(dpos, dpos) * 0.35);
            }
            float w = spatial * range;
            sum  += s * w;
            wsum += w;
        }
    }
    outColor = vec4(sum / max(wsum, 1e-4), 1.0);
}
