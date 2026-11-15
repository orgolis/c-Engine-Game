#version 450
// Water surface fragment stage (lakes, the sea and rivers).
//
// Ripples are noise, not three sines: two layers drifting along the flow (a
// river's downstream direction, or a slow drift on still water) and blended
// so the pattern never stretches. They fade with distance, where they would
// only alias into streaks. Colour comes from how much water the eye looks
// through (from the G-buffer depth behind): thin water is clear and shows
// its bed, thick water takes the deep colour. Fresnel reflection of the sky
// (softened), the sun's glint, and foam where water meets the bank or runs
// fast. A MANUAL depth test against the G-buffer (the pass has no depth
// attachment).

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    vec4 cam_time;     // xyz = camera pos, w = time (s)
    vec4 sun_dir;      // xyz = direction light->scene
    vec4 sun_color;    // xyz
    vec4 resolution;   // xy = render target size
    mat4 inv_view_proj; // inverse(proj * view): scene position from depth (F5)
    vec4 ripples[24];   // rings: x, z, start time, strength (0: none)
} u;

layout(set = 0, binding = 1) uniform sampler2D depthTex;     // G-buffer depth (1.0 = sky)
layout(set = 0, binding = 2) uniform samplerCube envMap;

layout(push_constant) uniform WaterPC {
    vec4 center_sx;    // xyz = surface center (world), w = size.x
    vec4 p0;           // x = size.y, y = wave_height, z = wave_speed, w = wave_scale
    vec4 deep_clarity; // rgb = deep color, w = clarity
    vec4 shallow_refl; // rgb = shallow color, w = reflectivity
    vec4 river;        // x = 1 for a river, y = flow speed
} pc;

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec2 inFlow;
layout(location = 2) in vec2 inAlong;

layout(location = 0) out vec4 outColor;

float hash(vec2 p) {
    // Stable on every GPU (no large-argument sin).
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 w = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), w.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), w.x), w.y);
}
float ripples(vec2 p) {
    return vnoise(p) * 0.5 + vnoise(p * 2.13 + vec2(3.7, 1.9)) * 0.3 + vnoise(p * 4.71 + vec2(7.1, 5.3)) * 0.2;
}
vec3 ripple_normal(vec2 p, float strength) {
    const float e = 0.07;
    float h  = ripples(p);
    float hx = ripples(p + vec2(e, 0.0));
    float hz = ripples(p + vec2(0.0, e));
    return normalize(vec3(-(hx - h) / e * strength, 1.0, -(hz - h) / e * strength));
}

float wave_h(vec2 p, float t) {
    float k = 6.2831853 / pc.p0.w;
    float s = pc.p0.z;
    vec2 d1 = normalize(vec2( 1.0, 0.15));
    vec2 d2 = normalize(vec2(-0.4, 0.90));
    vec2 d3 = normalize(vec2( 0.7,-0.70));
    float h = 0.0;
    h += 0.55 * sin(dot(p, d1) * k       + t * s * 1.00);
    h += 0.30 * sin(dot(p, d2) * k * 1.9 + t * s * 1.35);
    h += 0.15 * sin(dot(p, d3) * k * 3.7 + t * s * 0.80);
    return h * pc.p0.y;
}

