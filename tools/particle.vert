#version 450
// Particle billboard vertex stage (3.9).
// Billboards arrive already in world space with their colour baked in, so this
// is a transform and nothing else -- the CPU simulation owns particle
// behaviour, and duplicating any of it here would give two places to change.
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;

layout(push_constant) uniform Push { mat4 view_proj; } pc;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

void main() {
    v_uv    = in_uv;
    v_color = in_color;
    gl_Position = pc.view_proj * vec4(in_pos, 1.0);
}
