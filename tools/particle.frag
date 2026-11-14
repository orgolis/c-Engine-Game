// Particle billboard fragment stage (3.9): soft disc, glow or ragged smoke,
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
    int kind = int(v_extra.x + 0.5);
    if (kind == 1) {
        // Glow: a hot core fading out smoothly (fire, flashes, sparks of light).
        shape = (exp(-r * 4.0) - 0.0183) / 0.9817;
    } else if (kind == 2) {
        // Smoke and dust: a ragged puff, turned by its seed, so neighbours
        // merge into a cloud instead of reading as separate discs.
        float a = v_extra.y * 6.2831853;
        vec2 q = mat2(cos(a), -sin(a), sin(a), cos(a)) * d;
        float n = fbm(q * 1.9 + v_extra.y * 41.0);
        float body = smoothstep(1.0, 0.15, r);
        shape = body * smoothstep(0.2, 0.75, n * 0.9 + body * 0.45);
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
    out_color = vec4(v_color.rgb * cov, v_color.a < 0.0 ? cov : 0.0);
}
