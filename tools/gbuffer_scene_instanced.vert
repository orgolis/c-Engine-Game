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

struct InstanceData {
    mat4 mvp;     // projection * view * model, computed on the CPU
    mat4 model;   // model alone, for world-space position and the normal basis
};
layout(std430, set = 2, binding = 0) readonly buffer Instances {
    InstanceData inst[];
};

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
    vec4 wp = model * vec4(inPosition, 1.0);
    outWorldPos = wp.xyz;

    mat3 nrm = mat3(model[0].xyz, model[1].xyz, model[2].xyz);

    outNormal    = normalize(nrm * inNormal);
    outTangent   = normalize(nrm * inTangent.xyz);
    outBitangent = normalize(cross(outNormal, outTangent) * inTangent.w);
    outUV        = inUV;

    gl_Position = inst[gl_InstanceIndex].mvp * vec4(inPosition, 1.0);
}
