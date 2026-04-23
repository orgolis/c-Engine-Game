#version 450

// Lighting Pass Fragment Shader
// Reads G-Buffer and performs lighting calculations

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

// G-Buffer samplers
layout(set = 0, binding = 0) uniform sampler2D positionTex;
layout(set = 0, binding = 1) uniform sampler2D normalTex;
layout(set = 0, binding = 2) uniform sampler2D albedoTex;
layout(set = 0, binding = 3) uniform sampler2D materialTex;
layout(set = 0, binding = 4) uniform sampler2D depthTex;

// Shadow maps
layout(set = 0, binding = 5) uniform sampler2DArray shadowMap;
layout(set = 0, binding = 6) uniform samplerCube pointShadowMap;

// Light data
struct Light {
    vec4 position;          // xyz = position, w = type (0=directional, 1=point, 2=spot)
    vec4 direction;         // xyz = direction, w = intensity
    vec4 colorRadius;       // xyz = color, w = radius (for point lights)
    vec4 attenuation;       // x = constant, y = linear, z = quadratic, w = spot_angle
    mat4 shadowMatrix;      // For shadow mapping
    uint shadowMapIndex;
    uint castsShadow;
    uint _pad0;
};

layout(set = 0, binding = 7) uniform LightBuffer {
    Light lights[128];
    uint lightCount;
} lightData;

layout(push_constant) uniform Constants {
    mat4 viewInv;
    mat4 projInv;
    vec3 cameraPos;
    float ambientIntensity;
    vec3 ambientColor;
    float time;
} pc;

// Constants
const float PI = 3.14159265359;

// PBR Functions

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// GGX Normal Distribution Function
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return nom / denom;
}

// Geometry Function (Schlick-GGX)
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

// Percentage-Closer Filtering for shadows
float PCF(sampler2DArray shadowSampler, vec3 coords, float bias) {
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowSampler, 0).xy;
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(shadowSampler, coords + vec3(x * texelSize, y * texelSize, 0.0)).r;
            shadow += (coords.z - bias) > pcfDepth ? 0.0 : 1.0;
        }
    }
    return shadow / 9.0;
}

// Calculate shadow factor for directional light (cascaded)
float calculateDirectionalShadow(vec3 worldPos, vec3 normal, Light light, uint cascadeIndex) {
    if (light.castsShadow == 0) return 1.0;
    
    vec4 fragPosLightSpace = light.shadowMatrix * vec4(worldPos, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    
    if(projCoords.z > 1.0) return 1.0;
    
    float bias = max(0.05 * (1.0 - dot(normal, -light.direction.xyz)), 0.005);
    float shadow = PCF(shadowMap, vec3(projCoords.xy, float(cascadeIndex)), bias);
    
    return shadow;
}

// Calculate point light shadow
float calculatePointShadow(vec3 worldPos, Light light) {
    if (light.castsShadow == 0) return 1.0;
    
    vec3 fragToLight = worldPos - light.position.xyz;
    float closestDepth = texture(pointShadowMap, fragToLight).r * light.colorRadius.w;
    float currentDepth = length(fragToLight);
    float shadow = currentDepth - 0.005 > closestDepth ? 0.0 : 1.0;
    
    return shadow;
}

// Main lighting calculation
vec3 calculateLight(Light light, vec3 normal, vec3 viewDir, vec3 worldPos, vec3 albedo, float roughness, float metallic) {
    vec3 lightDir = normalize(light.direction.xyz);
    vec3 halfDir = normalize(viewDir + lightDir);
    
    float distance = 1.0;
    float attenuation = 1.0;
    
    // Calculate attenuation based on light type
    if (light.position.w == 1.0) { // Point light
        distance = length(light.position.xyz - worldPos);
        attenuation = 1.0 / (light.attenuation.x + 
                           light.attenuation.y * distance + 
                           light.attenuation.z * distance * distance);
    }
    
    // Calculate angles
    float NdotL = max(dot(normal, lightDir), 0.0);
    float NdotV = max(dot(normal, viewDir), 0.0);
    
    // PBR calculations
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = fresnelSchlick(max(dot(halfDir, viewDir), 0.0), F0);
    
    float NDF = DistributionGGX(normal, halfDir, roughness);
    float G = GeometrySmith(normal, viewDir, lightDir, roughness);
    
    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    
    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    vec3 specular = numerator / denominator;
    
    vec3 radiance = light.colorRadius.xyz * light.direction.w * attenuation;
    
    // Shadow calculation
    float shadow = 1.0;
    if (light.position.w == 1.0) {
        shadow = calculatePointShadow(worldPos, light);
    } else {
        shadow = calculateDirectionalShadow(worldPos, normal, light, light.shadowMapIndex);
    }
    
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL * shadow;
    
    return Lo;
}

void main() {
    // Sample G-Buffer
    vec3 position = texture(positionTex, inTexCoord).rgb;
    vec3 normal = normalize(texture(normalTex, inTexCoord).rgb);
    float roughness = texture(normalTex, inTexCoord).a;
    vec3 albedo = texture(albedoTex, inTexCoord).rgb;
    float metallic = texture(albedoTex, inTexCoord).a;
    
    vec3 viewDir = normalize(pc.cameraPos - position);
    
    // Ambient lighting
    vec3 ambient = pc.ambientColor * pc.ambientIntensity * albedo;
    
    // Accumulate lighting from all lights
    vec3 color = ambient;
    for (uint i = 0; i < lightData.lightCount; ++i) {
        color += calculateLight(lightData.lights[i], normal, viewDir, position, albedo, roughness, metallic);
    }
    
    // Tone mapping (Reinhard)
    color = color / (color + vec3(1.0));
    
    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));
    
    outColor = vec4(color, 1.0);
}
