#version 450
// Accumulation pass for Weighted-Blended Order-Independent Transparency
// (McGuire-Bavoil 2013). Two outputs:
//   location 0 = accum (RGBA16F) : sum of weight * alpha * [rgb, 1]
//   location 1 = reveal (R16F)   : product of (1 - alpha) terms
// Both blended additively / multiplicatively respectively. Composite pass
// resolves them onto HDR.

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;

layout(set = 0, binding = 0) uniform LightEnv {
    vec4 ambient_color;       // rgb = color, w = intensity (legacy ambient)
    vec4 camera_position;     // xyz = camera world pos, w = light count (uint bit-cast in float)
    vec4 ibl_params;          // x = prefilter max mip (0 disables IBL), y = intensity, zw = pad
} env;
layout(set = 0, binding = 1) uniform sampler2DArray shadowMap;
layout(set = 0, binding = 2) uniform samplerCube    pointShadowMap;
struct Light {
    vec4 position;
    vec4 direction;
    vec4 colorRadius;
    vec4 attenuation;
    mat4 shadowMatrix;
    uint shadowMapIndex;
    uint castsShadow;
    uint _pad0;
    uint _pad1;
};
layout(set = 0, binding = 3) readonly buffer LightBuffer { Light lights[]; } lightBuffer;

// IBL bindings — same data as the deferred lighting pass uses. Baked once
// at startup by VulkanEnvironmentMap. Skipped at runtime if ibl_params.x
// (the prefilter mip count) is 0, which preserves the legacy constant-
// ambient path for scenes that don't ship an env map.
layout(set = 0, binding = 4) uniform samplerCube iblIrradiance;
layout(set = 0, binding = 5) uniform samplerCube iblPrefilter;
layout(set = 0, binding = 6) uniform sampler2D   iblBrdfLut;

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
} mat;
layout(set = 1, binding = 1) uniform sampler2D albedoMap;
layout(set = 1, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 3) uniform sampler2D mrMap;
layout(set = 1, binding = 4) uniform sampler2D aoMap;
layout(set = 1, binding = 5) uniform sampler2D emissiveMap;

layout(location = 0) out vec4 outAccum;
layout(location = 1) out float outReveal;

const float PI = 3.14159265359;

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    vec3 r = max(vec3(1.0 - roughness), F0);
    return F0 + (r - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
vec3 computeIBL(vec3 N, vec3 V, vec3 albedo, float metallic, float roughness) {
    if (env.ibl_params.x <= 0.0) return vec3(0.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float NdotV = max(dot(N, V), 0.0);
    vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    vec3 irradiance = texture(iblIrradiance, N).rgb;
    vec3 diffuse    = irradiance * albedo;
    vec3 R = reflect(-V, N);
    float lod = roughness * env.ibl_params.x;
    vec3 prefiltered = textureLod(iblPrefilter, R, lod).rgb;
    vec2 brdf = texture(iblBrdfLut, vec2(NdotV, roughness)).rg;
    vec3 specular = prefiltered * (F * brdf.x + brdf.y);
    return kD * diffuse + specular;
}
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float d = (NdotH2 * (a2 - 1.0) + 1.0);
    d = PI * d * d;
    return a2 / max(d, 1e-6);
}
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness) *
           GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}
