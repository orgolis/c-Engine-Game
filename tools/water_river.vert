#version 450
// River surface vertex stage: a ribbon along the river's path, built on the
// CPU (VulkanWaterPass::add_river) -- one continuous surface whose level falls
// with the bed, instead of a chain of flat rects. Feeds water.frag.

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    vec4 cam_time;     // xyz = camera pos, w = time (s)
    vec4 sun_dir;
    vec4 sun_color;
    vec4 resolution;
} u;

layout(push_constant) uniform WaterPC {
    vec4 center_sx;
    vec4 p0;           // y = wave_height, z = wave_speed, w = wave_scale
    vec4 deep_clarity;
    vec4 shallow_refl;
    vec4 river;        // x = 1 for a river, y = flow speed
} pc;

layout(location = 0) in vec3 inPos;     // world position on the surface
layout(location = 1) in vec2 inFlow;    // downstream direction x speed (world xz per second)
layout(location = 2) in vec2 inAlong;   // metres along the river, -1..1 across it

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec2 outFlow;
layout(location = 2) out vec2 outAlong;

void main() {
    // A little swell, more in the middle than at the banks.
    float t = u.cam_time.w;
    float k = 6.2831853 / max(pc.p0.w, 0.5);
    float h = sin(inAlong.x * k - t * pc.p0.z * 2.0) * 0.6 + sin(inPos.x * k * 1.7 + inPos.z * k * 1.3 + t) * 0.4;
    vec3 p = inPos + vec3(0.0, h * pc.p0.y * (1.0 - inAlong.y * inAlong.y), 0.0);
    outWorldPos = p;
    outFlow     = inFlow;
    outAlong    = inAlong;
    gl_Position = u.proj * u.view * vec4(p, 1.0);
}
