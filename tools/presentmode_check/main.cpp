// ============================================================================
// presentmode_check — "vsync off" has to actually mean off.
//
// The swapchain preferred MAILBOX when vsync was off, and logged it as
// "uncapped". MAILBOX does not block the CPU and does not tear, and neither
// of those makes it uncapped: the presentation engine still releases one
// image per vertical blank, so the steady-state frame rate equals the
// display's refresh rate.
//
// The cost of that was not cosmetic. On a 75 Hz display the engine measured
// 75.1 fps with vsync on and 75.3 fps with it off — 13.33 ms either way,
// which is 1/75 s exactly — while doing 2.6 ms of GPU work. In that regime
// no optimization that brings a frame under the refresh interval can show up
// in the frame rate at all, which is what happened to a real 0.75 ms saving
// in the AO denoise: correct, measurable in the GPU breakdown, and invisible
// in fps.
//
// So the rule asserted here is about CAPPING, not about naming a mode: with
// vsync off, pick a mode that does not cap, whenever one is available.
// ============================================================================
#include "present_mode_choice.h"

#include <cstdio>

using namespace gws::renderer::gpu;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const char* what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what); }
}

const char* name_of(PresentModeChoice m) {
    switch (m) {
        case PresentModeChoice::Fifo:      return "FIFO";
        case PresentModeChoice::Mailbox:   return "MAILBOX";
        case PresentModeChoice::Immediate: return "IMMEDIATE";
    }
    return "?";
}

}  // namespace

int main() {
    std::printf("=== presentmode_check ===\n\n");

    std::printf("Which modes hold the frame rate to the refresh:\n");
    check(caps_to_refresh_rate(PresentModeChoice::Fifo),     "FIFO caps");
    // The whole bug, as a single assertion.
    check(caps_to_refresh_rate(PresentModeChoice::Mailbox),  "MAILBOX caps too (it is not 'uncapped')");
    check(!caps_to_refresh_rate(PresentModeChoice::Immediate), "IMMEDIATE does not cap");

    std::printf("\nvsync on is always FIFO, whatever else is offered:\n");
    for (int m = 0; m < 2; ++m) {
        for (int i = 0; i < 2; ++i) {
            const PresentModeChoice c = choose_present_mode_for(true, m != 0, i != 0);
            static char buf[128];
            std::snprintf(buf, sizeof buf,
                          "vsync on, mailbox=%d immediate=%d -> %s", m, i, name_of(c));
            check(c == PresentModeChoice::Fifo, buf);
        }
    }

    std::printf("\nvsync off picks an uncapped mode whenever one exists:\n");
    {
        const PresentModeChoice both = choose_present_mode_for(false, true, true);
        check(both == PresentModeChoice::Immediate,
              "both offered -> IMMEDIATE, not MAILBOX (the regression this guards)");
        check(!caps_to_refresh_rate(both), "both offered -> the chosen mode does not cap");

        const PresentModeChoice imm_only = choose_present_mode_for(false, false, true);
        check(!caps_to_refresh_rate(imm_only), "immediate only -> does not cap");
    }

    std::printf("\nWhen nothing uncapped exists, degrade rather than lie:\n");
    {
        // MAILBOX is still the better fallback — no tearing, renders ahead —
        // it just must not be sold as uncapped. The swapchain logs a warning
        // in both of these cases; the assertion is that a mode is still
        // chosen and it is the best one available.
        const PresentModeChoice mb = choose_present_mode_for(false, true, false);
        check(mb == PresentModeChoice::Mailbox, "mailbox only -> MAILBOX");

        const PresentModeChoice none = choose_present_mode_for(false, false, false);
        check(none == PresentModeChoice::Fifo,
              "neither offered -> FIFO, which the spec guarantees is present");
    }

    std::printf("\nThe decision is pure:\n");
    {
        constexpr PresentModeChoice a = choose_present_mode_for(false, true, true);
        static_assert(a == PresentModeChoice::Immediate, "must be constexpr-evaluable");
        check(choose_present_mode_for(false, true, true) ==
              choose_present_mode_for(false, true, true),
              "same inputs always yield the same mode");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail)
        std::printf("FAIL presentmode_check — vsync off no longer picks an uncapped mode.\n");
    return g_fail ? 1 : 0;
}
