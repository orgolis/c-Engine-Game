#include "render_settings.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace schizo::editor {
namespace {

fs::path settings_path() {
    // Per-machine, beside the Hub's own config. This describes the hardware in
    // front of the user, not the project they happen to have open.
    if (const char* ad = std::getenv("APPDATA"))
        return fs::path(ad) / "GameWorldshaper" / "render_settings.txt";
    if (const char* hp = std::getenv("USERPROFILE"))
        return fs::path(hp) / ".gameworldshaper" / "render_settings.txt";
    return fs::path(".") / "render_settings.txt";
}

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool as_bool(const std::string& v) { return trim(v) == "1" || trim(v) == "true"; }

float as_float(const std::string& v, float fallback) {
    try { return std::stof(v); } catch (...) { return fallback; }
}

}  // namespace

const char* quality_preset_name(QualityPreset p) {
    switch (p) {
        case QualityPreset::Low:    return "Low";
        case QualityPreset::Medium: return "Medium";
        case QualityPreset::High:   return "High";
        default:                    return "Custom";
    }
}

QualityPreset quality_preset_from_name(const std::string& s) {
    const std::string t = trim(s);
    if (t == "Low")    return QualityPreset::Low;
    if (t == "Medium") return QualityPreset::Medium;
    if (t == "Custom") return QualityPreset::Custom;
    return QualityPreset::High;   // unreadable must not silently downgrade
}

void RenderSettings::apply_preset(QualityPreset p) {
    // Custom means "these flags ARE the user's choice"; overwriting them would
    // destroy the thing the value stands for.
    if (p == QualityPreset::Custom) { preset = p; return; }

    preset = p;
    switch (p) {
        case QualityPreset::Low:
            // Everything scene-independent and expensive goes OFF, and the
            // render scale halves. A pass that does not run costs nothing;
            // a pass at lower quality still pays its full-screen traffic.
            ssao = ssr = clouds = volumetric = froxel = ddgi = false;
            ray_tracing  = false;   // the 10-12 ms per-pixel shadow rays
            shadow_rays  = 1;
            render_scale = 0.5f;
            break;
        case QualityPreset::Medium:
            ssao       = true;
            ssr        = false;   // a screen-space raymarch with dependent reads
            clouds     = false;   // typically the most expensive pass in the frame
            volumetric = false;
            froxel     = false;
            ddgi       = false;
            ray_tracing = false;   // still the dominant cost; Medium cannot afford it
            shadow_rays = 4;
            render_scale = 0.75f;
            break;
        case QualityPreset::High:
        default:
            ssao = ssr = clouds = volumetric = true;
            froxel = false;       // stays opt-in even at High: it is very heavy
            ddgi   = false;       // needs hardware ray tracing
            ray_tracing = true;
            shadow_rays = 8;
            render_scale = 1.0f;
            break;
    }
}

QualityPreset RenderSettings::detect_preset() const {
    for (QualityPreset p : {QualityPreset::Low, QualityPreset::Medium, QualityPreset::High}) {
        RenderSettings probe;
        probe.apply_preset(p);
        if (probe.ssao == ssao && probe.ssr == ssr && probe.clouds == clouds &&
            probe.volumetric == volumetric && probe.froxel == froxel &&
            probe.ddgi == ddgi && probe.ray_tracing == ray_tracing &&
            probe.shadow_rays == shadow_rays && probe.vsync == vsync &&
            std::abs(probe.render_scale - render_scale) < 1e-4f) {
            return p;
        }
    }
    return QualityPreset::Custom;
}

void RenderSettings::sanitize() {
    if (!(render_scale >= kMinScale)) render_scale = kMinScale;   // also catches NaN
    if (render_scale > kMaxScale)     render_scale = kMaxScale;
    if (shadow_rays < 1) shadow_rays = 1;
    if (shadow_rays > 8) shadow_rays = 8;
}

std::string render_settings_to_text(const RenderSettings& s) {
    std::ostringstream o;
    o << "# GameWorldshaper render settings (per machine)\n";
    o << "PRESET="       << quality_preset_name(s.preset) << "\n";
    o << "RENDER_SCALE=" << s.render_scale << "\n";
    o << "SSAO="         << (s.ssao ? 1 : 0) << "\n";
    o << "SSR="          << (s.ssr ? 1 : 0) << "\n";
    o << "CLOUDS="       << (s.clouds ? 1 : 0) << "\n";
    o << "VOLUMETRIC="   << (s.volumetric ? 1 : 0) << "\n";
    o << "FROXEL="       << (s.froxel ? 1 : 0) << "\n";
    o << "DDGI="         << (s.ddgi ? 1 : 0) << "\n";
    o << "RAY_TRACING="  << (s.ray_tracing ? 1 : 0) << "\n";
    o << "SHADOW_RAYS="  << s.shadow_rays << "\n";
    o << "VSYNC="        << (s.vsync ? 1 : 0) << "\n";
    return o.str();
}

void render_settings_from_text(const std::string& text, RenderSettings& out) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(t.substr(0, eq));
        const std::string v = t.substr(eq + 1);

        if      (k == "PRESET")       out.preset       = quality_preset_from_name(v);
        else if (k == "RENDER_SCALE") out.render_scale = as_float(trim(v), out.render_scale);
        else if (k == "SSAO")         out.ssao         = as_bool(v);
        else if (k == "SSR")          out.ssr          = as_bool(v);
        else if (k == "CLOUDS")       out.clouds       = as_bool(v);
        else if (k == "VOLUMETRIC")   out.volumetric   = as_bool(v);
        else if (k == "FROXEL")       out.froxel       = as_bool(v);
        else if (k == "DDGI")         out.ddgi         = as_bool(v);
        else if (k == "RAY_TRACING")  out.ray_tracing  = as_bool(v);
        else if (k == "SHADOW_RAYS")  out.shadow_rays  = static_cast<int>(as_float(trim(v), 8.0f));
        else if (k == "VSYNC")        out.vsync        = as_bool(v);
    }
    out.sanitize();
}

bool load_render_settings(RenderSettings& out) {
    std::ifstream in(settings_path());
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    render_settings_from_text(ss.str(), out);
    return true;
}

bool save_render_settings(const RenderSettings& s) {
    const fs::path p = settings_path();
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::trunc);
    if (!out) return false;
    out << render_settings_to_text(s);
    return static_cast<bool>(out);
}

QualityPreset preset_for_device(bool is_integrated, uint64_t vram_bytes) {
    // Integrated parts share system DRAM with the CPU, so the bandwidth every
    // full-screen pass consumes is the scarce resource regardless of how much
    // memory is reported. Start them at Low; a person can raise it and see.
    if (is_integrated) return QualityPreset::Low;
    constexpr uint64_t kGiB = 1024ull * 1024ull * 1024ull;
    if (vram_bytes < 3 * kGiB) return QualityPreset::Medium;
    return QualityPreset::High;
}

}  // namespace schizo::editor
