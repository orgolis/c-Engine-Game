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
layout(set = 1, binding = 3) uniform sampler2D mrMap;
layout(set = 1, binding = 4) uniform sampler2D aoMap;
layout(set = 1, binding = 5) uniform sampler2D emissiveMap;

layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outNormalRoughness;
layout(location = 2) out vec4 outAlbedoMetallic;
layout(location = 3) out vec4 outMaterial;

void main() {
    vec4 base = texture(albedoMap, inUV) * mat.base_color_factor;

    float alpha_cutoff = mat.emissive_factor.a;
    if (alpha_cutoff > 0.0 && base.a < alpha_cutoff) discard;

    vec3 nmap = texture(normalMap, inUV).xyz * 2.0 - 1.0;
    nmap.xy *= mat.normal_scale;
    mat3 TBN = mat3(normalize(inTangent), normalize(inBitangent), normalize(inNormal));
    vec3 N   = normalize(TBN * nmap);

    vec3 mr  = texture(mrMap, inUV).rgb;
    float roughness = clamp(mr.g * mat.roughness_factor, 0.04, 1.0);
    float metallic  = clamp(mr.b * mat.metallic_factor, 0.0, 1.0);

    float ao   = texture(aoMap, inUV).r * mat.occlusion_strength;
    vec3  emis = mat.emissive_factor.rgb + texture(emissiveMap, inUV).rgb;

    outPosition         = vec4(inWorldPos, 1.0);
    outNormalRoughness  = vec4(N, roughness);
    outAlbedoMetallic   = vec4(base.rgb, metallic);
    outMaterial         = vec4(emis, ao);
}
