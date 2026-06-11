#version 460
#extension GL_EXT_ray_query : require

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D positionTex;
layout(set = 0, binding = 1) uniform sampler2D normalTex;
layout(set = 0, binding = 2) uniform sampler2D albedoTex;
layout(set = 0, binding = 3) uniform sampler2D materialTex;
layout(set = 0, binding = 4) uniform sampler2D depthTex;
layout(set = 0, binding = 5) uniform sampler2DArray shadowMap;
layout(set = 0, binding = 6) uniform samplerCube   pointShadowMap;

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

layout(set = 0, binding = 7) readonly buffer LightBuffer {
    Light lights[];
} lightBuffer;

layout(set = 0, binding = 8)  uniform samplerCube iblIrradiance;
layout(set = 0, binding = 9)  uniform samplerCube iblPrefilter;
layout(set = 0, binding = 10) uniform sampler2D   iblBrdfLut;
layout(set = 0, binding = 11) uniform samplerCube envCubemap;
layout(set = 0, binding = 12) uniform sampler2D   ssaoTex;
// Scene TLAS — populated by VulkanRtScene each frame. Only sampled when
// pc.rt_shadow_enabled is non-zero; descriptor is allowed to be unbound
// otherwise (controlled by C++ side via set_tlas).
layout(set = 0, binding = 13) uniform accelerationStructureEXT scene_tlas;

layout(push_constant) uniform Constants {
    vec3  cameraPos;
    float ambientIntensity;
    vec3  ambientColor;
    uint  lightCount;
    float iblPrefilterMips;
    float iblIntensity;
    float rtShadowEnabled; // 1.0 → use ray-query shadow path
    float rtAoEnabled;     // 1.0 → use ray-query AO instead of SSAO texture
    mat4  invViewProj;
} pc;

const float PI = 3.14159265359;

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    vec3 r = max(vec3(1.0 - roughness), F0);
    return F0 + (r - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 computeIBL(vec3 N, vec3 V, vec3 albedo, float metallic, float roughness) {
    if (pc.iblPrefilterMips <= 0.0) return vec3(0.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float NdotV = max(dot(N, V), 0.0);
    vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);
    vec3 irradiance = texture(iblIrradiance, N).rgb;
    vec3 diffuse    = irradiance * albedo;
    vec3 R = reflect(-V, N);
    float lod = roughness * pc.iblPrefilterMips;
    vec3 prefiltered = textureLod(iblPrefilter, R, lod).rgb;
    vec2 brdf = texture(iblBrdfLut, vec2(NdotV, roughness)).rg;
    vec3 specular = prefiltered * (F * brdf.x + brdf.y);
    // Strongly damp IBL specular for non-metals: the Karis split-sum
    // approximation's brdf.y term keeps a non-trivial bias even at high
    // roughness, which makes every dielectric surface look subtly mirrored.
    // Real dielectric SSR fills this in from screen-space hits where
    // possible; here we just attenuate the IBL fallback so the editor
    // matches user expectation. metallic²-based scale: 0 at metallic=0,
    // 1 at metallic=1, with a small dielectric base so glossy plastics
    // still pick up some env tint.
    float spec_gate = mix(0.03, 1.0, metallic * metallic);
    // Hand smooth surfaces off to SSR: weight IBL specular by roughness so
    // mirror-like surfaces get ~0 IBL specular (SSR provides their sharp
    // reflection, including a sky fallback on ray miss) while rough
    // surfaces keep the blurry prefiltered-sky IBL (SSR is gated off for
    // them). This stops IBL and SSR double-counting the sky reflection,
    // which made the skybox overwhelm object reflections on metals.
    spec_gate *= smoothstep(0.25, 0.7, roughness);
    specular *= spec_gate;
    return kD * diffuse + specular;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / max(denom, 1e-6);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) *
           GeometrySchlickGGX(NdotL, roughness);
}

float PCF(sampler2DArray sm, vec2 uv, float layer, float expected_depth, float bias) {
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(sm, 0).xy);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(sm, vec3(uv + vec2(x, y) * texelSize, layer)).r;
            shadow += (expected_depth - bias) > pcfDepth ? 0.0 : 1.0;
        }
    }
    return shadow / 9.0;
}

