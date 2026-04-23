#version 450

// Geometry Pass Fragment Shader
// Writes to G-Buffer (Position, Normal, Albedo, Material)

layout(location = 0) in VertexOutput {
    vec3 position;
    vec3 normal;
    vec4 tangent;
    vec2 texCoord;
    vec4 color;
} vin;

// G-Buffer attachments
layout(location = 0) out vec4 outPosition;      // World position (RGB) + padding
layout(location = 1) out vec4 outNormal;        // World normal (RGB) + roughness
layout(location = 2) out vec4 outAlbedo;        // Albedo (RGB) + metallic
layout(location = 3) out vec4 outMaterial;      // Material ID (R) + AO (G) + Emission (B) + padding

// Material textures
layout(set = 0, binding = 0) uniform sampler2D albedoMap;
layout(set = 0, binding = 1) uniform sampler2D normalMap;
layout(set = 0, binding = 2) uniform sampler2D roughnessMap;
layout(set = 0, binding = 3) uniform sampler2D metallicMap;
layout(set = 0, binding = 4) uniform sampler2D aoMap;
layout(set = 0, binding = 5) uniform sampler2D emissionMap;

// Material parameters
layout(push_constant) uniform MaterialConstants {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 albedoFactor;
    float roughnessFactor;
    float metallicFactor;
    float aoStrength;
    uint materialID;
} pc;

// Unpack normal from normal map using Unreal convention
vec3 unpackNormal(vec3 packedNormal) {
    return normalize(packedNormal * 2.0 - 1.0);
}

// Calculate TBN matrix for normal mapping
mat3 calculateTBN(vec3 normal, vec4 tangent) {
    vec3 T = normalize(tangent.xyz);
    vec3 N = normalize(normal);
    vec3 B = normalize(cross(N, T)) * tangent.w;
    return mat3(T, B, N);
}

void main() {
    // Sample textures
    vec4 albedoSample = texture(albedoMap, vin.texCoord);
    vec4 normalSample = texture(normalMap, vin.texCoord);
    float roughness = texture(roughnessMap, vin.texCoord).r;
    float metallic = texture(metallicMap, vin.texCoord).r;
    float ao = texture(aoMap, vin.texCoord).r;
    vec3 emission = texture(emissionMap, vin.texCoord).rgb;
    
    // Apply material factors
    vec4 albedo = albedoSample * pc.albedoFactor;
    roughness = roughness * pc.roughnessFactor;
    metallic = metallic * pc.metallicFactor;
    
    // Normal mapping
    vec3 normalMapNormal = unpackNormal(normalSample.rgb);
    mat3 tbn = calculateTBN(vin.normal, vin.tangent);
    vec3 worldNormal = normalize(tbn * normalMapNormal);
    
    // Discard if alpha is too low
    if (albedo.a < 0.5) discard;
    
    // Write G-Buffer
    outPosition = vec4(vin.position, 1.0);
    outNormal = vec4(worldNormal, roughness);
    outAlbedo = vec4(albedo.rgb, metallic);
    outMaterial = vec4(
        float(pc.materialID) / 255.0,  // Material ID normalized
        ao * pc.aoStrength,            // Ambient occlusion
        length(emission),              // Emission intensity
        1.0
    );
}
