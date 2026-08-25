#pragma once

// ============================================================================
// MaterialDesc — the one description of a surface, as plain data.
//
// WHY THIS EXISTS. Before this file there were four half-materials and only one
// of them reached the screen:
//   1. MeshRendererComponent's inline fields (colour + PBR scalars + ONE albedo
//      path) — the only path the renderer actually read. The GPU material and
//      gbuffer_scene.frag have supported five maps since Phase 5; four of those
//      slots were permanently bound to engine defaults because nothing upstream
//      could name a file for them.
//   2. MaterialEditorPanel — a full six-slot texture UI whose Apply button was a
//      TODO that logged a line and returned. The inspector carried a second
//      "Mesh Renderer (live)" block underneath precisely because the first one
//      was decorative.
//   3. MaterialAsset / MaterialLibrary — a header whose implementation was
//      deleted as dead code (WORKFLOW_PLAN 3.10). No asset concept survived.
//   4. Terrain layers — an albedo path and a tiling number, with the terrain's
//      roughness hardcoded at 0.95 for every square metre of every terrain.
//
// A surface description that lives in a component cannot be shared between two
// entities, cannot be shared between an entity and a terrain layer, and cannot
// be edited once and seen everywhere. That is the whole reason this is a
// separate asset rather than more fields on the component.
//
// DESIGN CONSTRAINTS, and why the struct looks like this:
//
//  - It maps ONE-TO-ONE onto gpu::MaterialUniforms plus the five sampler slots.
//    A field here that the G-Buffer shader cannot consume would be a lie the
//    format tells the user, so there are none. UV tiling is the field people
//    ask for first and it is deliberately ABSENT: applying it needs
//    MaterialUniforms to grow past 48 bytes, which forces a regen of every
//    shader that mirrors that block (gbuffer_scene, gbuffer_terrain,
//    transparent_pass, and the shadergraph preamble that is *required* to mirror
//    gbuffer_scene exactly). Terrain layers keep their per-layer tiling on
//    TerrainComponent, where it belongs anyway: the same rock material has to
//    tile differently across a 100 m terrain and a 4000 m one.
//
//  - No Vulkan, no ImGui, no renderer dependency. Loading a material must work
//    from the cooker and from a headless check tool, and codegen-free plain data
//    is what makes `material_check` able to assert round-tripping without a GPU.
//
//  - Text, key=value, one key per line — the same shape as the .scene format, so
//    a material diffs cleanly in git and can be fixed in a text editor when a
//    tool writes something wrong.
//
//  - Unknown keys are IGNORED, not errors. A material written by a later version
//    of the editor must still load in an older one, minus the fields it cannot
//    represent. The alternative — refusing the file — turns every additive
//    format change into a migration.
//
//  - Loading distinguishes "file missing / unreadable" from "parsed to
//    defaults". A material system that silently hands back white-and-rough on a
//    typo'd path produces a scene that looks broken with nothing in the log, and
//    that is the single most expensive failure mode this format can have.
// ============================================================================

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <glm/glm.hpp>

namespace gws::assets {

/// How the renderer treats per-fragment alpha. Mirrors schizo::scene::AlphaMode
/// value-for-value so the two convert by cast; they are separate types only
/// because engine/core must not depend on engine/scene.
enum class MaterialAlphaMode : uint8_t {
    Opaque = 0,
    Cutout = 1,
    Blend  = 2,
};

/// Extension used for material assets on disk, including the dot.
inline constexpr const char* kMaterialExtension = ".mat";

/// The version stamped into every file this writes. Bump only for a change the
/// loader must branch on; additive fields do not need it, because unknown keys
/// are ignored and missing keys default.
inline constexpr int kMaterialFormatVersion = 1;

/// A complete surface description. Defaults are the renderer's "no material"
/// look — white, dielectric, fully rough — which is what an entity with no
/// material assigned already renders as, so assigning a fresh material changes
/// nothing until the user edits it. A default that *changed* the look would make
/// "add a material" a destructive action.
struct MaterialDesc {
    std::string name;
    /// UV tiling and offset for this material. Not part of the PBR surface --
    /// it describes how the maps are laid onto geometry, which is why a brick
    /// wall and a brick floor can share one material at different densities.
    glm::vec2 uv_scale{1.0f, 1.0f};
    glm::vec2 uv_offset{0.0f, 0.0f};

