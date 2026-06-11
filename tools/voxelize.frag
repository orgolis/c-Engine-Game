#version 450
// Voxelization fragment stage. Maps the interpolated world position into
// the voxel grid and marks that voxel occupied. No color attachment — the
// render pass exists purely for this imageStore side effect.

layout(location = 0) in vec3 inWorldPos;

layout(set = 0, binding = 0, r8) uniform writeonly image3D voxelTex;
layout(set = 0, binding = 1) uniform VolUBO {
    vec4  vol_min;    // xyz = world-space min corner
    vec4  vol_size;   // xyz = world-space extent
    vec4  grid;       // x = grid resolution (cubes per side)
} vol;

void main() {
    vec3 p = (inWorldPos - vol.vol_min.xyz) / vol.vol_size.xyz;
    if (any(lessThan(p, vec3(0.0))) || any(greaterThan(p, vec3(1.0)))) discard;
    ivec3 c = ivec3(p * vol.grid.x);
    imageStore(voxelTex, c, vec4(1.0));
}
