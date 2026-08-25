#version 450
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;

// Per-frame values, shared by every draw in the pass. Set 0 was an empty
// layout until v0.7.11; the camera position cannot live in push constants
// because the vertex block is already at Vulkan's 128-byte floor.
layout(set = 0, binding = 0) uniform FrameUBO {
    vec4 cameraPos;   // xyz = world position, w unused
} frame;

layout(set = 1, binding = 0) uniform MaterialUBO {
    vec4  base_color_factor;
    float metallic_factor;
    float roughness_factor;
    float occlusion_strength;
    float normal_scale;
    vec4  emissive_factor;
    // xy = UV scale, zw = UV offset. Appended, so a shader that does not
    // declare it still reads every field above at the same offset.
    vec4  uv_transform;
    vec4  detail;   // x=tiling multiplier, y=albedo strength, z=normal strength
} mat;
layout(set = 1, binding = 1) uniform sampler2D albedoMap;
layout(set = 1, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 3) uniform sampler2D mrMap;
layout(set = 1, binding = 4) uniform sampler2D aoMap;
layout(set = 1, binding = 5) uniform sampler2D emissiveMap;
layout(set = 1, binding = 6) uniform sampler2D detailAlbedoMap;
layout(set = 1, binding = 7) uniform sampler2D detailNormalMap;

layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outNormalRoughness;
layout(location = 2) out vec4 outAlbedoMetallic;
layout(location = 3) out vec4 outMaterial;

void main() {
    // One transform, applied once, used by every sampler below. Doing it per
    // sampler is how one map ends up untransformed and the surface looks
    // subtly wrong in a way nobody can point at.
    vec2 uv = inUV * mat.uv_transform.xy + mat.uv_transform.zw;

    // Detail maps: the same surface at a much higher frequency, blended over
    // the base so close-up detail exists without authoring the base at a
    // density the whole surface cannot afford.
    //
    // NO DISTANCE FADE. Detail is meant for near surfaces and ideally fades out
    // with range, but that needs a camera position, and the G-buffer vertex
    // push block is already exactly 128 bytes -- Vulkan's guaranteed minimum --
    // so there is nowhere to put one without a per-frame descriptor set the
    // pass does not have. Mip selection carries most of it; the fade is the
    // reason set 0 will eventually exist.
    vec2 duv = uv * mat.detail.x;
    // Detail fades out with range. A detail map is a high-frequency texture; at
    // distance it is minified past the point of adding anything and only
    // contributes aliasing that mips cannot fully remove. Fading it also
    // reclaims the sampling cost where it buys nothing.
    //
    // The range is deliberately fixed rather than authored: it is a property of
    // how far the eye can resolve that frequency, not of the material, and one
    // more slider here would be one more thing to set wrong.
    const float kDetailFadeStart = 12.0;
    const float kDetailFadeEnd   = 40.0;
    float view_dist  = distance(inWorldPos, frame.cameraPos.xyz);
    float detail_fade = 1.0 - smoothstep(kDetailFadeStart, kDetailFadeEnd, view_dist);

    vec4 base = texture(albedoMap, uv) * mat.base_color_factor;
    // A mid-grey detail texel (0.5) doubles to 1.0 and is therefore neutral,
    // which is the convention detail maps are authored to. Strength 0 makes the
    // mix an exact identity, so a material without detail maps is unchanged.
    base.rgb *= mix(vec3(1.0), texture(detailAlbedoMap, duv).rgb * 2.0, mat.detail.y * detail_fade);

    float alpha_cutoff = mat.emissive_factor.a;
    if (alpha_cutoff > 0.0 && base.a < alpha_cutoff) discard;

    vec3 nmap = texture(normalMap, uv).xyz * 2.0 - 1.0;
    // Detail normal, added in TANGENT space before the TBN transform. The xy of
    // a tangent-space normal is its slope, and summing slopes is the standard
    // cheap blend -- correct enough for the small perturbations a detail map
    // carries, and it costs one add rather than a second basis.
    nmap.xy += (texture(detailNormalMap, duv).xy * 2.0 - 1.0) * (mat.detail.z * detail_fade);
    nmap.xy *= mat.normal_scale;
    mat3 TBN = mat3(normalize(inTangent), normalize(inBitangent), normalize(inNormal));
    vec3 N   = normalize(TBN * nmap);

    vec3 mr  = texture(mrMap, uv).rgb;
    float roughness = clamp(mr.g * mat.roughness_factor, 0.04, 1.0);
    float metallic  = clamp(mr.b * mat.metallic_factor, 0.0, 1.0);

    float ao   = texture(aoMap, uv).r * mat.occlusion_strength;
    vec3  emis = mat.emissive_factor.rgb + texture(emissiveMap, uv).rgb;

    outPosition         = vec4(inWorldPos, 1.0);
    outNormalRoughness  = vec4(N, roughness);
    outAlbedoMetallic   = vec4(base.rgb, metallic);
    outMaterial         = vec4(emis, ao);
}
