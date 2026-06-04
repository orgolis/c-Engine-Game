#version 450

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

layout(push_constant) uniform Constants {
    vec3  cameraPos;
    float ambientIntensity;
    vec3  ambientColor;
    uint  lightCount;
} pc;

const float PI = 3.14159265359;

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

float PCF(sampler2DArray sm, vec3 coords, float bias) {
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(sm, 0).xy);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(sm, coords + vec3(vec2(x, y) * texelSize, 0.0)).r;
            shadow += (coords.z - bias) > pcfDepth ? 0.0 : 1.0;
        }
    }
    return shadow / 9.0;
}

float directionalShadow(vec3 worldPos, vec3 normal, Light L) {
    if (L.castsShadow == 0u) return 1.0;
    vec4 fragPosLS = L.shadowMatrix * vec4(worldPos, 1.0);
    vec3 proj = fragPosLS.xyz / max(fragPosLS.w, 1e-4);
    proj.xy = proj.xy * 0.5 + 0.5;
    if (proj.z > 1.0) return 1.0;
    float bias = max(0.05 * (1.0 - dot(normal, -L.direction.xyz)), 0.005);
    return PCF(shadowMap, vec3(proj.xy, float(L.shadowMapIndex)), bias);
}

float pointShadow(vec3 worldPos, Light L) {
    if (L.castsShadow == 0u) return 1.0;
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
                        ? pointShadow(worldPos, L)
                        : directionalShadow(worldPos, N, L);

    return (kD * albedo / PI + specular) * radiance * NdotL * shadow;
}

void main() {
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

    vec3 ambient = pc.ambientColor * pc.ambientIntensity * albedo;
    vec3 direct  = vec3(0.0);
    for (uint i = 0u; i < pc.lightCount; ++i) {
        direct += evaluateLight(lightBuffer.lights[i], N, V, worldPos, albedo, roughness, metallic);
    }
    vec3 color = (ambient + direct) * occlusion + emissive;

    outColor = vec4(color, 1.0);
}