    /// Detail maps. A tiled texture is sharp at its authored density and mush
    /// at arm's length; a detail map is the same surface at a much higher
    /// frequency, blended on top.
    ///
    /// Strengths default to 0 -- the blend is then an exact identity, so adding
    /// the fields changes nothing about materials that do not use them.
    std::string detail_albedo_map;
    std::string detail_normal_map;
    float detail_scale            = 8.0f;   // multiplies the base tiling
    float detail_albedo_strength  = 0.0f;
    float detail_normal_strength  = 0.0f;

    /// Height map + parallax depth. Depth 0 disables the effect entirely,
    /// which is what "no parallax" costs: nothing.
    std::string height_map;
    float parallax_depth          = 0.0f;

    // ---- Factors (→ gpu::MaterialUniforms) ----
    glm::vec4 base_color{1.0f, 1.0f, 1.0f, 1.0f};
    float     metallic     = 0.0f;
    float     roughness    = 1.0f;
    float     occlusion    = 1.0f;
    float     normal_scale = 1.0f;
    glm::vec3 emissive{0.0f};
    /// HDR multiplier on `emissive`. The bloom threshold is ~1.0, so an emissive
    /// colour inside [0,1] barely glows; this is the knob that makes a surface
    /// read as a real light source. Kept separate from the colour so the colour
    /// stays a colour in the picker.
    float     emissive_intensity = 1.0f;

    // ---- Rendering flags ----
    MaterialAlphaMode alpha_mode   = MaterialAlphaMode::Opaque;
    float             alpha_cutoff = 0.5f;   // Cutout only; glTF's default
    bool              double_sided = false;

    // ---- Texture maps ----
    // These are the five slots gbuffer_scene.frag already samples, in binding
    // order. Paths are project-relative and UTF-8. Empty = bind the engine
    // default for that slot (white / flat-blue normal / black), which is exactly
    // what the renderer did for all five before materials existed.
    std::string albedo_map;              // binding 1, sRGB
    std::string normal_map;              // binding 2, linear, tangent space
    std::string metallic_roughness_map;  // binding 3, linear; green=rough, blue=metal (glTF)
    std::string occlusion_map;           // binding 4, linear, red channel
    std::string emissive_map;            // binding 5, sRGB

    /// Emissive colour pre-multiplied by intensity — what the G-Buffer receives.
    /// Matches MeshRendererComponent::GetEmissiveLinear so the two paths agree.
    glm::vec3 emissive_linear() const { return emissive * emissive_intensity; }

    bool has_any_map() const {
        return !albedo_map.empty() || !normal_map.empty() ||
               !metallic_roughness_map.empty() || !occlusion_map.empty() ||
               !emissive_map.empty();
    }