void main() {
    const float t = u.cam_time.w;
    const vec3 cam = u.cam_time.xyz;
    const bool is_river = pc.river.x > 0.5;

    // Manual depth test, and how much water the eye looks through.
    vec2 suv = gl_FragCoord.xy / u.resolution.xy;
    float d = texture(depthTex, suv).r;
    float frag_dist  = length(inWorldPos - cam);
    float scene_dist = 1e9;
    if (d < 1.0 - 1e-5) {
        vec4 h = u.inv_view_proj * vec4(suv * 2.0 - 1.0, d, 1.0);
        scene_dist = length(h.xyz / h.w - cam);
    }
    if (scene_dist < frag_dist - 0.05) discard;        // opaque geometry in front
    float thickness = scene_dist - frag_dist;          // water along the ray

    vec3 V = normalize(cam - inWorldPos);

    // ---- surface normal ----
    // Big swell (lakes, the sea): from the displaced wave field.
    vec3 N = vec3(0.0, 1.0, 0.0);
    if (!is_river && pc.p0.y > 0.001) {
        const float e = 0.18;
        float hx = wave_h(inWorldPos.xz + vec2(e, 0), t) - wave_h(inWorldPos.xz - vec2(e, 0), t);
        float hz = wave_h(inWorldPos.xz + vec2(0, e), t) - wave_h(inWorldPos.xz - vec2(0, e), t);
        N = normalize(vec3(-hx / (2.0 * e), 1.0, -hz / (2.0 * e)));
    }
    // Ripples: two layers carried along the flow, half a cycle apart, each
    // weighted down while it resets -- so the pattern moves and never smears.
    float scale  = 1.6 / max(pc.p0.w, 0.5) + 0.35;
    float fade   = 1.0 / (1.0 + frag_dist * 0.035);      // far away: calmer, no aliasing
    float strength = (is_river ? 0.55 : 0.35) * fade;
    vec2  flow   = inFlow;
    float cycle  = 1.6;
    float ph0 = fract(t / cycle), ph1 = fract(t / cycle + 0.5);
    vec2  p   = inWorldPos.xz * scale;
    vec3  n0  = ripple_normal(p - flow * scale * ph0 * cycle, strength);
    vec3  n1  = ripple_normal(p - flow * scale * ph1 * cycle + vec2(0.37, 0.71), strength);
    float w0  = 1.0 - abs(2.0 * ph0 - 1.0);
    vec3  rn  = normalize(mix(n1, n0, w0));
    N = normalize(vec3(N.xz + rn.xz, 0.0).xzy + vec3(0.0, N.y * rn.y, 0.0));

    // Rings: from where something touched the water (a step, a round, a
    // blast), a few wavelets spreading out and fading over three seconds.
    vec2  rgrad = vec2(0.0);
    float rfoam = 0.0;
    for (int i = 0; i < 24; ++i) {
        vec4 r = u.ripples[i];
        if (r.w <= 0.0) continue;
        float age = t - r.z;
        if (age < 0.0 || age > 3.0) continue;
        vec2  dv = inWorldPos.xz - r.xy;
        float d = length(dv);
        float front = 0.15 + age * (1.1 + 0.5 * r.w);
        float x = d - front;
        float width = 0.12 + age * 0.22;
        float life = 1.0 - age / 3.0;
        float env = exp(-x * x / (width * width)) * r.w * life * life / (1.0 + front * 0.5);
        float k = 6.2831853 / (0.28 + age * 0.14);
        if (d > 1e-3) rgrad += dv / d * cos(x * k) * env * 1.4;
        rfoam = max(rfoam, env * (1.0 - smoothstep(0.0, 0.9, age)) * 1.6);
    }
    N = normalize(N - vec3(rgrad.x, 0.0, rgrad.y));
    if (V.y < 0.0) N = -N;                             // looking up from below

    // ---- colour from depth ----
    float clarity = max(pc.deep_clarity.w, 0.05);
    float trans   = exp(-thickness / clarity);         // 1 = see right through
    vec3  body    = mix(pc.deep_clarity.rgb, pc.shallow_refl.rgb, trans);

    // ---- reflection ----
    float ndv     = clamp(dot(N, V), 0.0, 1.0);
    float fresnel = 0.02 + 0.98 * pow(1.0 - ndv, 5.0);
    vec3  R       = reflect(-V, N);
    R.y = abs(R.y);                                   // never the ground under the horizon
    vec3  refl    = textureLod(envMap, R, 1.5).rgb;
    refl = refl / (1.0 + max(max(refl.r, refl.g), refl.b) * 0.15);   // soften the hot sky
    float rk      = clamp(fresnel * pc.shallow_refl.w, 0.0, 1.0);

    vec3 L = normalize(-u.sun_dir.xyz);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 320.0) * (0.3 + 0.7 * fresnel);

    vec3  color = mix(body, refl, rk) + u.sun_color.xyz * spec * 3.0;
    float alpha = clamp((1.0 - trans) * 0.92 + rk, 0.0, 1.0);

    // ---- foam: at the banks, and where a river runs fast ----
    vec2  fp     = inWorldPos.xz * 2.2 - flow * t * 2.2;
    float fn     = ripples(fp) * 0.65 + ripples(fp * 2.7 + vec2(1.3, 4.1)) * 0.35;
    float shore  = 1.0 - smoothstep(0.0, 0.45, thickness * max(abs(V.y), 0.25));
    float rapids = is_river ? smoothstep(1.4, 3.0, length(flow)) * (0.35 + 0.65 * abs(inAlong.y)) : 0.0;
    float foam   = clamp(smoothstep(0.52, 0.72, fn) * shore + smoothstep(0.55, 0.75, fn) * rapids, 0.0, 1.0) * fade;
    foam = max(foam, clamp(rfoam * (0.55 + 0.45 * fn), 0.0, 0.85));   // the white of a fresh ring
    color = mix(color, vec3(0.85, 0.88, 0.88) * (0.35 + 0.65 * max(dot(vec3(0.0, 1.0, 0.0), L), 0.0)), foam * 0.85);
    alpha = max(alpha, foam * 0.9);

    // The very edge of thin water fades out, so banks do not show a seam.
    alpha *= smoothstep(0.0, 0.06, thickness);
    outColor = vec4(color, alpha);
}
