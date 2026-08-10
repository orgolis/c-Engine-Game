// Extracted from vulkan_shadow_map.cpp (inline R"GLSL" literal).
// Verified: this compiles to SPIR-V semantically identical to the kShadowCasterFragSpv
// array that ships in the generated header, so moving it here changed
// nothing. Edit HERE; `gws shaders` compiles it for immediate iteration,
// and tools/spv_to_header.py regenerates the baked header for shipping.
#version 450
layout(location = 0) in vec2 inUV;
layout(set = 0, binding = 0) uniform MaterialUBO {
    vec4  base_color_factor;
    float metallic_factor;
    float roughness_factor;
    float occlusion_strength;
    float normal_scale;
    vec4  emissive_factor;
} mat;
layout(set = 0, binding = 1) uniform sampler2D albedoMap;
void main() {
    float alpha   = texture(albedoMap, inUV).a * mat.base_color_factor.a;
    float cutoff  = mat.emissive_factor.a;
    if (cutoff > 0.0 && alpha < cutoff) discard;
}