    /// Content hash over every field that affects the built GPU material.
    ///
    /// This is what makes the cache content-keyed rather than path-keyed: two
    /// entities pointing at the same file share one GPU material, and an edited
    /// file rebuilds exactly once. `name` is excluded on purpose — renaming a
    /// material must not churn descriptor sets, because the rename changes
    /// nothing the GPU can observe.
    uint64_t content_hash() const {
        uint64_t h = 1469598103934665603ull;               // FNV-1a offset basis
        auto mix_bytes = [&h](const void* p, size_t n) {
            const auto* b = static_cast<const unsigned char*>(p);
            for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
        };
        auto mix_f = [&](float f) {
            // Normalise -0.0 to +0.0 so two materials that compare equal also
            // hash equal; the raw bit patterns differ.
            if (f == 0.0f) f = 0.0f;
            mix_bytes(&f, sizeof f);
        };
        auto mix_s = [&](const std::string& s) {
            mix_bytes(s.data(), s.size());
            h = (h ^ 0xFFu) * 1099511628211ull;            // terminator: "ab"+"c" != "a"+"bc"
        };
        mix_f(base_color.r); mix_f(base_color.g); mix_f(base_color.b); mix_f(base_color.a);
        mix_f(metallic); mix_f(roughness); mix_f(occlusion); mix_f(normal_scale);
        mix_f(emissive.r); mix_f(emissive.g); mix_f(emissive.b); mix_f(emissive_intensity);
        mix_f(alpha_cutoff);
        // The UV transform changes where every map is sampled, so it is as
        // observable as any factor above. Omitting it would mean a tiling edit
        // never invalidates the cached GPU material -- the surface keeps its
        // old repeat and the edit appears to do nothing.
        mix_f(uv_scale.x);  mix_f(uv_scale.y);
        mix_f(uv_offset.x); mix_f(uv_offset.y);
        // Detail maps are as observable as any other field; leaving them out
        // would mean editing detail strength never invalidates the cached GPU
        // material, exactly the trap the UV fields fell into.
        mix_f(detail_scale); mix_f(detail_albedo_strength); mix_f(detail_normal_strength);
        mix_s(detail_albedo_map); mix_s(detail_normal_map);
        mix_f(parallax_depth); mix_s(height_map);
        const uint8_t flags = static_cast<uint8_t>(alpha_mode) |
                              (double_sided ? 0x80u : 0u);
        mix_bytes(&flags, sizeof flags);
        mix_s(albedo_map); mix_s(normal_map); mix_s(metallic_roughness_map);
        mix_s(occlusion_map); mix_s(emissive_map);
        return h;
    }

