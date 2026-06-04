#version 450
// Composite pass for Weighted-Blended OIT. Reads accum + revealage and
// blends the weighted average onto the HDR target with srcColor=ONE,
// dstColor=SRC_ALPHA so:
//   HDR_new.rgb = src.rgb + HDR.rgb * src.a
// where src.rgb = (accum.rgb / max(accum.a, eps))
// and   src.a  = revealage    (= product of (1 - alpha) terms)

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0) uniform sampler2D accumTex;
layout(set = 0, binding = 1) uniform sampler2D revealTex;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 accum = texture(accumTex,  inUV);
    float revealage = texture(revealTex, inUV).r;
    // No transparent fragments hit this pixel — let HDR through unchanged.
    if (revealage >= 1.0 - 1e-5) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec3 average = accum.rgb / max(accum.a, 1e-4);
    outColor = vec4(average, revealage);
}
