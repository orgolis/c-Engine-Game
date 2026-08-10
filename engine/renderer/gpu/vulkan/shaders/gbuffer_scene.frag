// Extracted from vulkan_g_buffer.cpp (inline R"GLSL" literal).
// Verified: this compiles to SPIR-V semantically identical to the kGbufferSceneFragSpv
// array that ships in the generated header, so moving it here changed
// nothing. Edit HERE; `gws shaders` compiles it for immediate iteration,
// and tools/spv_to_header.py regenerates the baked header for shipping.
#version 450
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;

layout(set = 1, binding = 0) uniform MaterialUBO {
    vec4  base_color_factor;
    float metallic_factor;
    float roughness_factor;
    float occlusion_strength;
    float normal_scale;
    vec4  emissive_factor;
} mat;
layout(set = 1, binding = 1) uniform sampler2D albedoMap;
layout(set = 1, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 3) uniform sampler2D mrMap; // green=R, blue=M (glTF)
layout(set = 1, binding = 4) uniform sampler2D aoMap;
layout(set = 1, binding = 5) uniform sampler2D emissiveMap;

layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outNormalRoughness;
layout(location = 2) out vec4 outAlbedoMetallic;
layout(location = 3) out vec4 outMaterial;

void main() {
    vec4 base = texture(albedoMap, inUV) * mat.base_color_factor;

    // Alpha cutout. emissive_factor.a doubles as alpha_cutoff (set on the
    // CPU side via EntityMaterialCache when the MeshRendererComponent has
    // its "Transparent" flag on). Cutoff == 0 means "opaque, no discard"
    // so opaque materials are unaffected.
    float alpha_cutoff = mat.emissive_factor.a;
    if (alpha_cutoff > 0.0 && base.a < alpha_cutoff) discard;

    vec3 nmap = texture(normalMap, inUV).xyz * 2.0 - 1.0;
    nmap.xy *= mat.normal_scale;
    mat3 TBN = mat3(normalize(inTangent), normalize(inBitangent), normalize(inNormal));
    vec3 N   = normalize(TBN * nmap);

    vec3 mr  = texture(mrMap, inUV).rgb;
    float roughness = clamp(mr.g * mat.roughness_factor, 0.04, 1.0);
    float metallic  = clamp(mr.b * mat.metallic_factor, 0.0, 1.0);

    // AO and emissive use additive / multiplier combine so the per-material
    // factors are visible without an asset texture (default textures are
    // white / black respectively, which would otherwise zero either signal
    // out via the strict glTF combine).
    float ao   = texture(aoMap, inUV).r * mat.occlusion_strength;
    vec3  emis = mat.emissive_factor.rgb + texture(emissiveMap, inUV).rgb;

    outPosition         = vec4(inWorldPos, 1.0);
    outNormalRoughness  = vec4(N, roughness);
    outAlbedoMetallic   = vec4(base.rgb, metallic);
    // Pack emissive RGB + AO into the 4th attachment. Previously this
    // stored emissive *luminance* only, so coloured glows were impossible
    // and the lighting pass never sampled it anyway.
    outMaterial         = vec4(emis, ao);
}
