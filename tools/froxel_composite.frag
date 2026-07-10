#version 450
// Froxel fog pass 3 (Stage 3.3): composite onto the HDR target. For each
// pixel: find its view distance (G-buffer world position; sky uses the fog
// far plane), map to the exponential slice coordinate, one trilinear tap of
// the integrated volume, and blend:
//     out = inscatter * 1 + hdr * transmittance
// (pipeline blend: src = ONE, dst = SRC_ALPHA; transmittance rides in alpha —
// the same scheme the cloud composite uses.)

const float NEAR     = 0.5;
const float FROXEL_Z = 64.0;

layout(set = 0, binding = 0) uniform FroxelUBO {
    mat4 inv_view_proj;
    mat4 shadow_matrix;
    vec4 cam_pos;
    vec4 sun_dir;
    vec4 sun_color;
    vec4 fog;          // w = far
    vec4 ambient;
} ub;

layout(set = 0, binding = 1) uniform sampler2D positionTex;  // w=1 where geometry
layout(set = 0, binding = 2) uniform sampler3D integratedTex;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 scene = texture(positionTex, inUV);
    float dist = scene.w > 0.5 ? length(scene.xyz - ub.cam_pos.xyz) : ub.fog.w;
    dist = clamp(dist, NEAR, ub.fog.w);

    // Inverse of the exponential slice mapping, normalized to [0,1] W.
    float slice = FROXEL_Z * log(dist / NEAR) / log(ub.fog.w / NEAR);
    float w = clamp(slice / FROXEL_Z, 0.0, 1.0);

    vec4 fog = texture(integratedTex, vec3(inUV, w));
    outColor = vec4(fog.rgb, fog.a);   // rgb = in-scatter, a = transmittance
}
