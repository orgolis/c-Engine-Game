/**
 * @file present_mode_choice.h
 * @brief Which presentation mode "vsync off" should actually mean.
 *
 * Free of any Vulkan dependency so the choice can be checked without a
 * device — vulkan_swapchain.cpp maps the result onto VkPresentModeKHR. Same
 * split as gpu_zones.h's GWS_ZONES_NO_VULKAN half, for the same reason: the
 * decision worth asserting on is a decision, not a graphics call.
 */
#pragma once

namespace gws::renderer::gpu {

enum class PresentModeChoice { Fifo, Mailbox, Immediate };

/// Does this mode hold the frame rate down to the display's refresh?
///
/// FIFO and MAILBOX both do. This is the fact the swapchain log got wrong:
/// MAILBOX was described as "uncapped" because it does not BLOCK the CPU and
/// does not tear. Neither of those makes it uncapped. The presentation engine
/// still releases one image per vertical blank, so once the application has
/// used up the swapchain images it waits in acquire, and the steady-state
/// frame rate equals the refresh rate. Only IMMEDIATE presents without
/// waiting for a blank.
constexpr bool caps_to_refresh_rate(PresentModeChoice m) {
    return m != PresentModeChoice::Immediate;
}

/// Pick a present mode from what the surface supports.
///
/// vsync on -> FIFO. Always available, presents once per refresh, renders no
/// frame nobody sees.
///
/// vsync off -> IMMEDIATE, falling back to MAILBOX and then FIFO. IMMEDIATE
/// comes first because "off" has to mean off. This used to prefer MAILBOX,
/// on the reasoning that it renders ahead and does not tear — both true, and
/// both beside the point: it is still refresh-locked, so turning vsync off
/// changed nothing measurable.
///
/// That was not only a labelling bug. It made the frame rate useless as a
/// measure of anything: on a 75 Hz display the engine sat at 75.1 fps with
/// vsync on and 75.3 fps with it off, 13.33 ms either way (1/75 s exactly),
/// while the GPU did 2.6 ms of work. Every optimization that brings a frame
/// under the refresh interval is invisible in that regime — which is exactly
/// what happened to the AO denoise fix, a real 0.75 ms saving that moved the
/// frame rate not at all.
///
/// The cost of this choice is tearing when vsync is off, which is the
/// conventional meaning of the setting and what someone turning it off is
/// asking for. MAILBOX remains the fallback for drivers without IMMEDIATE,
/// and is worth exposing as its own option ("triple buffering") rather than
/// hiding behind "off".
constexpr PresentModeChoice choose_present_mode_for(bool vsync,
                                                    bool has_mailbox,
                                                    bool has_immediate) {
    if (vsync) return PresentModeChoice::Fifo;
    if (has_immediate) return PresentModeChoice::Immediate;
    if (has_mailbox)   return PresentModeChoice::Mailbox;
    return PresentModeChoice::Fifo;   // guaranteed present; caller should warn
}

}  // namespace gws::renderer::gpu
