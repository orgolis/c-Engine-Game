// ====================
// rendersettings_check — quality presets and render scale (F3/F4/F6/F7)
// ====================
//
// The point of this data model is that a person on weak hardware can reach the
// controls that were previously environment variables. So the assertions are
// about the ways such a control quietly fails to help:
//
//   * a "Low" that lowers sample counts instead of DISABLING passes -- on
//     integrated hardware a pass still pays its full-screen read and write, so
//     that Low is barely cheaper than High;
//   * a preset that overwrites the hand-edits it is supposed to represent;
//   * a malformed settings file that silently downgrades someone's visuals, or
//     produces a render scale of 0 and a device loss.

#include "render_settings.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

using namespace schizo::editor;

static int g_failures = 0;
static void check(const char* name, bool ok) {
    std::printf("  [%s] %s\n", ok ? "OK  " : "FAIL", name);
    if (!ok) ++g_failures;
}
static bool approx(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main() {
    std::printf("rendersettings_check — presets and render scale\n");

    std::printf("\n[group] Low actually turns things OFF\n");
    {
        RenderSettings s;
        s.apply_preset(QualityPreset::Low);
        // The whole argument for a preset over a quality dial: a pass that does
        // not run costs nothing, a pass at reduced quality still pays its
        // full-screen traffic, and bandwidth is what weak hardware lacks.
        check("SSAO off",       !s.ssao);
        check("SSR off",        !s.ssr);
        check("clouds off",     !s.clouds);
        check("volumetric off", !s.volumetric);
        check("froxel off",     !s.froxel);
        check("DDGI off",       !s.ddgi);
        // The measured headline: on an RTX 3060 with 18 draws the lighting pass
        // costs 10-12 ms because its ray-query variant casts per-pixel shadow
        // rays. A Low that leaves that on is not a Low.
        check("ray tracing off",  !s.ray_tracing);
        check("shadow rays down to 1", s.shadow_rays == 1);
        check("and the render scale drops", approx(s.render_scale, 0.5f));
        check("preset is recorded", s.preset == QualityPreset::Low);
    }

    std::printf("\n[group] the presets are actually different from each other\n");
    {
        RenderSettings lo, mid, hi;
        lo.apply_preset(QualityPreset::Low);
        mid.apply_preset(QualityPreset::Medium);
        hi.apply_preset(QualityPreset::High);

        // A preset ladder where the rungs are the same is a UI that lies.
        check("Medium keeps SSAO but drops the raymarchers",
              mid.ssao && !mid.ssr && !mid.clouds && !mid.volumetric);
        check("High enables the expensive four",
              hi.ssao && hi.ssr && hi.clouds && hi.volumetric);
        check("Medium also drops ray tracing", !mid.ray_tracing);
        check("only High takes ray tracing",   hi.ray_tracing && !lo.ray_tracing);
        check("shadow rays rise with quality",
              lo.shadow_rays < mid.shadow_rays && mid.shadow_rays < hi.shadow_rays);
        check("scale increases with quality",
              lo.render_scale < mid.render_scale && mid.render_scale < hi.render_scale);
        check("froxel stays opt-in even at High", !hi.froxel);
        check("DDGI stays off at High (needs hardware RT)", !hi.ddgi);
    }

    std::printf("\n[group] Custom does not destroy what it stands for\n");
    {
        RenderSettings s;
        s.apply_preset(QualityPreset::High);
        s.clouds = false;                    // a hand edit
        check("hand edit reads as Custom", s.detect_preset() == QualityPreset::Custom);

        s.preset = QualityPreset::Custom;
        s.apply_preset(QualityPreset::Custom);
        check("applying Custom changes no flag", !s.clouds && s.ssao && s.ssr);
        check("and stays Custom", s.preset == QualityPreset::Custom);

        // A hand edit that lands exactly on a preset should stop saying Custom.
        s.clouds = true;
        check("editing back onto High is detected", s.detect_preset() == QualityPreset::High);
    }

    std::printf("\n[group] round trip\n");
    {
        RenderSettings a;
        a.apply_preset(QualityPreset::Medium);
        a.ssr    = true;              // non-default for Medium
        a.preset = QualityPreset::Custom;
        a.render_scale = 0.625f;

        RenderSettings b;
        render_settings_from_text(render_settings_to_text(a), b);
        check("preset survives",       b.preset == QualityPreset::Custom);
        check("render scale survives", approx(b.render_scale, 0.625f));
        check("every flag survives",   b.ssao == a.ssao && b.ssr == a.ssr &&
                                       b.clouds == a.clouds && b.volumetric == a.volumetric &&
                                       b.froxel == a.froxel && b.ddgi == a.ddgi &&
                                       b.ray_tracing == a.ray_tracing &&
                                       b.shadow_rays == a.shadow_rays);
        check("text is stable", render_settings_to_text(b) == render_settings_to_text(a));
    }

    std::printf("\n[group] a broken file must not make things worse\n");
    {
        RenderSettings s;
        render_settings_from_text("garbage\n=\nPRESET=Nonsense\n", s);
        // Falling back to Low would silently degrade someone's visuals because
        // a file failed to parse; falling back to High is the safe direction.
        check("an unknown preset name falls back to High", s.preset == QualityPreset::High);

        RenderSettings z;
        render_settings_from_text("RENDER_SCALE=0\n", z);
        check("scale 0 is clamped, not passed through",
              approx(z.render_scale, RenderSettings::kMinScale));

        RenderSettings big;
        render_settings_from_text("RENDER_SCALE=9\n", big);
        check("scale above 1 is clamped", approx(big.render_scale, RenderSettings::kMaxScale));

        RenderSettings rays;
        render_settings_from_text("SHADOW_RAYS=99\n", rays);
        check("an absurd ray count is clamped", rays.shadow_rays == 8);
        render_settings_from_text("SHADOW_RAYS=0\n", rays);
        check("zero rays is clamped to 1", rays.shadow_rays == 1);

        RenderSettings nan_s;
        render_settings_from_text("RENDER_SCALE=notanumber\n", nan_s);
        check("an unparseable scale keeps a usable value",
              nan_s.render_scale >= RenderSettings::kMinScale &&
              nan_s.render_scale <= RenderSettings::kMaxScale);

        RenderSettings empty;
        render_settings_from_text("", empty);
        check("an empty file leaves the defaults", empty.preset == QualityPreset::High &&
                                                   approx(empty.render_scale, 1.0f));
    }

    std::printf("\n[group] first launch picks something usable\n");
    {
        constexpr uint64_t kGiB = 1024ull * 1024ull * 1024ull;
        // The whole reason this exists: a person on integrated graphics should
        // not have to find the settings menu to get a usable first run.
        check("integrated starts at Low",     preset_for_device(true,  2 * kGiB) == QualityPreset::Low);
        check("integrated with lots of RAM is still Low",
                                              preset_for_device(true, 16 * kGiB) == QualityPreset::Low);
        check("a small discrete card starts Medium",
                                              preset_for_device(false, 2 * kGiB) == QualityPreset::Medium);
        check("a large discrete card starts High",
                                              preset_for_device(false, 8 * kGiB) == QualityPreset::High);
    }

    std::printf("\n[group] preset names round-trip\n");
    {
        for (QualityPreset p : {QualityPreset::Low, QualityPreset::Medium,
                                QualityPreset::High, QualityPreset::Custom}) {
            if (quality_preset_from_name(quality_preset_name(p)) != p) {
                check(quality_preset_name(p), false);
            }
        }
        check("all four names round-trip", true);
    }

    std::printf("\n%s — %d failure(s)\n", g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
