#version 450
// G-buffer vertex stage for INSTANCED scene draws -- gbuffer_scene.vert with
// its two matrices read from a storage buffer instead of push constants.
//
// Many draws share a mesh and a material (a scene of 20k primitives has a
// handful of distinct pairs). One draw call per object cost ~190 ns of CPU per
// object -- push constants, a draw call, frequent rebinds -- ~2.9 ms a frame at
// 15k draws. Batched, a pair is one vkCmdDrawIndexed with instanceCount = N,
// and each instance finds its matrices at gl_InstanceIndex (which in Vulkan
// already includes the draw's firstInstance).
//
// EXACTLY the math of gbuffer_scene.vert, on EXACTLY the same values: the CPU
// computes each instance's mvp the same way it computes the push constant, so
// both paths rasterise bit-identical positions. Do not fold mvp into
// viewProj * model here -- a different association of the same product changes
// the low bits, and with them which pixels an edge covers.
//
// Wind: a vertex whose tangent.w is 2 (grass blades; 1 is an ordinary
// bitangent sign, which the fragment stage only uses the sign of) sways with
// the wind, more toward its tip (uv.y 0 at the root, 1 at the tip). The sway
// is in world space -- gusts roll across a field -- and turned into the
// instance's own space, so the mvp product stays the CPU's.

struct InstanceData {
    mat4 mvp;     // projection * view * model, computed on the CPU
    mat4 model;   // model alone, for world-space position and the normal basis
};
layout(std430, set = 2, binding = 0) readonly buffer Instances {
    InstanceData inst[];
};

layout(set = 0, binding = 0) uniform FrameUBO {
    vec4 cameraPos;   // xyz = camera position, w = time (seconds) for the wind
} frame;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec3 outTangent;
layout(location = 4) out vec3 outBitangent;

void main() {
    const mat4 model = inst[gl_InstanceIndex].model;
    mat3 nrm = mat3(model[0].xyz, model[1].xyz, model[2].xyz);

    vec3 pos = inPosition;
    if (inTangent.w > 1.5) {
        vec3  root = (model * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
        float t    = frame.cameraPos.w;
        vec2  dir  = normalize(vec2(1.0, 0.35));
        float gust = sin(t * 0.9 + dot(root.xz, dir) * 0.08) * 0.5 + 0.5;   // gusts rolling across
        float sway = 0.35 + gust * 0.65;
        float flutter = sin(t * 3.3 + dot(root.xz, vec2(0.71, 0.53)) * 1.7) * 0.35;
        float w = inUV.y * inUV.y;                                           // root still, tip free
        vec3 world_off = vec3(dir.x, 0.0, dir.y) * (sway + flutter) * 0.10 * w;
        world_off.y = -0.03 * w * (sway + 0.3);                              // bending lowers the tip
        // Into the instance's space: model is rotation x uniform scale.
        float s2 = max(dot(model[0].xyz, model[0].xyz), 1e-6);
        pos += transpose(nrm) * world_off / s2;
    }

    vec4 wp = model * vec4(pos, 1.0);
    outWorldPos = wp.xyz;

    outNormal    = normalize(nrm * inNormal);
    outTangent   = normalize(nrm * inTangent.xyz);
    outBitangent = normalize(cross(outNormal, outTangent) * inTangent.w);
    outUV        = inUV;

    gl_Position = inst[gl_InstanceIndex].mvp * vec4(pos, 1.0);
}
