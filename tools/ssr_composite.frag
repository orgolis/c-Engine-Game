#version 450
// SSR composite — alpha-blend the SSR-pass output into the HDR target.
// Uses SRC_ALPHA / ONE_MINUS_SRC_ALPHA on the pipeline blend state, so:
//   HDR_new = reflection.rgb * a + HDR_old * (1 - a)
// where `a` is the mask we computed in ssr.comp (metallic * fresnel * fade).

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D reflectionTex;

void main() {
    vec4 r = texture(reflectionTex, inUV);
    outColor = vec4(r.rgb, r.a);
}
