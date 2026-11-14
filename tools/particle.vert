// Particle billboard vertex stage (3.9): world-space quads, colour baked in;
// the shape and a seed ride along for the fragment stage.
#version 450
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(location = 3) in vec2 in_extra;   // x: shape (0 soft, 1 glow, 2 smoke), y: a per-particle seed

layout(push_constant) uniform Push { mat4 view_proj; vec4 params; } pc;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
layout(location = 2) out vec2 v_extra;

void main() {
    v_uv    = in_uv;
    v_color = in_color;
    v_extra = in_extra;
    gl_Position = pc.view_proj * vec4(in_pos, 1.0);
}
