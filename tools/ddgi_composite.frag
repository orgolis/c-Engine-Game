#version 450
// DDGI composite — adds probe-grid indirect diffuse onto the lit HDR target
// (blend ONE, ONE). Per pixel: 8-probe trilinear gather with smooth backface
// weighting + Chebyshev visibility (mean / mean^2 from the depth atlas) so
// probes behind walls don't leak light.

layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0, std140) uniform DdgiUBO {
    vec4  origin;     // xyz = grid origin,  w = intensity
    vec4  spacing;    // xyz = probe spacing, w = hysteresis (trace only)
    ivec4 counts;     // xyz = probe counts,  w = frame index
    vec4  sun_dir;    // xyz = sun (trace only), w = normal bias
    vec4  sun_color;  // rgb (trace only), w = max ray distance
    vec4  ambient;    // rgb (trace only), w = enabled
} u;

layout(set = 0, binding = 1) uniform sampler2D positionTex;
layout(set = 0, binding = 2) uniform sampler2D normalTex;
layout(set = 0, binding = 3) uniform sampler2D albedoTex;
layout(set = 0, binding = 4) uniform sampler2D depthTex;
layout(set = 0, binding = 5) uniform sampler2D irradianceAtlas;
layout(set = 0, binding = 6) uniform sampler2D depthAtlas;

const int kIrrSize = 8;    // must match ddgi_trace.comp
const int kDepSize = 16;

// ---- octahedral encode (must match ddgi_trace.comp's decode) ---------------
vec2 octWrap(vec2 v) {
    return (1.0 - abs(v.yx)) * vec2(v.x >= 0.0 ? 1.0 : -1.0,
                                    v.y >= 0.0 ? 1.0 : -1.0);
}
vec2 octEncode(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 p = n.xy;
    if (n.z < 0.0) p = octWrap(p);
    return p;   // [-1,1]
}

// Atlas UV for probe (px,py,pz), direction d, interior size S (+1px border).
vec2 atlas_uv(ivec3 pi, vec3 d, int S) {
    const int tile = S + 2;
    vec2 atlas_dims = vec2(float(u.counts.x * tile),
                           float(u.counts.y * u.counts.z * tile));
    vec2 base = vec2(float(pi.x * tile),
                     float((pi.y * u.counts.z + pi.z) * tile));
    vec2 oct01 = octEncode(d) * 0.5 + 0.5;
    return (base + 1.0 + oct01 * float(S)) / atlas_dims;
}

void main() {
    if (u.ambient.w < 0.5) { outColor = vec4(0.0); return; }
    // Sky gate — same far-plane test as the other screen passes.
    if (texture(depthTex, inUV).r >= 1.0 - 1e-5) { outColor = vec4(0.0); return; }

    vec4 nrm = texture(normalTex, inUV);
    vec3 N = nrm.xyz;
    if (dot(N, N) < 0.01) { outColor = vec4(0.0); return; }
    N = normalize(N);

    vec3 P      = texture(positionTex, inUV).xyz;
    vec3 albedo = texture(albedoTex, inUV).rgb;

    // Bias the sample point off the surface toward the normal so surface
    // points don't self-shadow against their own probe cell.
    vec3 biased = P + N * u.sun_dir.w;

    vec3 g    = (biased - u.origin.xyz) / u.spacing.xyz;
    ivec3 cnt = u.counts.xyz;
    ivec3 b0  = clamp(ivec3(floor(g)), ivec3(0), max(cnt - 2, ivec3(0)));
    vec3 f    = clamp(g - vec3(b0), 0.0, 1.0);

    vec3  sum_irr = vec3(0.0);
    float sum_w   = 0.0;
    for (int c = 0; c < 8; ++c) {
        ivec3 off = ivec3(c & 1, (c >> 1) & 1, (c >> 2) & 1);
        ivec3 pi  = clamp(b0 + off, ivec3(0), cnt - 1);
        vec3 probe_pos = u.origin.xyz + vec3(pi) * u.spacing.xyz;

        vec3 tw3 = mix(1.0 - f, f, vec3(off));
        float w  = tw3.x * tw3.y * tw3.z + 1e-5;

        // Smooth backface: probes behind the surface contribute little.
        vec3 dir_to_probe = normalize(probe_pos - P);
        float wn = (dot(dir_to_probe, N) + 1.0) * 0.5;
        w *= wn * wn + 0.2;

        // Chebyshev visibility from the depth tile (direction probe->point).
        vec3  dvec = biased - probe_pos;
        float r    = length(dvec);
        if (r > 1e-4) {
            vec2 md = texture(depthAtlas, atlas_uv(pi, dvec / r, kDepSize)).rg;
            if (r > md.x) {
                float var = max(md.y - md.x * md.x, 1e-4);
                float dd  = r - md.x;
                float vis = var / (var + dd * dd);
                w *= max(vis * vis * vis, 0.05);
            }
        }

        sum_irr += w * texture(irradianceAtlas, atlas_uv(pi, N, kIrrSize)).rgb;
        sum_w   += w;
    }

    vec3 irr = (sum_w > 1e-6) ? (sum_irr / sum_w) : vec3(0.0);
    outColor = vec4(albedo * irr * u.origin.w, 1.0);   // additive (ONE, ONE)
}
