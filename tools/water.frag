#version 450
// Water surface fragment stage: analytic wave normals, Schlick fresnel with
// environment-cubemap reflection, sun glint, depth-based shallow->deep color
// and shoreline softness, and a MANUAL depth test against the G-buffer world
// position (the pass has no depth attachment).

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    vec4 cam_time;     // xyz = camera pos, w = time (s)
    vec4 sun_dir;      // xyz = direction light->scene
    vec4 sun_color;    // xyz
    vec4 resolution;   // xy = render target size
} u;

layout(set = 0, binding = 1) uniform sampler2D positionTex;  // G-buffer world pos (w=1 where geometry)
layout(set = 0, binding = 2) uniform samplerCube envMap;

layout(push_constant) uniform WaterPC {
    vec4 center_sx;    // xyz = surface center (world), w = size.x
    vec4 p0;           // x = size.y, y = wave_height, z = wave_speed, w = wave_scale
    vec4 deep_clarity; // rgb = deep color, w = clarity
    vec4 shallow_refl; // rgb = shallow color, w = reflectivity
} pc;

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec2 inGridXZ;

layout(location = 0) out vec4 outColor;

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

    // Wave normal from central differences of the height field.
    const float e = 0.18;
    float hx = wave_h(inGridXZ + vec2(e, 0), t) - wave_h(inGridXZ - vec2(e, 0), t);
    float hz = wave_h(inGridXZ + vec2(0, e), t) - wave_h(inGridXZ - vec2(0, e), t);
    vec3 N = normalize(vec3(-hx / (2.0 * e), 1.0, -hz / (2.0 * e)));

    // Manual depth test + water depth from the G-buffer world position.
    vec2 uv = gl_FragCoord.xy / u.resolution.xy;
    vec4 scene = texture(positionTex, uv);
    float frag_dist  = length(inWorldPos - cam);
    float scene_dist = scene.w > 0.5 ? length(scene.xyz - cam) : 1e9;
    if (scene_dist < frag_dist - 0.05) discard;        // opaque geometry in front

    float depth_diff = scene_dist - frag_dist;         // water thickness along the ray
    float depth_fac  = clamp(depth_diff / pc.deep_clarity.w, 0.0, 1.0);

    vec3 V = normalize(cam - inWorldPos);
    if (V.y < 0.0) N = -N;                             // looking up from below

    float ndv = clamp(dot(N, V), 0.0, 1.0);
    float fresnel = 0.02 + 0.98 * pow(1.0 - ndv, 5.0);

    vec3 base = mix(pc.shallow_refl.rgb, pc.deep_clarity.rgb, depth_fac);
    vec3 R    = reflect(-V, N);
    vec3 refl = texture(envMap, R).rgb;

    vec3 L = normalize(-u.sun_dir.xyz);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 240.0);

    vec3 color = mix(base, refl, clamp(fresnel * pc.shallow_refl.w * 2.0, 0.0, 1.0))
               + u.sun_color.xyz * spec * 2.5;

    // Shoreline softness: thin water is nearly transparent; fresnel keeps
    // grazing angles reflective and opaque.
    float alpha = clamp(mix(0.12, 0.88, depth_fac) + fresnel * 0.6, 0.0, 1.0);
    outColor = vec4(color, alpha);
}