// Trace a single occlusion ray. Returns 1.0 if the ray reaches `t_max`
// without hitting anything (lit), 0.0 if it hits geometry first (shadowed).
// `worldPos` is offset by `+normal * 0.01` before tracing to avoid the
// surface self-intersecting itself at t=0.
float rayQueryShadow(vec3 worldPos, vec3 normal, vec3 ray_dir, float t_max) {
    vec3 origin = worldPos + normal * 0.01;
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, scene_tlas,
        gl_RayFlagsOpaqueEXT |
        gl_RayFlagsTerminateOnFirstHitEXT |
        gl_RayFlagsSkipClosestHitShaderEXT,
        0xFF, origin, 0.001, ray_dir, t_max);
    while (rayQueryProceedEXT(rq)) {}
    return (rayQueryGetIntersectionTypeEXT(rq, true) ==
            gl_RayQueryCommittedIntersectionNoneEXT) ? 1.0 : 0.0;
}

// Cheap hash for per-pixel ray jitter (decorrelates the AO sample set so
// the low ray count doesn't band).
float aoHash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Ray-traced ambient occlusion. Casts AO_SAMPLES short rays over the
// cosine-weighted hemisphere around `N` and returns the visibility
// fraction (1 = fully open to the environment, 0 = fully enclosed).
// Unlike SSAO this sees real geometry beyond the screen, so a sealed
// room reads as genuinely dark even when the occluders aren't on screen.
float rayQueryAO(vec3 worldPos, vec3 N, vec2 px) {
    const int   AO_SAMPLES = 12;
    // Long radius so a sealed room reads as *enclosed* (every ray hits a
    // wall/ceiling within range → AO ~0 → the IBL/ambient sky light is
    // killed and the room goes genuinely dark). A short radius only finds
    // contact crevices and leaves big enclosed rooms fully lit.
    const float AO_RADIUS  = 30.0;
    // Build a tangent basis around N.
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 T  = normalize(cross(up, N));
    vec3 B  = cross(N, T);

    float rnd = aoHash(px);
    vec3  origin = worldPos + N * 0.02;
    float occluded = 0.0;
    for (int i = 0; i < AO_SAMPLES; ++i) {
        // Cosine-weighted hemisphere sample via concentric-disk mapping.
        float u1 = fract(rnd + float(i) * 0.6180339887);
        float u2 = fract(rnd * 1.3247179572 + float(i) * 0.7548776662);
        float r   = sqrt(u1);
        float phi = 6.2831853 * u2;
        vec3 dir_t = vec3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - u1)));
        vec3 dir   = normalize(dir_t.x * T + dir_t.y * B + dir_t.z * N);

        rayQueryEXT rq;
        rayQueryInitializeEXT(rq, scene_tlas,
            gl_RayFlagsOpaqueEXT |
            gl_RayFlagsTerminateOnFirstHitEXT |
            gl_RayFlagsSkipClosestHitShaderEXT,
            0xFF, origin, 0.01, dir, AO_RADIUS);
        while (rayQueryProceedEXT(rq)) {}
        if (rayQueryGetIntersectionTypeEXT(rq, true) !=
            gl_RayQueryCommittedIntersectionNoneEXT) {
            occluded += 1.0;
        }
    }
    return 1.0 - (occluded / float(AO_SAMPLES));
}

float directionalShadow(vec3 worldPos, vec3 normal, Light L) {
    if (L.castsShadow == 0u) return 1.0;
    if (pc.rtShadowEnabled > 0.5) {
        // Sun direction is -L.direction (light direction is "from" the
        // sun toward the surface; the ray to the sun goes the other way).
        // 1000 is well past any plausible scene extent.
        return rayQueryShadow(worldPos, normal, normalize(-L.direction.xyz), 1000.0);
    }
    vec4 fragPosLS = L.shadowMatrix * vec4(worldPos, 1.0);
    vec3 proj = fragPosLS.xyz / max(fragPosLS.w, 1e-4);
    proj.xy = proj.xy * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.z < 0.0 ||
        proj.x < 0.0 || proj.x > 1.0 ||
        proj.y < 0.0 || proj.y > 1.0) return 1.0;
    float bias = max(0.05 * (1.0 - dot(normal, -L.direction.xyz)), 0.005);
    return PCF(shadowMap, proj.xy, float(L.shadowMapIndex), proj.z, bias);
}

