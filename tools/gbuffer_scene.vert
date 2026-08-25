#version 450
// G-buffer vertex stage for ordinary scene meshes.
//
// RECOVERED, 2026-08-25. This source was missing from the repo: 607c5d9
// ("extract inline GLSL to files") moved the other shaders out of C++ string
// literals and left this one behind, so only its compiled words survived, in
// gbuffer_scene_spirv.h. Regenerating that header the ordinary way would have
// deleted a shader nobody could rebuild.
//
// It was reconstructed by disassembling kGBufferSceneVertSpv with spirv-cross.
// The interface it declares -- push constants, inputs, outputs and their
// locations -- is asserted against the shipped SPIR-V by gbuffer_vert_check,
// because "close enough" here means silently mismatched vertex attributes.
//
// The push block is EXACTLY 128 bytes, which is Vulkan's guaranteed minimum
// maxPushConstantsSize. There is no room to add anything: a per-frame value the
// fragment stage needs (a camera position for parallax, say) has to arrive
// through a descriptor set, not through here.

layout(push_constant) uniform PC {
    mat4 mvp;     // projection * view * model
    mat4 model;   // model alone, for world-space position and the normal basis
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
// w carries the handedness of the tangent basis, which is why this is a vec4
// while the others are vec3 -- mirrored UVs flip the bitangent and without it
// every mirrored surface lights inside out.
layout(location = 3) in vec4 inTangent;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec3 outTangent;
layout(location = 4) out vec3 outBitangent;

void main() {
    vec4 wp = pc.model * vec4(inPosition, 1.0);
    outWorldPos = wp.xyz;

    // Upper 3x3 of the model matrix. Correct for the rigid and uniformly scaled
    // transforms this engine uses; a non-uniform scale would need the inverse
    // transpose, and would skew the normals here instead.
    mat3 nrm = mat3(pc.model[0].xyz, pc.model[1].xyz, pc.model[2].xyz);

    outNormal    = normalize(nrm * inNormal);
    outTangent   = normalize(nrm * inTangent.xyz);
    outBitangent = normalize(cross(outNormal, outTangent) * inTangent.w);
    outUV        = inUV;

    gl_Position = pc.mvp * vec4(inPosition, 1.0);
}
