/**
 * @file ao_blur_params.h
 * @brief The ambient-occlusion technique list, and how hard to denoise each.
 *
 * Deliberately free of any Vulkan dependency so the choice can be checked
 * without a device — the decision below is arithmetic, not graphics, and it
 * was wrong for months precisely because nothing could assert on it.
 */
#pragma once

namespace gws::renderer::gpu {

enum class AoTechnique { SSAO = 0, HBAO, HDAO, GTAO, VXAO, RT };

/// Bilateral-blur kernel for the AO occlusion texture.
struct AoBlurParams {
    int   radius;        ///< kernel half-size in pixels; 2 => 5x5, 3 => 7x7
    float depth_sigma;   ///< depth-difference falloff (NDC units)
    float normal_sigma;  ///< normal-similarity falloff (1 - dot)
};

/// Size the denoise to the noise, which is a property of the technique.
///
/// RT AO is stochastic — Monte-Carlo rays, no denoiser of its own — so it
/// needs a wide kernel and loose sigmas or the grain survives. Every other
/// technique here is analytic; their only noise is a per-pixel hash rotation,
/// which 5x5 clears completely.
///
/// This used to be one constant: the RT values, applied to everything. The
/// kernel is O(radius^2) with three texture fetches per tap, so that made the
/// default path pay 49 taps where 25 would do. Measured on a 2-draw /
/// 734-triangle scene at 1920x1032, the blur cost 14-18 ms against GTAO's
/// 8.8-9.8 ms — the denoise cost about twice the AO it was cleaning, removing
/// noise that technique never produced. Halving the kernel took the blur to
/// 4.7-8.9 ms and the frame from 44 ms to 31 ms, on a scene drawing almost
/// nothing.
constexpr AoBlurParams ao_blur_params_for(AoTechnique t) {
    // Only RT is stochastic. VXAO cone-traces a voxel field: analytic, and it
    // does not even run this blur today — but if it ever does, it wants the
    // analytic kernel, so it is grouped by what it IS, not by what it uses.
    return (t == AoTechnique::RT) ? AoBlurParams{3, 0.04f, 0.30f}
                                  : AoBlurParams{2, 0.01f, 0.20f};
}

}  // namespace gws::renderer::gpu
