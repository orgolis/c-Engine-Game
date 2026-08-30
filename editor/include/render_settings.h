#pragma once
// ============================================================================
// render_settings — the graphics quality a person can actually choose
// (performance audit items F3, F4, F6, F7).
//
// WHAT WAS WRONG. The engine had a master off-switch for every expensive
// effect -- GWS_NO_FX, plus GWS_NO_SSR, GWS_NO_CLOUDS, GWS_NO_DDGI,
// GWS_NO_VOLUMETRIC, GWS_NO_FROXEL and GWS_NO_WATER -- and they were
// ENVIRONMENT VARIABLES. This engine is launched from the Hub, so the single
// most effective performance control in the codebase was unreachable by the
// people who needed it most. That is a recorded prior failure repeating: ray
// -traced reflections were once gated behind GWS_SSR_RT and the user had no
// way to set it.
//
// There was also no preset of any kind. The per-effect toggles existed and
// worked, scattered through the Post-Processing panel, so a user on weak
// hardware had to find each one and already know which were expensive.
//
// WHY A PRESET BEATS TURNING THE DIALS DOWN. A pass that does not run costs
// nothing. A pass at reduced sample count still pays its full-screen read and
// write, which on integrated graphics -- where bandwidth, not arithmetic, is
// the scarce resource -- is most of its cost. So Low DISABLES passes rather
// than lowering their quality.
//
// This file is deliberately free of Vulkan, ImGui and the filesystem-in-header
// so the decision logic (which preset implies which effects, what a malformed
// file falls back to, how a custom edit is detected) is testable headlessly.
// ============================================================================

#include <cstdint>
#include <string>

namespace schizo::editor {

enum class QualityPreset { Low, Medium, High, Custom };

const char* quality_preset_name(QualityPreset p);
/// Parse a preset name; unknown text yields High, because a settings file we
/// cannot read must not silently downgrade someone's visuals.
QualityPreset quality_preset_from_name(const std::string& s);

struct RenderSettings {
    QualityPreset preset = QualityPreset::High;

    /// Fraction of the window's framebuffer the 3D scene renders at.
    ///
    /// The single control that scales every resolution-dependent cost at once,
    /// and the reason it exists: before this the renderer drew at a hardcoded
    /// 1920x1080 no matter the window, so a smaller display did MORE pixel work
    /// than it could display.
    float render_scale = 1.0f;

    bool ssao       = true;
    bool ssr        = true;
    bool clouds     = true;
    bool volumetric = true;
    bool froxel     = false;   // already opt-in: heavy
    bool ddgi       = false;   // also needs hardware ray tracing

    /// Take hardware ray tracing when the device offers it.
    ///
    /// The single most expensive thing measured in this engine. On an RTX 3060
    /// with a trivial scene -- 18 draws, 96k triangles -- the deferred lighting
    /// pass costs **10-12 ms**, about 94% of measured GPU time, because its
    /// ray-query variant casts shadow rays per pixel and loops them for soft
    /// shadows. Off, the lighting pass compiles its non-RT shader instead.
    ///
    /// Applies at startup: the shader variant is chosen when the pipeline is
    /// created, and the device must be told before it enables the extension.
    bool ray_tracing = true;

    /// Set every effect flag and the render scale from `p`. Custom is a no-op:
    /// it means "the user has hand-edited these", so applying it must not
    /// overwrite the very choices it stands for.
    void apply_preset(QualityPreset p);

    /// Which preset these flags correspond to, or Custom when they match none.
    /// Used to move the UI back off "Custom" when a hand edit happens to land
    /// exactly on a preset.
    QualityPreset detect_preset() const;

    /// Clamp to the supported range. Called on load, because a hand-edited file
    /// with render_scale = 0 would produce a zero-sized swapchain image and a
    /// device loss rather than a visible mistake.
    void sanitize();

    static constexpr float kMinScale = 0.25f;
    static constexpr float kMaxScale = 1.0f;
};

/// Serialise to the same shape every other document in this engine uses:
/// KEY=VALUE lines, every field written, unknown keys ignored, parsing that
/// always succeeds.
std::string render_settings_to_text(const RenderSettings& s);
void        render_settings_from_text(const std::string& text, RenderSettings& out);

/// Per-machine, not per-project: this is about the hardware in front of you.
/// False only when the file cannot be read or written.
bool load_render_settings(RenderSettings& out);
bool save_render_settings(const RenderSettings& s);

/// A sane starting preset for the device actually present. An integrated GPU
/// should get a usable first launch without anyone finding the settings menu,
/// because the first launch is where a person decides whether the engine works.
QualityPreset preset_for_device(bool is_integrated, uint64_t vram_bytes);

}  // namespace schizo::editor
