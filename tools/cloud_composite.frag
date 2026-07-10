#version 450
// Composite resolved half-res clouds into the HDR target. The cloud texture
// stores premultiplied scattering in .rgb and transmittance in .a, so the
// pipeline blend is configured as:  out = scattering*ONE + hdr*transmittance
// (srcColorBlendFactor = ONE, dstColorBlendFactor = SRC_ALPHA). Bilinear
// upsample of the low-frequency cloud buffer.
layout(location = 0) in vec2 vUv;
layout(set = 0, binding = 0) uniform sampler2D cloudTex;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = texture(cloudTex, vUv); // rgb = scattering, a = transmittance
}
