#version 450
// Water surface vertex stage: a procedural grid (no vertex buffer — positions
// derived from gl_VertexIndex) spanning the water rect, displaced by a sum of
// directional waves. One draw per water surface; per-surface data in push
// constants, per-frame data in the UBO.

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    vec4 cam_time;     // xyz = camera pos, w = time (s)
    vec4 sun_dir;      // xyz = direction light->scene
    vec4 sun_color;    // xyz
    vec4 resolution;   // xy = render target size
} u;

layout(push_constant) uniform WaterPC {
    vec4 center_sx;    // xyz = surface center (world), w = size.x
    vec4 p0;           // x = size.y, y = wave_height, z = wave_speed, w = wave_scale
    vec4 deep_clarity; // rgb = deep color, w = clarity
    vec4 shallow_refl; // rgb = shallow color, w = reflectivity
} pc;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec2 outGridXZ;   // world xz (for FS normal reconstruction)

const int GRID = 128;   // cells per side; 2 triangles per cell

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
    // Decode grid cell + corner from the vertex index (6 verts per cell).
    int vid    = gl_VertexIndex;
    int cell   = vid / 6;
    int corner = vid % 6;
    int cx = cell % GRID;
    int cz = cell / GRID;
    // Two CCW triangles: (0,0)(0,1)(1,0) and (1,0)(0,1)(1,1).
    ivec2 off[6] = ivec2[6](ivec2(0,0), ivec2(0,1), ivec2(1,0),
                            ivec2(1,0), ivec2(0,1), ivec2(1,1));
    vec2 uv = (vec2(cx + off[corner].x, cz + off[corner].y) / float(GRID)) - 0.5;

    vec2 size = vec2(pc.center_sx.w, pc.p0.x);
    vec2 xz   = pc.center_sx.xz + uv * size;
    float y   = pc.center_sx.y + wave_h(xz, u.cam_time.w);

    outWorldPos = vec3(xz.x, y, xz.y);
    outGridXZ   = xz;
    gl_Position = u.proj * u.view * vec4(outWorldPos, 1.0);
}
