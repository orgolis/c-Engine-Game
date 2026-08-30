#version 460
#ifdef GWS_RAY_QUERY
#extension GL_EXT_ray_query : require
#endif

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
#ifdef GWS_RAY_QUERY
layout(set = 0, binding = 13) uniform accelerationStructureEXT scene_tlas;
#endif
layout(set = 0, binding = 14) uniform sampler2D cloudShadow; // top-down sun transmittance
layout(set = 0, binding = 15) uniform sampler2D ltc1Tex;     // LTC inverse-M (area lights)
layout(set = 0, binding = 16) uniform sampler2D ltc2Tex;     // LTC GGX norm / fresnel / horizon
layout(set = 0, binding = 17) uniform sampler2D cookieTex;   // spot-light cookie / gobo mask

layout(push_constant) uniform Constants {
    vec3  cameraPos;
    float ambientIntensity;
    vec3  ambientColor;
    uint  lightCount;
    float iblPrefilterMips;
    float iblIntensity;
    // Three booleans packed into one float, because this block is EXACTLY
    // 128 bytes -- Vulkan's guaranteed minimum maxPushConstantsSize -- and
    // growing it would work on this GPU and fail on someone else's. Bit 0 =
    // ray-query shadows, bit 1 = ray-query AO, bit 2 = cloud shadows.
    float flags;
    // How far light spreads into shadow. 0 = the old hard edge; larger values
    // widen the penumbra (shadow-map PCF radius, and the sun's apparent angular
    // size for the ray-query path).
    float shadowSoftness;
    mat4  invViewProj;
    // w carries shadow PERSISTENCE, not a cloud value: how much light survives
    // inside a fully occluded region. It rides here because this block is at
    // Vulkan's 128-byte floor and the cloud enable vacated the slot.
    vec4  cloudParams;     // x=center.x, y=center.z, z=half_extent, w=persistence
} pc;

bool flagSet(int bit) { return (int(pc.flags) & (1 << bit)) != 0; }

// Soft-shadow cone rays, packed into bits 8-11 of the same float.
//
// It cannot be its own push-constant field: this block is EXACTLY 128 bytes,
// Vulkan's guaranteed minimum, so an extra float would work here and fail on a
// device that offers no more. Bits 0-2 are booleans and 8-11 hold a count of
// 1..8, which is all the range this needs.
//
// Why it is worth controlling at all: this is the most expensive number in the
// engine. Eight rays per pixel PER LIGHT at 1080p is ~16.6M ray traversals a
// frame for one light, and the lighting pass measured 10-12 ms on an RTX 3060
// with an 18-draw scene -- 94% of GPU time. Halving the count halves that.
int shadowRayCount() {
    int n = (int(pc.flags) >> 8) & 0xF;
    return clamp(n, 1, 8);
}
#define RT_SHADOWS_ON  flagSet(0)
#define RT_AO_ON       flagSet(1)
#define CLOUD_SHADOW_ON flagSet(2)

// How much sun reaches `worldPos` through the clouds overhead (1 = clear).
float cloudShadowFactor(vec3 worldPos) {
    if (!CLOUD_SHADOW_ON) return 1.0;
    vec2 uv = (worldPos.xz - pc.cloudParams.xy) / (2.0 * pc.cloudParams.z) + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
    return clamp(texture(cloudShadow, uv).r, 0.0, 1.0);
}

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

// Percentage-closer filtering. The kernel RADIUS is what produces a penumbra:
// a fixed 3x3 at one texel spans three texels of gradient, which on a 2048 map
// covering a large scene collapses to a hard edge on screen. Widening the
// radius spreads the transition over more texels; the sample count grows with
// it so the gradient stays smooth instead of banding into rings.
float PCF(sampler2DArray sm, vec2 uv, float layer, float expected_depth, float bias) {
    vec2 texelSize = 1.0 / vec2(textureSize(sm, 0).xy);
    float radius = 1.0 + pc.shadowSoftness * 6.0;      // texels
    int   steps  = int(clamp(radius, 1.0, 4.0));       // 3x3 .. 9x9 taps
    float shadow = 0.0;
    float taken  = 0.0;
    for (int x = -steps; x <= steps; ++x) {
        for (int y = -steps; y <= steps; ++y) {
            vec2 o = vec2(x, y) * (radius / float(steps)) * texelSize;
            float pcfDepth = texture(sm, vec3(uv + o, layer)).r;
            shadow += (expected_depth - bias) > pcfDepth ? 0.0 : 1.0;
            taken  += 1.0;
        }
    }
    return shadow / taken;
}

