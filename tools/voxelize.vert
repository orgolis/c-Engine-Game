#version 450
// Voxelization vertex stage. Transforms a mesh vertex by an axis-aligned
// orthographic VP (one of the three dominant axes) so the rasterizer
// covers the voxel grid, and forwards the world position so the fragment
// can compute the destination voxel.

layout(location = 0) in vec3 inPosition;
// Mesh binding also carries normal/uv/tangent; unused here.

layout(push_constant) uniform PC {
    mat4 mvp;    // axis_ortho_vp * model
    mat4 model;  // for world position
} pc;

layout(location = 0) out vec3 outWorldPos;

void main() {
    outWorldPos = (pc.model * vec4(inPosition, 1.0)).xyz;
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
}
