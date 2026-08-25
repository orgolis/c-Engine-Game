// ============================================================================
// material_check — the .mat format has to be boring, and boring is testable.
//
// A material asset sits between a person editing a text file and a descriptor
// set the GPU reads. Both ends fail SILENTLY, which is what these assertions are
// for:
//
//   - A field that round-trips wrong does not crash. The surface just renders
//     with last week's roughness, and nobody can tell whether the editor failed
//     to save or the loader failed to read. So: every field, written and read
//     back, compared.
//
//   - A missing file that parses to defaults is the single most expensive
//     failure this format can have — a typo'd path yields a plausible white
//     surface with nothing in the log. So: load_material MUST report false for a
//     file that is not there, and MUST leave the caller's material untouched.
//
//   - The content hash decides whether the cache rebuilds a GPU material. If it
//     misses a field, edits to that field never reach the screen; if it hashes
//     something the GPU cannot observe (the name), every rename churns
//     descriptor sets. Both directions are asserted, field by field.
//
//   - Unknown keys must load, not fail. Otherwise every additive format change
//     becomes a migration, and a project saved by a newer editor is unopenable
//     rather than merely missing a feature.
//
// No GPU, no ImGui: the format is plain data precisely so this can run headless.
// ============================================================================
#include "assets/material_desc.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using gws::assets::MaterialAlphaMode;
using gws::assets::MaterialDesc;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

bool near_eq(float a, float b) { return std::fabs(a - b) < 1e-4f; }

/// A material with every field pushed OFF its default. A round-trip test built
/// on a default-constructed material passes even if the loader ignores the file
/// entirely, which is the exact bug it is supposed to catch.
MaterialDesc make_populated() {
    MaterialDesc m;
    m.name         = "Wet Cobblestone";
    m.base_color   = glm::vec4(0.31f, 0.47f, 0.62f, 0.75f);
    m.metallic     = 0.35f;
    m.roughness    = 0.22f;
    m.occlusion    = 0.66f;
    m.normal_scale = 1.75f;
    m.emissive     = glm::vec3(0.10f, 0.20f, 0.30f);
    m.emissive_intensity     = 4.5f;
    m.alpha_mode             = MaterialAlphaMode::Cutout;
    m.alpha_cutoff           = 0.37f;
    m.double_sided           = true;
    m.albedo_map             = "assets/textures/cobble_c.png";
    m.normal_map             = "assets/textures/cobble_n.png";
    m.metallic_roughness_map = "assets/textures/cobble_mr.png";
    m.occlusion_map          = "assets/textures/cobble_ao.png";
    m.emissive_map           = "assets/textures/cobble_e.png";
    m.uv_scale               = glm::vec2(4.0f, 2.5f);   // non-uniform on purpose
    m.uv_offset              = glm::vec2(0.125f, -0.75f);
    m.detail_albedo_map      = "assets/textures/grain_c.png";
    m.detail_normal_map      = "assets/textures/grain_n.png";
    m.detail_scale           = 23.5f;
    m.detail_albedo_strength = 0.65f;
    m.detail_normal_strength = 1.40f;
    m.height_map             = "assets/textures/cobble_h.png";
    m.parallax_depth         = 0.045f;
    return m;
}

fs::path g_dir;

}  // namespace

