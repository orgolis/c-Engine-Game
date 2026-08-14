#version 450
layout(location = 0) in vec2 gs_uv;
layout(location = 1) in vec4 gs_vertex_color;
layout(set = 0, binding = 0) uniform sampler2D gs_tex0;
layout(push_constant) uniform P { float gs_time_v; } pc;
layout(location = 0) out vec4 out_color;
void main() {
    float gs_time = pc.gs_time_v;
    vec3  gs_out_base_color;
    float gs_out_metallic;
    float gs_out_roughness;
    vec3  gs_out_emissive;
    float gs_out_alpha;
    vec2 n1 = gs_uv;
    vec4 n2 = texture(gs_tex0, n1);
    vec4 n3 = vec4(1.0, 1.0, 1.0, 1.0);
    vec4 n4 = n2 * n3;
    float n5 = (n2).w;
    float n6 = 1.0 - n5;
    float n7 = gs_time;
    float n8 = clamp(n7, 0.0, 1.0);
    vec3 n9 = vec3(n8, 0.0, 0.0);
    gs_out_base_color = (n4).xyz;
    gs_out_metallic = 0.0;
    gs_out_roughness = n6;
    gs_out_emissive = n9;
    gs_out_alpha = 1.0;
    out_color = vec4(gs_out_base_color + gs_out_emissive, gs_out_alpha);
    out_color.rgb *= (1.0 - gs_out_roughness * 0.0 + gs_out_metallic * 0.0);
}
