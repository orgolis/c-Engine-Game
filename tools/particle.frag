#version 450
// Particle billboard fragment stage (3.9).
// A soft radial falloff rather than a hard-edged quad. Without it every
// particle is a visible square, which is the single thing that makes a particle
// system look broken rather than stylised.
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 out_color;

void main() {
    vec2  d = v_uv * 2.0 - 1.0;
    float r = dot(d, d);
    if (r > 1.0) discard;                 // outside the disc: no square corners
    float falloff = 1.0 - r;
    falloff *= falloff;                   // squared: a softer core-to-edge ramp
    out_color = vec4(v_color.rgb, v_color.a * falloff);
}