// Trace a single occlusion ray. Returns 1.0 if the ray reaches `t_max`
// without hitting anything (lit), 0.0 if it hits geometry first (shadowed).
// `worldPos` is offset by `+normal * 0.01` before tracing to avoid the
// surface self-intersecting itself at t=0.
// Forward declaration: the cone sampler below jitters with it, and it is
// defined further down beside the AO code that also uses it.
float aoHash(vec2 p);

#ifdef GWS_RAY_QUERY
// How far to lift a shadow ray off the surface it starts on.
//
// A fixed 1 cm was not enough and produced the terrain artefact this replaced:
// broad, slope-following patches of false shadow. Two things defeat a constant
// offset. Terrain triangles are METRES across, so at a grazing sun angle the
// ray re-enters the very triangle it left; and float precision on a world
// coordinate hundreds of units from the origin is itself coarser than 1 cm, so
// the offset can round away entirely.
//
// So it scales with both. The slope term is tan(angle between the surface and
// the ray) -- grazing rays need the most lift -- and the magnitude term keeps
// the offset above the representable step at this distance from the origin.
float shadowRayEpsilon(vec3 worldPos, vec3 normal, vec3 ray_dir) {
    float ndotl = max(dot(normal, ray_dir), 0.0);
    float slope = sqrt(max(1.0 - ndotl * ndotl, 0.0)) / max(ndotl, 0.05);
    float scale = max(1.0, length(worldPos) * 1e-3);
    return clamp(0.02 * (1.0 + slope) * scale, 0.02, 2.0);
}