float pointShadow(vec3 worldPos, vec3 normal, Light L) {
    if (L.castsShadow == 0u) return 1.0;
    if (pc.rtShadowEnabled > 0.5) {
        vec3 to_light = L.position.xyz - worldPos;
        float dist = length(to_light);
        if (dist < 1e-4) return 1.0;
        return rayQueryShadow(worldPos, normal, to_light / dist, dist - 0.005);
    }
    vec3 fragToLight = worldPos - L.position.xyz;
    float closest = texture(pointShadowMap, fragToLight).r * L.colorRadius.w;
    return length(fragToLight) - 0.005 > closest ? 0.0 : 1.0;
}

vec3 evaluateLight(Light L, vec3 N, vec3 V, vec3 worldPos,
                   vec3 albedo, float roughness, float metallic) {
    vec3 lightDir;
    float distance    = 1.0;
    float attenuation = 1.0;
    if (L.position.w == 0.0) {
        lightDir = normalize(-L.direction.xyz);
    } else {
        vec3 toLight = L.position.xyz - worldPos;
        distance     = length(toLight);
        lightDir     = toLight / max(distance, 1e-4);
        attenuation  = 1.0 / max(L.attenuation.x +
                                 L.attenuation.y * distance +
                                 L.attenuation.z * distance * distance, 1e-4);
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
    vec3 numerator   = NDF * G * F;
    float denominator = 4.0 * NdotV * NdotL + 1e-4;
    vec3 specular = numerator / denominator;
    vec3 radiance = L.colorRadius.xyz * L.direction.w * attenuation;
    float shadow  = (L.position.w == 1.0)
                        ? pointShadow(worldPos, N, L)
                        : directionalShadow(worldPos, N, L);
    return (kD * albedo / PI + specular) * radiance * NdotL * shadow;
}

void main() {
    float gbufferDepth = texture(depthTex, inTexCoord).r;
    if (gbufferDepth >= 1.0 - 1e-5) {
        // invViewProj here is the inverse of (proj * view-WITHOUT-translation),
        // so unprojecting the NDC gives the world-space ray DIRECTION
        // directly — no `world - cameraPos` subtraction. That subtraction
        // loses float precision when the camera is far from the world
        // origin, which made the sampled cubemap direction (and thus the
        // sky colour) drift toward grey with position.
        vec2 ndc = inTexCoord * 2.0 - 1.0;
        vec4 dir4 = pc.invViewProj * vec4(ndc, 1.0, 1.0);
        vec3 viewDir = normalize(dir4.xyz / dir4.w);
        outColor = vec4(texture(envCubemap, viewDir).rgb, 1.0);
        return;
    }

    vec4 normalSample   = texture(normalTex,   inTexCoord);
    vec4 albedoSample   = texture(albedoTex,   inTexCoord);
    vec4 materialSample = texture(materialTex, inTexCoord);
    vec3 worldPos = texture(positionTex, inTexCoord).xyz;
    vec3 N        = normalize(normalSample.xyz);
    float roughness = normalSample.a;
    vec3 albedo   = albedoSample.rgb;
    float metallic = albedoSample.a;
    vec3  emissive   = materialSample.rgb;
    float occlusion  = materialSample.a;

    vec3 V = normalize(pc.cameraPos - worldPos);

    vec3 ambient_const = pc.ambientColor * pc.ambientIntensity * albedo;
    vec3 ambient_ibl   = computeIBL(N, V, albedo, metallic, roughness);
    vec3 ambient = mix(ambient_const, ambient_ibl, pc.iblIntensity);
    // Ambient occlusion comes from the SSAO pass texture. When the device
    // supports RT the SSAO pass computes long-range ray-traced AO (sealed
    // rooms go dark) and bilateral-blurs it — so this single sample is
    // already denoised. Without RT it's the screen-space SSAO. Either way
    // the lighting shader just samples the result (no noisy inline AO).
    float ao = texture(ssaoTex, inTexCoord).r;
    ambient *= ao;

    vec3 direct  = vec3(0.0);
    for (uint i = 0u; i < pc.lightCount; ++i) {
        direct += evaluateLight(lightBuffer.lights[i], N, V, worldPos, albedo, roughness, metallic);
    }
    vec3 color = (ambient + direct) * occlusion + emissive;

    outColor = vec4(color, 1.0);
}