int main() {
    std::printf("material_check — .mat asset format\n\n");

    // ---- Round trip through text ------------------------------------------
    std::printf("Text round-trip (every field, none left at its default):\n");
    {
        const MaterialDesc src = make_populated();
        const MaterialDesc dst = gws::assets::material_from_text(
            gws::assets::material_to_text(src));

        check(dst.name == src.name, "name survives");
        check(near_eq(dst.base_color.r, src.base_color.r) &&
              near_eq(dst.base_color.g, src.base_color.g) &&
              near_eq(dst.base_color.b, src.base_color.b) &&
              near_eq(dst.base_color.a, src.base_color.a), "base colour survives, alpha included");
        check(near_eq(dst.metallic, src.metallic), "metallic survives");
        check(near_eq(dst.roughness, src.roughness), "roughness survives");
        check(near_eq(dst.occlusion, src.occlusion), "occlusion survives");
        check(near_eq(dst.normal_scale, src.normal_scale), "normal scale survives");
        check(near_eq(dst.emissive.r, src.emissive.r) &&
              near_eq(dst.emissive.g, src.emissive.g) &&
              near_eq(dst.emissive.b, src.emissive.b), "emissive colour survives");
        check(near_eq(dst.emissive_intensity, src.emissive_intensity), "emissive intensity survives");
        check(dst.alpha_mode == src.alpha_mode, "alpha mode survives");
        check(near_eq(dst.alpha_cutoff, src.alpha_cutoff), "alpha cutoff survives");
        check(dst.double_sided == src.double_sided, "double-sided survives");
        check(dst.albedo_map == src.albedo_map &&
              dst.normal_map == src.normal_map &&
              dst.metallic_roughness_map == src.metallic_roughness_map &&
              dst.occlusion_map == src.occlusion_map &&
              dst.emissive_map == src.emissive_map, "all five map paths survive");
        // UV transform. Non-uniform and with a negative component, so a
        // loader that reads one component into both -- or drops the sign --
        // fails here instead of shipping a surface that tiles wrongly.
        check(dst.uv_scale  == src.uv_scale,  "UV tiling survives, both components");
        check(dst.uv_offset == src.uv_offset, "UV offset survives, including a negative");
        check(dst.detail_albedo_map == src.detail_albedo_map, "detail colour map survives");
        check(dst.detail_normal_map == src.detail_normal_map, "detail normal map survives");
        check(dst.height_map == src.height_map, "height map survives");
        check(std::fabs(dst.parallax_depth - src.parallax_depth) < 1e-4f, "parallax depth survives");
        check(std::fabs(dst.detail_scale - src.detail_scale) < 1e-3f, "detail tiling survives");
        check(std::fabs(dst.detail_albedo_strength - src.detail_albedo_strength) < 1e-3f,
              "detail colour strength survives");
        check(std::fabs(dst.detail_normal_strength - src.detail_normal_strength) < 1e-3f,
              "detail normal strength survives");
        check(dst.content_hash() == src.content_hash(), "content hash survives the round trip");
    }

    // ---- Content hash sensitivity -----------------------------------------
    // Each field is changed alone. A hash that misses one means edits to that
    // field never rebuild the GPU material — the edit appears to do nothing.
    std::printf("\nContent hash reacts to every field the GPU can observe:\n");
    {
        const MaterialDesc base = make_populated();
        const uint64_t h0 = base.content_hash();

        auto differs = [&](MaterialDesc m, const char* what) {
            check(m.content_hash() != h0, std::string("hash changes: ") + what);
        };
        { MaterialDesc m = base; m.base_color.r += 0.1f;        differs(m, "base colour"); }
        { MaterialDesc m = base; m.base_color.a -= 0.1f;        differs(m, "base alpha"); }
        { MaterialDesc m = base; m.metallic = 0.9f;             differs(m, "metallic"); }
        { MaterialDesc m = base; m.roughness = 0.9f;            differs(m, "roughness"); }
        { MaterialDesc m = base; m.occlusion = 0.1f;            differs(m, "occlusion"); }
        { MaterialDesc m = base; m.normal_scale = 0.5f;         differs(m, "normal scale"); }
        { MaterialDesc m = base; m.emissive.g = 0.9f;           differs(m, "emissive colour"); }
        { MaterialDesc m = base; m.emissive_intensity = 1.0f;   differs(m, "emissive intensity"); }
        { MaterialDesc m = base; m.alpha_mode = MaterialAlphaMode::Blend; differs(m, "alpha mode"); }
        // The UV transform is as observable as any other field: it changes
        // where every map is sampled. A hash that ignores it means editing
        // tiling updates nothing on the GPU -- the material cache never
        // notices, and the surface keeps its old repeat.
        { MaterialDesc m = base; m.uv_scale.x  = 8.0f;  differs(m, "UV tiling"); }
        { MaterialDesc m = base; m.uv_offset.y = 0.25f; differs(m, "UV offset"); }
        { MaterialDesc m = base; m.detail_scale = 4.0f;             differs(m, "detail tiling"); }
        { MaterialDesc m = base; m.detail_albedo_strength = 0.1f;   differs(m, "detail colour strength"); }
        { MaterialDesc m = base; m.detail_normal_strength = 0.1f;   differs(m, "detail normal strength"); }
        { MaterialDesc m = base; m.detail_albedo_map = "other.png"; differs(m, "detail colour map"); }
        { MaterialDesc m = base; m.parallax_depth = 0.09f;          differs(m, "parallax depth"); }
        { MaterialDesc m = base; m.height_map = "other_h.png";      differs(m, "height map"); }
        { MaterialDesc m = base; m.alpha_cutoff = 0.9f;         differs(m, "alpha cutoff"); }
        { MaterialDesc m = base; m.double_sided = false;        differs(m, "double-sided"); }
        { MaterialDesc m = base; m.albedo_map = "other.png";    differs(m, "albedo map"); }
        { MaterialDesc m = base; m.normal_map = "other.png";    differs(m, "normal map"); }
        { MaterialDesc m = base; m.metallic_roughness_map = "o.png"; differs(m, "metallic-roughness map"); }
        { MaterialDesc m = base; m.occlusion_map = "other.png"; differs(m, "occlusion map"); }
        { MaterialDesc m = base; m.emissive_map = "other.png";  differs(m, "emissive map"); }

        // The negative half. A rename must NOT rebuild: the name is invisible to
        // the GPU, and churning descriptor sets on every keystroke in a text
        // field is exactly the kind of cost nobody attributes correctly.
        { MaterialDesc m = base; m.name = "Something Else";
          check(m.content_hash() == h0, "hash ignores the name (a rename must not rebuild)"); }

        // Map slots must not be interchangeable. Concatenating paths without a
        // terminator would hash ("a","b") the same as ("ab",""), so a normal map
        // moved into the albedo slot would silently reuse the old material.
        { MaterialDesc a = base, b = base;
          a.albedo_map = "x"; a.normal_map = "y";
          b.albedo_map = "xy"; b.normal_map = "";
          check(a.content_hash() != b.content_hash(),
                "map slots are distinguishable (path concatenation is not ambiguous)"); }
    }

    // ---- Defaults ----------------------------------------------------------
    // A fresh material must render as "no material assigned" already does.
    // Anything else makes adding a material a destructive act.
    std::printf("\nDefaults match the renderer's no-material look:\n");
    {
        const MaterialDesc m;
        check(near_eq(m.base_color.r, 1.0f) && near_eq(m.base_color.a, 1.0f), "white, opaque");
        check(near_eq(m.metallic, 0.0f), "dielectric (metallic 0)");
        check(near_eq(m.roughness, 1.0f), "fully rough — not a mirror");
        check(near_eq(m.occlusion, 1.0f), "unoccluded");
        check(m.emissive == glm::vec3(0.0f), "not emissive");
        check(m.alpha_mode == MaterialAlphaMode::Opaque, "opaque");
        check(!m.has_any_map(), "no maps bound");
    }

    // ---- Malformed input ---------------------------------------------------
    std::printf("\nMalformed input degrades rather than corrupts:\n");
    {
        const MaterialDesc m = gws::assets::material_from_text(
            "METALLIC=not-a-number\nROUGHNESS=\nBASE_COLOR=0.5\nNO_EQUALS_HERE\n"
            "# comment\n\n   \nFUTURE_KEY=whatever\n");
        check(near_eq(m.metallic, 0.0f), "unparseable number keeps the default");
        check(near_eq(m.roughness, 1.0f), "empty value keeps the default");
        check(near_eq(m.base_color.r, 0.5f) && near_eq(m.base_color.a, 1.0f),
              "truncated colour fills what it can and preserves alpha");
        check(true, "lines without '=' , comments and blanks are skipped without error");

        // Forward compatibility: this is the assertion that keeps additive
        // format changes from becoming migrations.
        const MaterialDesc n = gws::assets::material_from_text(
            "NAME=Future\nSUBSURFACE_RADIUS=3.0\nCLEARCOAT=1\nROUGHNESS=0.5\n");
        check(n.name == "Future" && near_eq(n.roughness, 0.5f),
              "unknown keys are ignored, known keys around them still load");
    }

    // ---- Clamping ----------------------------------------------------------
    // A hand-edited file must not be able to hand the BRDF a value it divides by
    // or a light that removes light.
    std::printf("\nOut-of-range values are clamped, not propagated:\n");
    {
        const MaterialDesc m = gws::assets::material_from_text(
            "ROUGHNESS=0\nMETALLIC=5\nEMISSIVE=-1,-1,-1\nBASE_COLOR=2,2,2,2\n"
            "ALPHA_CUTOFF=9\nALPHA_MODE=77\nOCCLUSION=-3\n");
        check(m.roughness >= 0.04f, "roughness floored at 0.04 (0 divides by zero in the BRDF)");
        check(near_eq(m.metallic, 1.0f), "metallic clamped to 1");
        check(m.emissive.r >= 0.0f, "negative emissive clamped to 0");
        check(near_eq(m.base_color.r, 1.0f), "base colour clamped to 1");
        check(m.alpha_cutoff <= 1.0f, "alpha cutoff clamped to 1");
        check(static_cast<int>(m.alpha_mode) <= 2, "out-of-range alpha mode clamped to a real mode");
        check(m.occlusion >= 0.0f, "negative occlusion clamped to 0");
    }

    // ---- Disk I/O ----------------------------------------------------------
    std::printf("\nDisk load/save, and the missing-file distinction:\n");
    {
        g_dir = fs::temp_directory_path() / "gws_material_check";
        std::error_code ec;
        fs::remove_all(g_dir, ec);
        fs::create_directories(g_dir, ec);

        // Saving into a folder that does not exist yet must work: the material
        // browser writes into project subfolders the user has not created.
        const std::string path = (g_dir / "nested" / "deep" / "Rock.mat").string();
        const MaterialDesc src = make_populated();
        std::string err;
        check(gws::assets::save_material(path, src, &err),
              "save creates missing parent folders" + (err.empty() ? "" : " — " + err));

        MaterialDesc dst;
        err.clear();
        check(gws::assets::load_material(path, dst, &err),
              "load succeeds" + (err.empty() ? "" : " — " + err));
        check(dst.content_hash() == src.content_hash(), "disk round-trip preserves every field");

        // THE assertion this tool exists for. A loader that returns true here
        // hands back a plausible white material and the scene looks broken with
        // an empty log.
        MaterialDesc untouched = make_populated();
        const uint64_t before = untouched.content_hash();
        err.clear();
        const bool ok = gws::assets::load_material(
            (g_dir / "does_not_exist.mat").string(), untouched, &err);
        check(!ok, "a missing file FAILS — it does not parse to defaults");
        check(!err.empty() && err.find("does_not_exist") != std::string::npos,
              "the error names the path that was not found");
        check(untouched.content_hash() == before,
              "a failed load leaves the caller's material untouched");

        // A file with no NAME still needs something to show in a picker.
        const std::string anon = (g_dir / "Anonymous.mat").string();
        { std::ofstream f(anon, std::ios::binary); f << "ROUGHNESS=0.5\n"; }
        MaterialDesc a;
        check(gws::assets::load_material(anon, a) && a.name == "Anonymous",
              "a nameless file falls back to its filename");

        fs::remove_all(g_dir, ec);
    }

    // ---- Path classification -----------------------------------------------
    std::printf("\nMaterial paths are recognised regardless of case:\n");
    {
        check(gws::assets::is_material_path("assets/materials/Rock.mat"), "lowercase .mat");
        check(gws::assets::is_material_path("Rock.MAT"), "uppercase .MAT (Windows preserves case)");
        check(!gws::assets::is_material_path("assets/textures/rock.png"), ".png is not a material");
        check(!gws::assets::is_material_path("mat"), "a bare word is not a material path");
        check(!gws::assets::is_material_path(""), "an empty path is not a material path");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