float PCF(sampler2DArray sm, vec3 coords, float bias) {
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(sm, 0).xy);
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(sm, coords + vec3(vec2(x, y) * texelSize, 0.0)).r;
            shadow += (coords.z - bias) > pcfDepth ? 0.0 : 1.0;
        }
    return shadow / 9.0;
}
float directionalShadow(vec3 wp, vec3 n, Light L) {
    if (L.castsShadow == 0u) return 1.0;
    vec4 ls = L.shadowMatrix * vec4(wp, 1.0);
    vec3 p  = ls.xyz / max(ls.w, 1e-4);
    p.xy = p.xy * 0.5 + 0.5;
    if (p.z > 1.0) return 1.0;
    float bias = max(0.05 * (1.0 - dot(n, -L.direction.xyz)), 0.005);
    return PCF(shadowMap, vec3(p.xy, float(L.shadowMapIndex)), bias);
}
float pointShadow(vec3 wp, Light L) {
    if (L.castsShadow == 0u) return 1.0;
    vec3 ftl = wp - L.position.xyz;
    float closest = texture(pointShadowMap, ftl).r * L.colorRadius.w;
    return length(ftl) - 0.005 > closest ? 0.0 : 1.0;
}
vec3 evaluateLight(Light L, vec3 N, vec3 V, vec3 wp,
                   vec3 albedo, float roughness, float metallic) {
    vec3 lightDir; float dist = 1.0; float atten = 1.0;
    if (L.position.w == 0.0) {
        lightDir = normalize(-L.direction.xyz);
    } else {
        vec3 tl = L.position.xyz - wp;
        dist = length(tl);
        lightDir = tl / max(dist, 1e-4);
        atten = 1.0 / max(L.attenuation.x + L.attenuation.y * dist +
                          L.attenuation.z * dist * dist, 1e-4);
    }
    vec3 H = normalize(V + lightDir);
    float NdotL = max(dot(N, lightDir), 0.0);
    float NdotV = max(dot(N, V),         0.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F  = fresnelSchlick(max(dot(H, V), 0.0), F0);
    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, lightDir, roughness);
    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    vec3 num = NDF * G * F;
    float den = 4.0 * NdotV * NdotL + 1e-4;
    vec3 spec = num / den;
    vec3 rad = L.colorRadius.xyz * L.direction.w * atten;
    float sh = (L.position.w == 1.0) ? pointShadow(wp, L) : directionalShadow(wp, N, L);
    return (kD * albedo / PI + spec) * rad * NdotL * sh;
}

void main() {
    // Same UV transform the G-buffer uses. A cutout material with a UV
    // transform would otherwise be alpha-tested at untransformed coordinates
    // here, so its shadow would be cut in a different shape than the object.
    vec2 uv = inUV * mat.uv_transform.xy + mat.uv_transform.zw;
    vec4 base = texture(albedoMap, uv) * mat.base_color_factor;
    vec3 nmap = texture(normalMap, uv).xyz * 2.0 - 1.0;
    nmap.xy *= mat.normal_scale;
    mat3 TBN = mat3(normalize(inTangent), normalize(inBitangent), normalize(inNormal));
    vec3 N = normalize(TBN * nmap);
    vec3 mr = texture(mrMap, uv).rgb;
    float roughness = clamp(mr.g * mat.roughness_factor, 0.04, 1.0);
    float metallic  = clamp(mr.b * mat.metallic_factor, 0.0,  1.0);
    float ao   = texture(aoMap, uv).r * mat.occlusion_strength;
    vec3  emis = mat.emissive_factor.rgb + texture(emissiveMap, uv).rgb;
    vec3 V = normalize(env.camera_position.xyz - inWorldPos);

    vec3 direct = vec3(0.0);
    uint count = uint(env.camera_position.w);
    for (uint i = 0u; i < count; ++i)
        direct += evaluateLight(lightBuffer.lights[i], N, V, inWorldPos,
                                base.rgb, roughness, metallic);
    vec3 ambient_const = env.ambient_color.xyz * env.ambient_color.w * base.rgb;
    vec3 ambient_ibl   = computeIBL(N, V, base.rgb, metallic, roughness);
    vec3 ambient = mix(ambient_const, ambient_ibl, env.ibl_params.y);
    vec3 lit = (ambient + direct) * ao + emis;

    float alpha = base.a;
    // Bavoil's depth-weight: emphasises near fragments, de-emphasises far.
    // gl_FragCoord.z is in NDC [0,1]. The classic weight function:
    //   w = clamp(0.03 / (1e-5 + (depth)^4), 1e-2, 3e3)
    // Tweaked here to use a softer power for stability at large depths.
    float depth = gl_FragCoord.z;
    float weight = clamp(0.03 / (1e-5 + pow(depth, 4.0)), 1e-2, 3e3);

    outAccum  = vec4(lit * alpha, alpha) * weight;
    outReveal = alpha;
}