float rayQueryShadowOne(vec3 origin, vec3 ray_dir, float t_max) {
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

// A single ray gives a BINARY result -- lit or not -- which is why ray-traced
// shadows had no penumbra at all and a shadow's edge was exactly as dark as its
// centre. Real shadows soften because the sun is a disc, not a point: near the
// edge only part of it is hidden. Spreading a few rays over a cone the width of
// that disc reproduces it, and the fraction that get through IS the gradient.
float rayQueryShadow(vec3 worldPos, vec3 normal, vec3 ray_dir, float t_max) {
    vec3 origin = worldPos + normal * shadowRayEpsilon(worldPos, normal, ray_dir);

    if (pc.shadowSoftness <= 0.001)
        return rayQueryShadowOne(origin, ray_dir, t_max);

    // Cone half-angle. The real sun is ~0.27 degrees; the slider scales up from
    // there so the effect is dialable rather than physically fixed.
    float spread = pc.shadowSoftness * 0.08;
    vec3 up = abs(ray_dir.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tx = normalize(cross(up, ray_dir));
    vec3 ty = cross(ray_dir, tx);

    // Rotated per pixel so the low sample count reads as a soft gradient rather
    // than as a repeating pattern of hard copies.
    float ang = aoHash(gl_FragCoord.xy) * 6.2831853;
    float lit = 0.0;
    const int kConeRays = shadowRayCount();
    for (int i = 0; i < kConeRays; ++i) {
        float t = (float(i) + 0.5) / float(kConeRays);
        float r = sqrt(t) * spread;
        float a = ang + t * 6.2831853 * 2.39996;   // golden-angle spiral
        vec3 d = normalize(ray_dir + tx * (cos(a) * r) + ty * (sin(a) * r));
        lit += rayQueryShadowOne(origin, d, t_max);
    }
    return lit / float(kConeRays);
}
#else
// No ray query on this device: fully lit, so shadowing falls back to
// the shadow map path the caller already blends with.
float rayQueryShadow(vec3 worldPos, vec3 normal, vec3 ray_dir, float t_max) { return 1.0; }
#endif

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
#ifdef GWS_RAY_QUERY
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
#else
// No ray query: no ray-traced occlusion. 1.0 means unoccluded, so SSAO
// remains the only AO term rather than everything going black.
float rayQueryAO(vec3 worldPos, vec3 N, vec2 px) { return 1.0; }
#endif

// Screen-space contact shadows (Stage 3.4): a short world-space march toward
// the light, projected back to screen and tested against the G-buffer position
// buffer. Adds the fine-scale contact darkening cascaded shadow maps miss
// (object bases, crevices). Only used on the NON-RT path — ray-query shadows
// are already contact-accurate. Costs one mat4 inverse per shaded pixel on
// that path (the push-constant block is at the 128-byte limit, so the forward
// view-proj cannot be passed in).
float contactShadow(vec3 worldPos, vec3 N, vec3 Ldir) {
    mat4 viewProj = inverse(pc.invViewProj);
    const int   STEPS    = 8;
    const float MAX_DIST = 0.6;    // march length (m) — contact-scale only
    const float THICK    = 0.35;   // assumed occluder thickness (m)
    for (int i = 1; i <= STEPS; ++i) {
        vec3 p = worldPos + Ldir * (MAX_DIST * float(i) / float(STEPS))
                          + N * 0.02;
        vec4 clip = viewProj * vec4(p, 1.0);
        if (clip.w <= 0.0) break;
        vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
        vec4 sp = texture(positionTex, uv);
        if (sp.w < 0.5) continue;                       // sky pixel
        float sceneDist  = length(sp.xyz - pc.cameraPos);
        float sampleDist = length(p - pc.cameraPos);
        // Occluded when the scene surface is in front of the march point but
        // within a plausible thickness (avoids darkening from far background).
        if (sceneDist < sampleDist - 0.02 && sampleDist - sceneDist < THICK)
            return 0.0;
    }
    return 1.0;
}

float directionalShadow(vec3 worldPos, vec3 normal, Light L) {
    if (L.castsShadow == 0u) return 1.0;
    if (RT_SHADOWS_ON) {
        // Sun direction is -L.direction (light direction is "from" the
        // sun toward the surface; the ray to the sun goes the other way).
        // 1000 is well past any plausible scene extent.
        return rayQueryShadow(worldPos, normal, normalize(-L.direction.xyz), 1000.0);
    }
    vec4 fragPosLS = L.shadowMatrix * vec4(worldPos, 1.0);
    vec3 proj = fragPosLS.xyz / max(fragPosLS.w, 1e-4);
    proj.xy = proj.xy * 0.5 + 0.5;
    float sm = 1.0;
    if (proj.z <= 1.0 && proj.z >= 0.0 &&
        proj.x >= 0.0 && proj.x <= 1.0 &&
        proj.y >= 0.0 && proj.y <= 1.0) {
        float bias = max(0.05 * (1.0 - dot(normal, -L.direction.xyz)), 0.005);
        sm = PCF(shadowMap, proj.xy, float(L.shadowMapIndex), proj.z, bias);
    }
    // Contact shadows refine the mapped result near occluders (non-RT only).
    if (sm > 0.0)
        sm *= contactShadow(worldPos, normal, normalize(-L.direction.xyz));
    return sm;
}

float pointShadow(vec3 worldPos, vec3 normal, Light L) {
    if (L.castsShadow == 0u) return 1.0;
    if (RT_SHADOWS_ON) {
        vec3 to_light = L.position.xyz - worldPos;
        float dist = length(to_light);
        if (dist < 1e-4) return 1.0;
        return rayQueryShadow(worldPos, normal, to_light / dist, dist - 0.005);
    }
    vec3 fragToLight = worldPos - L.position.xyz;
    float closest = texture(pointShadowMap, fragToLight).r * L.colorRadius.w;
    return length(fragToLight) - 0.005 > closest ? 0.0 : 1.0;
}

// Spot-light shadow. On RT GPUs (the primary path here) traces a ray toward the
// spot's position — the correct occlusion test for a positional light (the old
// path reused directionalShadow, which traced a parallel ray and looked wrong).
// Without RT there is no per-spot shadow map, so it returns lit (no shadow).
float spotShadow(vec3 worldPos, vec3 normal, Light L) {
    if (L.castsShadow == 0u) return 1.0;
    if (RT_SHADOWS_ON) {
        vec3 to_light = L.position.xyz - worldPos;
        float dist = length(to_light);
        if (dist < 1e-4) return 1.0;
        return rayQueryShadow(worldPos, normal, to_light / dist, dist - 0.01);
    }
    return 1.0;
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
    // Spot cone falloff (smooth inner→outer). Without this a spot light is
    // omnidirectional like a point. attenuation.w = cos(outer angle);
    // _pad0 of the GLSL struct maps to the cookie flag, _pad1 = cos(inner) bits.
    if (L.position.w == 2.0) {
        float cosA   = dot(-lightDir, normalize(L.direction.xyz));
        float outerC = L.attenuation.w;
        float innerC = uintBitsToFloat(L._pad1);
        float spotF  = clamp((cosA - outerC) / max(innerC - outerC, 1e-4), 0.0, 1.0);
        radiance *= spotF * spotF;
    }
    // Spot cookie/gobo: project the surface through the spot's matrix and
    // modulate the light by the cookie texture. Outside the projection frustum
    // is dark, which also gives the cookie-spot a defined cone shape.
    if (L.position.w == 2.0 && L._pad0 != 0u) {
        vec4 cp = L.shadowMatrix * vec4(worldPos, 1.0);
        if (cp.w > 0.0) {
            vec2 cuv = (cp.xy / cp.w) * 0.5 + 0.5;
            if (cuv.x >= 0.0 && cuv.x <= 1.0 && cuv.y >= 0.0 && cuv.y <= 1.0)
                radiance *= texture(cookieTex, cuv).rgb;
            else
                radiance = vec3(0.0);
        } else {
            radiance = vec3(0.0);
        }
    }
    float shadow;
    if (L.position.w == 1.0)      shadow = pointShadow(worldPos, N, L);
    else if (L.position.w == 2.0) shadow = spotShadow(worldPos, N, L);
    else                          shadow = directionalShadow(worldPos, N, L);
    // The directional (sun) light is additionally occluded by clouds overhead.
    if (L.position.w == 0.0) shadow *= cloudShadowFactor(worldPos);
    // Light persistence: how much of this light still reaches a fully occluded
    // point. Applied AFTER the occlusion tests, so it lifts the umbra without
    // touching the penumbra gradient the softness term produces. Physically
    // this is the light that arrives by bouncing rather than directly -- with a
    // low ambient and none of it, every shadow is a flat black silhouette.
    shadow = mix(pc.cloudParams.w, 1.0, shadow);
    return (kD * albedo / PI + specular) * radiance * NdotL * shadow;
}

// ---------------------------------------------------------------------------
// Rectangular area lights via Linearly Transformed Cosines (Heitz et al.).
// LTC1/LTC2 are the fitted 64x64 lookup tables (ltc1Tex / ltc2Tex). A Light
// with position.w == 3 encodes: center (position.xyz), half-edge vectors
// (direction.xyz = right, attenuation.xyz = up), intensity (direction.w),
// colour (colorRadius.xyz), range (colorRadius.w), two_sided (attenuation.w).
// ---------------------------------------------------------------------------
const float LTC_LUT_SIZE  = 64.0;
const float LTC_LUT_SCALE = (LTC_LUT_SIZE - 1.0) / LTC_LUT_SIZE;
const float LTC_LUT_BIAS  = 0.5 / LTC_LUT_SIZE;

vec3 ltcIntegrateEdgeVec(vec3 v1, vec3 v2) {
    float x = dot(v1, v2);
    float y = abs(x);
    float a = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
    float b = 3.4175940 + (4.1616724 + y) * y;
    float v = a / b;
    float theta_sintheta = (x > 0.0) ? v
                         : 0.5 * inversesqrt(max(1.0 - x * x, 1e-7)) - v;
    return cross(v1, v2) * theta_sintheta;
}

// Integrate the (clipped) rectangle against the cosine lobe transformed by Minv.
float ltcEvaluate(vec3 N, vec3 V, vec3 P, mat3 Minv, vec3 points[4], bool twoSided) {
    vec3 T1 = normalize(V - N * dot(V, N));
    vec3 T2 = cross(N, T1);
    Minv = Minv * transpose(mat3(T1, T2, N));

    vec3 L0 = Minv * (points[0] - P);
    vec3 L1 = Minv * (points[1] - P);
    vec3 L2 = Minv * (points[2] - P);
    vec3 L3 = Minv * (points[3] - P);

    vec3 dir = points[0] - P;
    vec3 lightNormal = cross(points[1] - points[0], points[3] - points[0]);
    bool behind = (dot(dir, lightNormal) < 0.0);

    L0 = normalize(L0); L1 = normalize(L1);
    L2 = normalize(L2); L3 = normalize(L3);

    vec3 vsum = vec3(0.0);
    vsum += ltcIntegrateEdgeVec(L0, L1);
    vsum += ltcIntegrateEdgeVec(L1, L2);
    vsum += ltcIntegrateEdgeVec(L2, L3);
    vsum += ltcIntegrateEdgeVec(L3, L0);

    float len = length(vsum);
    float z   = vsum.z / max(len, 1e-7);
    if (behind) z = -z;

    vec2 uv = vec2(z * 0.5 + 0.5, len);
    uv = uv * LTC_LUT_SCALE + LTC_LUT_BIAS;
    float scale = texture(ltc2Tex, uv).w;

    float sum = len * scale;
    if (!behind && !twoSided) sum = 0.0;
    return sum;
}

vec3 evaluateAreaLight(Light L, vec3 N, vec3 V, vec3 P,
                       vec3 albedo, float roughness, float metallic) {
    vec3  center    = L.position.xyz;
    vec3  right     = L.direction.xyz;
    vec3  up        = L.attenuation.xyz;
    float intensity = L.direction.w;
    vec3  lightCol  = L.colorRadius.xyz;
    float range     = L.colorRadius.w;
    bool  twoSided  = L.attenuation.w > 0.5;

    vec3 points[4];
    points[0] = center - right - up;
    points[1] = center + right - up;
    points[2] = center + right + up;
    points[3] = center - right + up;

    float NdotV = clamp(dot(N, V), 0.0, 1.0);
    vec2  cuv = vec2(roughness, sqrt(clamp(1.0 - NdotV, 0.0, 1.0)));
    cuv = cuv * LTC_LUT_SCALE + LTC_LUT_BIAS;
    vec4 t1 = texture(ltc1Tex, cuv);
    vec4 t2 = texture(ltc2Tex, cuv);

    mat3 Minv = mat3(
        vec3(t1.x, 0.0, t1.y),
        vec3(0.0,  1.0, 0.0),
        vec3(t1.z, 0.0, t1.w));

    float spec = ltcEvaluate(N, V, P, Minv, points, twoSided);
    vec3  F0   = mix(vec3(0.04), albedo, metallic);
    vec3  specCol = (F0 * t2.x + (1.0 - F0) * t2.y) * spec;

    float diff = ltcEvaluate(N, V, P, mat3(1.0), points, twoSided);
    vec3  diffCol = albedo * (1.0 - metallic) * diff;

    vec3 result = lightCol * intensity * (specCol + diffCol) * (1.0 / (2.0 * PI));

    // Soft distance falloff using the configured range.
    if (range > 0.0) {
        float d  = length(center - P);
        float a  = clamp(1.0 - (d / range) * (d / range), 0.0, 1.0);
        result  *= a * a;
    }
    return result;
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
        Light Lt = lightBuffer.lights[i];
        if (Lt.position.w == 3.0)   // area (rect) light → LTC
            direct += evaluateAreaLight(Lt, N, V, worldPos, albedo, roughness, metallic);
        else
            direct += evaluateLight(Lt, N, V, worldPos, albedo, roughness, metallic);
    }
    vec3 color = (ambient + direct) * occlusion + emissive;

    outColor = vec4(color, 1.0);
}
