#version 450
// Terrain splat-blend G-buffer fragment stage — full per-layer PBR.
//
// WHAT CHANGED AND WHY. This shader used to reuse the standard 6-binding
// material descriptor layout, reinterpreting its five sampler slots as
// [splatmap, layer0..3 ALBEDO]. That capped a terrain layer at one colour
// texture and a tiling number, with roughness hardcoded at 0.95 and metallic at
// 0 for every square metre of every terrain — so wet rock, dry sand and packed
// mud were all the same material with different pictures painted on them, and no
// terrain could ever have surface relief.
//
// Terrain now has its OWN descriptor set layout (14 bindings) because there is
// no way to fit four layers × three maps into five samplers. It is a separate
// layout rather than a widened shared one: the shared layout is used by the
// scene pipeline, the transparent pass and the shadow caster, and growing it
// would make every ordinary material carry eight unused sampler descriptors.
//
//   binding 0  TerrainMaterialUBO — per-layer tiling / tint / PBR factors
//   binding 1  splat map, RGBA per-texel weights for layers 0..3, at terrain UV
//   binding 2..5   layer 0..3 ALBEDO   (sRGB)
//   binding 6..9   layer 0..3 NORMAL   (linear, tangent space)
//   binding 10..13 layer 0..3 METAL/ROUGH (linear; glTF packing, G=rough B=metal)
//
// Every map is sampled at inUV * tiling[layer] with a REPEAT sampler, so a layer
// tiles seamlessly and independently of the others.
//
// The four weights are normalised, so the blend is energy-preserving even when
// the painted weights do not sum to 1 — a partially painted terrain must not
// darken.
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;

layout(set = 1, binding = 0) uniform TerrainMaterialUBO {
    // Per-layer UV tiling: how many times layer i repeats across the terrain.
    vec4 tiling;         // x = L0 .. w = L3
    // Per-layer base colour, multiplying that layer's albedo texture.
    vec4 tint[4];
    // Per-layer (metallic, roughness, normal_scale, occlusion). Packed as vec4s
    // rather than four float[4] arrays because std140 pads a float array to a
    // 16-byte stride anyway — this way the padding is at least readable.
    vec4 mrno[4];
} mat;

layout(set = 1, binding = 1)  uniform sampler2D splatMap;
layout(set = 1, binding = 2)  uniform sampler2D albedo0;
layout(set = 1, binding = 3)  uniform sampler2D albedo1;
layout(set = 1, binding = 4)  uniform sampler2D albedo2;
layout(set = 1, binding = 5)  uniform sampler2D albedo3;
layout(set = 1, binding = 6)  uniform sampler2D normal0;
layout(set = 1, binding = 7)  uniform sampler2D normal1;
layout(set = 1, binding = 8)  uniform sampler2D normal2;
layout(set = 1, binding = 9)  uniform sampler2D normal3;
layout(set = 1, binding = 10) uniform sampler2D mr0;
layout(set = 1, binding = 11) uniform sampler2D mr1;
layout(set = 1, binding = 12) uniform sampler2D mr2;
layout(set = 1, binding = 13) uniform sampler2D mr3;

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

    vec2 uv0 = inUV * mat.tiling.x;
    vec2 uv1 = inUV * mat.tiling.y;
    vec2 uv2 = inUV * mat.tiling.z;
    vec2 uv3 = inUV * mat.tiling.w;

    // ---- Albedo: each layer's texture times its tint, weighted -------------
    vec3 albedo = texture(albedo0, uv0).rgb * mat.tint[0].rgb * w.r
                + texture(albedo1, uv1).rgb * mat.tint[1].rgb * w.g
                + texture(albedo2, uv2).rgb * mat.tint[2].rgb * w.b
                + texture(albedo3, uv3).rgb * mat.tint[3].rgb * w.a;

    // ---- Normals: unpack each layer, scale, blend in TANGENT space ---------
    // Blending tangent-space normals before the TBN transform (rather than
    // transforming each and blending world-space normals) is both cheaper and
    // better behaved: the weights already sum to 1, so the blend stays inside
    // the tangent hemisphere and normalising at the end is enough.
    //
    // An unbound slot holds the engine's flat-blue default, which unpacks to
    // (0,0,1) — the identity. That is what makes "no normal map on this layer"
    // cost nothing and change nothing, rather than needing a shader branch.
    vec3 n0 = texture(normal0, uv0).xyz * 2.0 - 1.0;
    vec3 n1 = texture(normal1, uv1).xyz * 2.0 - 1.0;
    vec3 n2 = texture(normal2, uv2).xyz * 2.0 - 1.0;
    vec3 n3 = texture(normal3, uv3).xyz * 2.0 - 1.0;
    n0.xy *= mat.mrno[0].z;
    n1.xy *= mat.mrno[1].z;
    n2.xy *= mat.mrno[2].z;
    n3.xy *= mat.mrno[3].z;
    vec3 nts = n0 * w.r + n1 * w.g + n2 * w.b + n3 * w.a;
    // A fully cancelled blend (opposed normals at equal weight) would leave a
    // zero vector; normalize() of zero is undefined and shows up as black
    // speckle. Fall back to the geometric normal instead.
    nts = (dot(nts, nts) < 1e-8) ? vec3(0.0, 0.0, 1.0) : normalize(nts);

    mat3 TBN = mat3(normalize(inTangent), normalize(inBitangent), normalize(inNormal));
    vec3 N = normalize(TBN * nts);

    // ---- Metal / rough: texture channel times the layer's factor ----------
    // glTF packing, matching gbuffer_scene.frag so a material behaves the same
    // on terrain as on a mesh: GREEN is roughness, BLUE is metallic. An unbound
    // slot holds the shared white default, so the factor passes through alone.
    vec4 s0 = texture(mr0, uv0);
    vec4 s1 = texture(mr1, uv1);
    vec4 s2 = texture(mr2, uv2);
    vec4 s3 = texture(mr3, uv3);
    float roughness = s0.g * mat.mrno[0].y * w.r
                    + s1.g * mat.mrno[1].y * w.g
                    + s2.g * mat.mrno[2].y * w.b
                    + s3.g * mat.mrno[3].y * w.a;
    float metallic  = s0.b * mat.mrno[0].x * w.r
                    + s1.b * mat.mrno[1].x * w.g
                    + s2.b * mat.mrno[2].x * w.b
                    + s3.b * mat.mrno[3].x * w.a;
    float ao = mat.mrno[0].w * w.r + mat.mrno[1].w * w.g
             + mat.mrno[2].w * w.b + mat.mrno[3].w * w.a;

    roughness = clamp(roughness, 0.04, 1.0);
    metallic  = clamp(metallic, 0.0, 1.0);
    ao        = clamp(ao, 0.0, 1.0);

    outPosition        = vec4(inWorldPos, 1.0);
    outNormalRoughness = vec4(N, roughness);
    outAlbedoMetallic  = vec4(albedo, metallic);
    // Terrain has no emissive channel — a glowing terrain layer is not a thing
    // anyone has asked for, and the four spare UBO floats it would cost are
    // better spent on occlusion, which every terrain material has.
    outMaterial        = vec4(0.0, 0.0, 0.0, ao);
}
