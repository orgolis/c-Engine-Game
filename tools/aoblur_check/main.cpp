// ============================================================================
// aoblur_check — the AO denoise must be sized to the technique's noise.
//
// Why this exists. The bilateral blur behind ambient occlusion was tuned for
// ray-traced AO, which is stochastic and needs a wide kernel with loose
// sigmas to clear its grain. That tuning was then applied to EVERY technique,
// including the analytic ones that produce no such grain — and the kernel is
// O(radius^2) with three texture fetches per tap, so the widening from 5x5 to
// 7x7 nearly doubled the work on the default path.
//
// It was not cheap. Measured on a 2-draw / 734-triangle scene at 1920x1032
// (an RTX 3060), AO.Blur cost 14-18 ms while the GTAO it was cleaning cost
// 8.8-9.8 ms: the denoise cost about twice the thing it denoised, to remove
// noise that was not there. Sizing it to the technique took the blur to
// 4.7-8.9 ms and the whole frame from 44 ms to 31 ms.
//
// Nothing could have caught that. The decision lived as three literals inside
// a Vulkan command-recording function, reachable only with a device, on a
// path no check ran. It is now a pure function in ao_blur_params.h, and the
// assertions below are about the SHAPE of the decision — that cost tracks
// noise — not about the specific constants, so retuning stays possible and
// re-applying RT's kernel to everything does not.
// ============================================================================
#include "ao_blur_params.h"

#include <cstdio>
#include <initializer_list>

using namespace gws::renderer::gpu;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const char* what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what); }
}

// Taps in the square kernel: what the cost is actually proportional to.
constexpr int taps(AoBlurParams p) { return (2 * p.radius + 1) * (2 * p.radius + 1); }

constexpr AoTechnique kAnalytic[] = {
    AoTechnique::SSAO, AoTechnique::HBAO, AoTechnique::HDAO,
    AoTechnique::GTAO, AoTechnique::VXAO,
};

const char* name_of(AoTechnique t) {
    switch (t) {
        case AoTechnique::SSAO: return "SSAO";
        case AoTechnique::HBAO: return "HBAO";
        case AoTechnique::HDAO: return "HDAO";
        case AoTechnique::GTAO: return "GTAO";
        case AoTechnique::VXAO: return "VXAO";
        case AoTechnique::RT:   return "RT";
    }
    return "?";
}

}  // namespace

int main() {
    std::printf("=== aoblur_check ===\n\n");

    const AoBlurParams rt = ao_blur_params_for(AoTechnique::RT);

    std::printf("Only the stochastic technique pays for a wide kernel:\n");
    for (AoTechnique t : kAnalytic) {
        const AoBlurParams p = ao_blur_params_for(t);
        static char buf[128];
        // The real regression is "RT's kernel got applied to everything", so
        // assert strictly less, not merely "not greater".
        std::snprintf(buf, sizeof buf,
                      "%s denoises with fewer taps than RT (%d vs %d)",
                      name_of(t), taps(p), taps(rt));
        check(taps(p) < taps(rt), buf);
    }

    std::printf("\nEvery technique still gets a usable kernel:\n");
    // A radius of 0 would be a 1x1 "blur" — i.e. silently disabling the
    // denoise, which is a different change wearing this one's clothes.
    for (AoTechnique t : {AoTechnique::SSAO, AoTechnique::HBAO, AoTechnique::HDAO,
                          AoTechnique::GTAO, AoTechnique::VXAO, AoTechnique::RT}) {
        const AoBlurParams p = ao_blur_params_for(t);
        static char buf[128];
        std::snprintf(buf, sizeof buf, "%s radius is at least 1 (got %d)", name_of(t), p.radius);
        check(p.radius >= 1, buf);
    }

    std::printf("\nSigmas track the kernel, so a wide blur is also a tolerant one:\n");
    {
        const AoBlurParams g = ao_blur_params_for(AoTechnique::GTAO);
        // Loose sigmas on a narrow kernel smear across edges for no reach;
        // tight sigmas on a wide one pay for taps that get weighted to zero.
        // Either way the two must move together.
        check(rt.depth_sigma  > g.depth_sigma,
              "RT tolerates more depth difference than an analytic technique");
        check(rt.normal_sigma > g.normal_sigma,
              "RT tolerates more normal difference than an analytic technique");
    }

    std::printf("\nThe decision is total and pure:\n");
    {
        // constexpr proves it has no hidden state and cannot depend on a
        // device, a setting, or the frame it is called on.
        constexpr AoBlurParams a = ao_blur_params_for(AoTechnique::GTAO);
        constexpr AoBlurParams b = ao_blur_params_for(AoTechnique::GTAO);
        static_assert(a.radius == b.radius, "must be constexpr-evaluable");
        check(a.radius == b.radius && a.depth_sigma == b.depth_sigma,
              "same technique always yields the same kernel");

        bool all_sane = true;
        for (AoTechnique t : {AoTechnique::SSAO, AoTechnique::HBAO, AoTechnique::HDAO,
                              AoTechnique::GTAO, AoTechnique::VXAO, AoTechnique::RT}) {
            const AoBlurParams p = ao_blur_params_for(t);
            if (!(p.depth_sigma > 0.0f && p.normal_sigma > 0.0f)) all_sane = false;
        }
        // A zero sigma divides through max(sigma, 1e-5) in the shader and
        // collapses every tap weight to zero, which reads as a black screen.
        check(all_sane, "every technique has strictly positive sigmas");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail)
        std::printf("FAIL aoblur_check — the AO denoise no longer tracks the technique's noise.\n");
    return g_fail ? 1 : 0;
}