    bool operator==(const MaterialDesc& o) const {
        return name == o.name && content_hash() == o.content_hash();
    }
    bool operator!=(const MaterialDesc& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// Text serialisation
// ---------------------------------------------------------------------------

namespace detail {

/// Trim ASCII whitespace and a trailing CR. Files authored on Windows and read
/// on Linux (CI runs both) otherwise carry a '\r' into every path, and a texture
/// path with an invisible trailing byte fails to open with a message that names
/// the right file.
inline std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    auto is_ws = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (b < e && is_ws(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && is_ws(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

/// Non-ASCII filenames (Cyrillic, CJK) fail to open through a narrow ifstream on
/// Windows, which interprets the bytes in the ANSI codepage. Same fix the OBJ
/// importer and the asset path utilities already apply.
inline std::filesystem::path u8path(const std::string& s) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

inline float parse_float(const std::string& v, float fallback) {
    try {
        size_t used = 0;
        const float f = std::stof(v, &used);
        return used == 0 ? fallback : f;
    } catch (...) {
        return fallback;   // malformed number keeps the default, never NaN
    }
}

inline int parse_int(const std::string& v, int fallback) {
    try {
        size_t used = 0;
        const int i = std::stoi(v, &used);
        return used == 0 ? fallback : i;
    } catch (...) {
        return fallback;
    }
}

/// Parse "r,g,b" / "r,g,b,a". Components the string omits keep `out`'s value, so
/// a 3-component colour written into a vec4 preserves alpha rather than zeroing
/// it — a truncated line must not turn a surface invisible.
inline void parse_vec(const std::string& v, float* out, int count) {
    std::stringstream ss(v);
    std::string part;
    for (int i = 0; i < count && std::getline(ss, part, ','); ++i)
        out[i] = parse_float(trim(part), out[i]);
}

}  // namespace detail

/// Serialise to the .mat text form. Every field is written, including ones left
/// at their default: a partially-written file makes "did the editor fail to save
/// this, or was it just default?" unanswerable from the file alone.
inline std::string material_to_text(const MaterialDesc& m) {
    std::ostringstream o;
    o << "# GameWorldshaper material\n";
    o << "MATERIAL_VERSION=" << kMaterialFormatVersion << "\n";
    o << "NAME=" << m.name << "\n";
    o << "UV_SCALE=" << m.uv_scale.x << "," << m.uv_scale.y << "\n";
    o << "UV_OFFSET=" << m.uv_offset.x << "," << m.uv_offset.y << "\n";
    o << "DETAIL_ALBEDO_MAP=" << m.detail_albedo_map << "\n";
    o << "DETAIL_NORMAL_MAP=" << m.detail_normal_map << "\n";
    o << "DETAIL_SCALE=" << m.detail_scale << "\n";
    o << "DETAIL_ALBEDO_STRENGTH=" << m.detail_albedo_strength << "\n";
    o << "DETAIL_NORMAL_STRENGTH=" << m.detail_normal_strength << "\n";
    o << "HEIGHT_MAP=" << m.height_map << "\n";
    o << "PARALLAX_DEPTH=" << m.parallax_depth << "\n";
    o << "BASE_COLOR=" << m.base_color.r << "," << m.base_color.g << ","
                       << m.base_color.b << "," << m.base_color.a << "\n";
    o << "METALLIC=" << m.metallic << "\n";
    o << "ROUGHNESS=" << m.roughness << "\n";
    o << "OCCLUSION=" << m.occlusion << "\n";
    o << "NORMAL_SCALE=" << m.normal_scale << "\n";
    o << "EMISSIVE=" << m.emissive.r << "," << m.emissive.g << "," << m.emissive.b << "\n";
    o << "EMISSIVE_INTENSITY=" << m.emissive_intensity << "\n";
    o << "ALPHA_MODE=" << static_cast<int>(m.alpha_mode) << "\n";
    o << "ALPHA_CUTOFF=" << m.alpha_cutoff << "\n";
    o << "DOUBLE_SIDED=" << (m.double_sided ? 1 : 0) << "\n";
    o << "ALBEDO_MAP=" << m.albedo_map << "\n";
    o << "NORMAL_MAP=" << m.normal_map << "\n";
    o << "MR_MAP=" << m.metallic_roughness_map << "\n";
    o << "AO_MAP=" << m.occlusion_map << "\n";
    o << "EMISSIVE_MAP=" << m.emissive_map << "\n";
    return o.str();
}

/// Parse the .mat text form. Always succeeds — an empty or garbage string yields
/// the default material — because the caller that needs to know whether a FILE
/// existed is load_material(), and conflating "unparseable" with "absent" is how
/// a missing asset turns into a silently white surface.
///
/// Values are clamped to the ranges the renderer accepts, so a hand-edited file
/// cannot push roughness to 0 (a perfect mirror the BRDF divides by) or a
/// negative emissive (which reads as a light that removes light).
inline MaterialDesc material_from_text(const std::string& text) {
    MaterialDesc m;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = detail::trim(line);
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = detail::trim(line.substr(0, eq));
        const std::string val = detail::trim(line.substr(eq + 1));

        if      (key == "NAME")               m.name = val;
        else if (key == "UV_SCALE")           detail::parse_vec(val, &m.uv_scale.x, 2);
        else if (key == "UV_OFFSET")          detail::parse_vec(val, &m.uv_offset.x, 2);
        else if (key == "DETAIL_ALBEDO_MAP")  m.detail_albedo_map = val;
        else if (key == "DETAIL_NORMAL_MAP")  m.detail_normal_map = val;
        else if (key == "DETAIL_SCALE")       m.detail_scale = detail::parse_float(val, m.detail_scale);
        else if (key == "DETAIL_ALBEDO_STRENGTH") m.detail_albedo_strength = detail::parse_float(val, m.detail_albedo_strength);
        else if (key == "DETAIL_NORMAL_STRENGTH") m.detail_normal_strength = detail::parse_float(val, m.detail_normal_strength);
        else if (key == "HEIGHT_MAP")         m.height_map = val;
        else if (key == "PARALLAX_DEPTH")     m.parallax_depth = detail::parse_float(val, m.parallax_depth);
        else if (key == "BASE_COLOR")         detail::parse_vec(val, &m.base_color.r, 4);
        else if (key == "METALLIC")           m.metallic = detail::parse_float(val, m.metallic);
        else if (key == "ROUGHNESS")          m.roughness = detail::parse_float(val, m.roughness);
        else if (key == "OCCLUSION")          m.occlusion = detail::parse_float(val, m.occlusion);
        else if (key == "NORMAL_SCALE")       m.normal_scale = detail::parse_float(val, m.normal_scale);
        else if (key == "EMISSIVE")           detail::parse_vec(val, &m.emissive.r, 3);
        else if (key == "EMISSIVE_INTENSITY") m.emissive_intensity = detail::parse_float(val, m.emissive_intensity);
        else if (key == "ALPHA_MODE")         m.alpha_mode = static_cast<MaterialAlphaMode>(
                                                  std::clamp(detail::parse_int(val, 0), 0, 2));
        else if (key == "ALPHA_CUTOFF")       m.alpha_cutoff = detail::parse_float(val, m.alpha_cutoff);
        else if (key == "DOUBLE_SIDED")       m.double_sided = detail::parse_int(val, 0) != 0;
        else if (key == "ALBEDO_MAP")         m.albedo_map = val;
        else if (key == "NORMAL_MAP")         m.normal_map = val;
        else if (key == "MR_MAP")             m.metallic_roughness_map = val;
        else if (key == "AO_MAP")             m.occlusion_map = val;
        else if (key == "EMISSIVE_MAP")       m.emissive_map = val;
        // MATERIAL_VERSION and anything unknown: ignored by design (see header).
    }

    m.base_color        = glm::clamp(m.base_color, glm::vec4(0.0f), glm::vec4(1.0f));
    m.metallic          = std::clamp(m.metallic, 0.0f, 1.0f);
    m.roughness         = std::clamp(m.roughness, 0.04f, 1.0f);
    m.occlusion         = std::clamp(m.occlusion, 0.0f, 1.0f);
    m.normal_scale      = std::clamp(m.normal_scale, 0.0f, 4.0f);
    m.emissive          = glm::max(m.emissive, glm::vec3(0.0f));
    m.emissive_intensity = std::clamp(m.emissive_intensity, 0.0f, 100.0f);
    m.alpha_cutoff      = std::clamp(m.alpha_cutoff, 0.0f, 1.0f);
    return m;
}

/// Read a .mat from disk. Returns false — leaving `out` untouched — when the
/// file cannot be opened, and fills `error` with a sentence naming the path.
/// The distinction matters: a caller that cannot tell "missing" from "default"
/// renders a white surface and logs nothing.
inline bool load_material(const std::string& path, MaterialDesc& out,
                          std::string* error = nullptr) {
    std::ifstream f(detail::u8path(path), std::ios::binary);
    if (!f) {
        if (error) *error = "material not found or unreadable: " + path;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    out = material_from_text(ss.str());
    if (out.name.empty()) {
        // A material with no NAME is legal but awkward to show in a picker;
        // fall back to the filename so the UI always has something to print.
        out.name = detail::u8path(path).stem().string();
    }
    return true;
}

/// Write a .mat to disk, creating parent directories. Returns false with an
/// explanation rather than throwing, because every call site is a UI action that
/// has to report the failure to a person.
inline bool save_material(const std::string& path, const MaterialDesc& m,
                          std::string* error = nullptr) {
    const auto p = detail::u8path(path);
    std::error_code ec;
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec) {
            if (error) *error = "cannot create folder for " + path + ": " + ec.message();
            return false;
        }
    }
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (error) *error = "cannot write material: " + path;
        return false;
    }
    f << material_to_text(m);
    if (!f.good()) {
        if (error) *error = "write failed partway through: " + path;
        return false;
    }
    return true;
}

/// True when `path` names a material asset. Case-insensitive: Windows hands back
/// whatever case the file was created with, and a drag-and-drop of "Rock.MAT"
/// must land in the material slot like any other.
inline bool is_material_path(const std::string& path) {
    if (path.size() < 4) return false;
    std::string ext = path.substr(path.size() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == kMaterialExtension;
}

}  // namespace gws::assets
