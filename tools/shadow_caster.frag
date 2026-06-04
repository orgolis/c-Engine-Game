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
    float alpha  = texture(albedoMap, inUV).a * mat.base_color_factor.a;
    float cutoff = mat.emissive_factor.a;
    if (cutoff > 0.0 && alpha < cutoff) discard;
}
