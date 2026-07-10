#version 450
// Terrain splat-blend G-buffer fragment stage.
//
// Reuses the standard material descriptor layout (set=1): the UBO + 5 combined
// image samplers, reinterpreted for terrain:
//   binding 0 (MaterialUBO): base_color_factor.xyzw = per-layer UV tiling
//                            (how many times each layer texture repeats across
//                            the terrain); metallic_factor / roughness_factor =
//                            terrain surface PBR factors.
//   binding 1 = splat map   (RGBA per-texel weights for layers 0..3), sampled
//                            at the terrain's [0,1] UV.
//   bindings 2..5 = layer 0..3 albedo, sampled at inUV * tiling[layer] (REPEAT
//                            sampler → seamless tiling).
//
// The 4 layer weights are normalised so the blend is energy-preserving even if
// the painted weights don't sum to 1.
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;

layout(set = 1, binding = 0) uniform MaterialUBO {
    vec4  base_color_factor;   // per-layer tiling (x=L0 .. w=L3)
    float metallic_factor;
    float roughness_factor;
    float occlusion_strength;
    float normal_scale;
    vec4  emissive_factor;
} mat;
layout(set = 1, binding = 1) uniform sampler2D splatMap;
layout(set = 1, binding = 2) uniform sampler2D layer0;
layout(set = 1, binding = 3) uniform sampler2D layer1;
layout(set = 1, binding = 4) uniform sampler2D layer2;
layout(set = 1, binding = 5) uniform sampler2D layer3;

layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outNormalRoughness;
layout(location = 2) out vec4 outAlbedoMetallic;
layout(location = 3) out vec4 outMaterial;

void main() {
    vec4 w = texture(splatMap, inUV);
    float sum = w.r + w.g + w.b + w.a;
    // Default to layer 0 where nothing was painted, so fresh terrain shows a
    // base coat instead of going black.
    if (sum < 1e-4) { w = vec4(1.0, 0.0, 0.0, 0.0); sum = 1.0; }
    w /= sum;

    vec4 tile = mat.base_color_factor;
    vec3 c0 = texture(layer0, inUV * tile.x).rgb;
    vec3 c1 = texture(layer1, inUV * tile.y).rgb;
    vec3 c2 = texture(layer2, inUV * tile.z).rgb;
    vec3 c3 = texture(layer3, inUV * tile.w).rgb;
    vec3 albedo = c0 * w.r + c1 * w.g + c2 * w.b + c3 * w.a;

    vec3 N = normalize(inNormal);
    float roughness = clamp(mat.roughness_factor, 0.04, 1.0);
    float metallic  = clamp(mat.metallic_factor, 0.0, 1.0);

    outPosition        = vec4(inWorldPos, 1.0);
    outNormalRoughness = vec4(N, roughness);
    outAlbedoMetallic  = vec4(albedo, metallic);
    outMaterial        = vec4(0.0, 0.0, 0.0, 1.0); // no emissive, full AO
}
