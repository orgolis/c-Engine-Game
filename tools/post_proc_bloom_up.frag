#version 450
// Bloom chain, up: the mip below, blurred with a 3x3 tent and ADDED into this
// one (the pipeline blends ONE, ONE). Going back up the chain, each mip gathers
// everything below it: the result is a wide, soft glow that keeps its shape.
layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D src;
layout(set = 0, binding = 1) readonly buffer ExposureBuffer { float exposure; float p0; float p1; float p2; } autoExposure;
layout(push_constant) uniform P {
    vec2  srcTexel;   // 1 / the lower mip's size
    float radius;     // in its texels: how far the tent reaches
    float weight;     // how much of it to add
    vec4  pad;
} pc;

void main() {
    vec2 t = pc.srcTexel * pc.radius;
    vec2 uv = inTexCoord;
    vec3 s = texture(src, uv).rgb * 4.0;
    s += (texture(src, uv + vec2(t.x, 0.0)).rgb + texture(src, uv - vec2(t.x, 0.0)).rgb +
          texture(src, uv + vec2(0.0, t.y)).rgb + texture(src, uv - vec2(0.0, t.y)).rgb) * 2.0;
    s += texture(src, uv + t).rgb + texture(src, uv - t).rgb +
         texture(src, uv + vec2(t.x, -t.y)).rgb + texture(src, uv + vec2(-t.x, t.y)).rgb;
    outColor = vec4(s * (pc.weight / 16.0) + 0.0 * autoExposure.exposure, 1.0);
}
