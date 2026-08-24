#version 450
layout(location = 0) in vec2 inUV;
layout(set = 0, binding = 0) uniform MaterialUBO {
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
layout(set = 0, binding = 1) uniform sampler2D albedoMap;
void main() {
    // Same UV transform the G-buffer uses. A cutout material with a UV
    // transform would otherwise be alpha-tested at untransformed coordinates
    // here, so its shadow would be cut in a different shape than the object.
    vec2 uv = inUV * mat.uv_transform.xy + mat.uv_transform.zw;
    float alpha  = texture(albedoMap, uv).a * mat.base_color_factor.a;
    float cutoff = mat.emissive_factor.a;
    if (cutoff > 0.0 && alpha < cutoff) discard;
}
