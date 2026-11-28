// Particle billboard fragment stage (3.9): soft disc, glow, ragged smoke, a lit
// orb, a hollow shell, turbulent fire or a thin ring,
// soft where it meets the scene, premultiplied.
#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec2 v_extra;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D u_depth;   // the scene's depth (read-only this pass)
// params: x, y the projection's [2][2] and [3][2] (depth -> distance), z 1 / soft distance, w 1 when depth is bound
layout(push_constant) uniform Push { mat4 view_proj; vec4 params; } pc;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}
float fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; ++i) { s += a * vnoise(p); p = p * 2.03 + vec2(17.1, 9.2); a *= 0.5; }
    return s;
}
float dist(float z) { float den = z + pc.params.x; return pc.params.y / (abs(den) < 1e-7 ? 1e-7 : den); }   // depth -> distance along the view (any sign convention)

void main() {
    vec2  d = v_uv * 2.0 - 1.0;
    float r = dot(d, d);
    if (r > 1.0) discard;                 // outside the disc: no square corners
    float shape;
    vec3  tint = vec3(1.0);               // shading on its colour (an orb's light, fire's heat)
    int   kind = int(v_extra.x + 0.5);
    float seed = v_extra.y;
    if (kind == 1) {
        // Glow: a hot core fading out smoothly (fire, flashes, sparks of light).
        shape = (exp(-r * 4.0) - 0.0183) / 0.9817;
    } else if (kind == 2) {
        // Smoke and dust: a ragged puff, turned by its seed, so neighbours
        // merge into a cloud instead of reading as separate discs.
        float a = seed * 6.2831853;
        vec2 q = mat2(cos(a), -sin(a), sin(a), cos(a)) * d;
        float n = fbm(q * 1.9 + seed * 41.0);
        float body = smoothstep(1.0, 0.15, r);
        shape = body * smoothstep(0.2, 0.75, n * 0.9 + body * 0.45);
    } else if (kind == 3) {
        // Orb: a ball -- a clean round edge and light on it. Giving light it is
        // brightest at its middle (a sun, an energy orb); covering, it is lit from
        // above like a solid sphere.
        float nz = sqrt(1.0 - r);
        float edge = smoothstep(1.0, 0.86, r);
        float swirl = fbm(d * 2.6 + seed * 37.0);
        if (v_color.a >= 0.0) {
            shape = edge * (0.3 + 0.7 * nz) * (0.8 + 0.4 * swirl);
        } else {
            shape = edge;
            float l = max(0.0, dot(vec3(d.x, d.y, nz), normalize(vec3(-0.35, 0.65, 0.68))));
            tint = vec3(0.4 + 0.8 * l) * (0.85 + 0.3 * swirl);
        }
    } else if (kind == 4) {
        // Shell: a hollow sphere, bright at its rim (a shockwave, a bubble, a shield).
        float nz = sqrt(1.0 - r);
        float edge = smoothstep(1.0, 0.94, r);
        float ripple = 0.85 + 0.3 * fbm(d * 3.0 + seed * 23.0);
        shape = edge * (pow(1.0 - nz, 2.5) * 1.1 + 0.06) * ripple;
    } else if (kind == 5) {
        // Fire: a flame -- upright (not turned by its seed), thinning into ragged
        // tongues towards its top, turbulent inside: hot streaks, darker edges.
        float n = fbm(vec2(d.x * 2.2, d.y * 1.3 - seed * 9.0) + seed * 31.0);
        float n2 = fbm(d * 4.7 + seed * 17.0);
        float taper = r + max(d.y, 0.0) * 0.55 * (1.2 - n);
        float body = smoothstep(1.0, 0.05, taper);
        shape = body * smoothstep(0.32, 0.78, n * 0.9 + body * 0.42);
        tint = vec3(0.5 + 1.0 * n2 * body);
    } else if (kind == 6) {
        // Ring: a thin ring with a faint fill (flat on the ground: a shockwave).
        float rr = sqrt(r);
        float x = (rr - 0.86) / 0.07;
        shape = exp(-x * x) + 0.08 * (1.0 - r);
    } else {
        float f = 1.0 - r;
        shape = f * f;                    // the soft disc
    }
    // Soft where it meets the scene: no hard line where smoke sinks into the ground.
    if (pc.params.w > 0.5) {
        float scene = dist(texelFetch(u_depth, ivec2(gl_FragCoord.xy), 0).r);
        float me = dist(gl_FragCoord.z);
        shape *= clamp((scene - me) * pc.params.z, 0.0, 1.0);
    }
    // Premultiplied, blended (ONE, ONE_MINUS_SRC_ALPHA). Alpha above zero adds
    // light (fire, sparks: nothing behind is covered); alpha below zero covers
    // what is behind by that much (smoke, dust).
    float cov = abs(v_color.a) * shape;
    out_color = vec4(v_color.rgb * tint * cov, v_color.a < 0.0 ? cov : 0.0);
}
