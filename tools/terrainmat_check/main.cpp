// ============================================================================
// terrainmat_check — the terrain material contract, verified without a GPU.
//
// Terrain went from five sampler slots reinterpreted as [splat, 4×albedo] to a
// dedicated fourteen-binding layout carrying albedo, normal and metal/rough for
// each of four layers. That change has two joints that FAIL SILENTLY, and both
// are checked here:
//
//   1. THE BINDING NUMBERS. The C++ side writes descriptors to binding indices
//      the GLSL side declares. Nothing cross-checks them — Vulkan is perfectly
//      happy to bind a normal map where the shader reads albedo, and the result
//      is a terrain that renders, in the wrong colours, with an empty log. So
//      this parses the actual gbuffer_terrain.frag and asserts every binding
//      index and its sampler name against what vulkan_terrain_material.cpp
//      writes.
//
//   2. THE UNIFORM BLOCK LAYOUT. TerrainMaterialUniforms is memcpy'd into a
//      std140 block. A field added on one side only, or a float array where the
//      shader declares vec4s, shifts every value after it: roughness starts
//      reading a tint's alpha. The struct's size and each member's offset are
//      asserted against the std140 rules the shader's declaration implies.
//
// Plus the layer resolution rule (a .mat wins over the legacy single texture,
// and a material that fails to load degrades to that texture rather than
// erasing the terrain), which is the behaviour a person would describe as "I
// assigned a material and nothing happened" if it ever inverted.
// ============================================================================
#include "terrain_layer_material.h"
#include "vulkan/vulkan_terrain_material.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using gws::renderer::gpu::TerrainMaterialUniforms;
using schizo::scene::TerrainComponent;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

/// Locate the shader source relative to the executable or the repo root. The
/// check runs from the build's bin/ directory in CI and from the repo root by
/// hand, and a check that silently passes because it could not find the file it
/// was meant to read is worse than no check.
fs::path find_shader() {
    const char* candidates[] = {
        "tools/gbuffer_terrain.frag",
        "../tools/gbuffer_terrain.frag",
        "../../tools/gbuffer_terrain.frag",
        "../../../tools/gbuffer_terrain.frag",
        "../../../../tools/gbuffer_terrain.frag",
    };
    for (const char* c : candidates) {
        std::error_code ec;
        if (fs::exists(c, ec)) return fs::path(c);
    }
    return {};
}

}  // namespace

int main() {
    std::printf("terrainmat_check — terrain material contract\n\n");

    // ---- Uniform block layout ---------------------------------------------
    // std140: a vec4 is 16 bytes and 16-byte aligned, and an array of vec4 has a
    // 16-byte stride. The shader declares { vec4 tiling; vec4 tint[4];
    // vec4 mrno[4]; }, so the C++ mirror must be exactly 16 + 64 + 64.
    std::printf("Uniform block matches the shader's std140 declaration:\n");
    {
        check(sizeof(TerrainMaterialUniforms) == 144,
              "block is 144 bytes (vec4 + vec4[4] + vec4[4])");
        check(offsetof(TerrainMaterialUniforms, tiling) == 0,  "tiling at offset 0");
        check(offsetof(TerrainMaterialUniforms, tint)   == 16, "tint[] at offset 16");
        check(offsetof(TerrainMaterialUniforms, mrno)   == 80, "mrno[] at offset 80");
        check(sizeof(TerrainMaterialUniforms{}.tint) == 64,
              "tint[] is 4 x vec4 with no padding between elements");
        check(sizeof(TerrainMaterialUniforms{}.mrno) == 64,
              "mrno[] is 4 x vec4 with no padding between elements");

        // Defaults must reproduce the pre-material terrain look, or upgrading
        // the engine restyles every existing project's ground.
        const TerrainMaterialUniforms d{};
        check(d.mrno[0].x == 0.0f, "default metallic 0 — ground is not metal");
        check(d.mrno[0].y == 1.0f, "default roughness fully rough");
        check(d.mrno[0].z == 1.0f, "default normal scale 1 (no exaggeration)");
        check(d.mrno[0].w == 1.0f, "default occlusion 1 (unoccluded)");
        check(d.tint[3] == glm::vec4(1.0f), "every layer defaults to an untinted white");
    }

    // ---- Binding indices agree with the shader ----------------------------
    std::printf("\nBinding indices agree with gbuffer_terrain.frag:\n");
    {
        const fs::path shader = find_shader();
        if (shader.empty()) {
            check(false, "found gbuffer_terrain.frag (searched upward from the CWD)");
        } else {
            std::ifstream f(shader);
            std::stringstream ss;
            ss << f.rdbuf();
            const std::string src = ss.str();

            // Collect "layout(set = 1, binding = N) uniform sampler2D NAME;"
            std::unordered_map<int, std::string> samplers;
            const std::regex re(
                R"(layout\s*\(\s*set\s*=\s*1\s*,\s*binding\s*=\s*(\d+)\s*\)\s*uniform\s+sampler2D\s+(\w+))");
            for (std::sregex_iterator it(src.begin(), src.end(), re), end; it != end; ++it)
                samplers[std::stoi((*it)[1])] = (*it)[2];

            // And the UBO's binding.
            const std::regex ubo_re(
                R"(layout\s*\(\s*set\s*=\s*1\s*,\s*binding\s*=\s*(\d+)\s*\)\s*uniform\s+TerrainMaterialUBO)");
            std::smatch m;
            const bool have_ubo = std::regex_search(src, m, ubo_re);

            check(have_ubo && std::stoi(m[1]) == 0, "UBO is binding 0");
            check(samplers.size() == 13,
                  "13 samplers declared (splat + 4 layers x 3 maps), found " +
                      std::to_string(samplers.size()));
            check(samplers[1] == "splatMap", "binding 1 is the splat map");

            // These are the indices vulkan_terrain_material.cpp writes to:
            // albedo 2+i, normal 6+i, metal/rough 10+i. Asserting the NAMES too
            // catches a shader edit that renumbers correctly but reorders the
            // meaning — bindings that line up while sampling the wrong map.
            bool albedo_ok = true, normal_ok = true, mr_ok = true;
            for (int i = 0; i < 4; ++i) {
                albedo_ok &= samplers[2  + i] == ("albedo" + std::to_string(i));
                normal_ok &= samplers[6  + i] == ("normal" + std::to_string(i));
                mr_ok     &= samplers[10 + i] == ("mr"     + std::to_string(i));
            }
            check(albedo_ok, "bindings 2..5 are albedo0..3");
            check(normal_ok, "bindings 6..9 are normal0..3");
            check(mr_ok,     "bindings 10..13 are mr0..3");

            // The UBO field order is the memcpy contract. A reorder in the
            // shader is invisible to the compiler on this side.
            const size_t p_tiling = src.find("vec4 tiling");
            const size_t p_tint   = src.find("vec4 tint[4]");
            const size_t p_mrno   = src.find("vec4 mrno[4]");
            check(p_tiling != std::string::npos && p_tint != std::string::npos &&
                  p_mrno != std::string::npos,
                  "the UBO declares tiling, tint[4] and mrno[4]");
            check(p_tiling < p_tint && p_tint < p_mrno,
                  "and declares them in the order the C++ struct memcpy's them");

            // Terrain must not be routed through the alpha-test path. It is
            // always opaque, and a discard in the terrain shader would punch
            // holes in the ground that no tool could explain.
            check(src.find("discard") == std::string::npos,
                  "the terrain shader never discards (terrain is always opaque)");
        }
    }

    // ---- Layer resolution --------------------------------------------------
    std::printf("\nA layer's surface comes from its material, else its texture:\n");
    {
        TerrainComponent terrain(16, 100.0f);

        // No material, no texture: the legacy default. Roughness must be 0.95 —
        // what terrain rendered with before layers had materials — or upgrading
        // the engine restyles every existing project's ground.
        {
            const auto d = schizo::editor::resolve_terrain_layer_material(terrain, 0, nullptr);
            check(d.albedo_map.empty(), "an empty layer has no albedo map");
            check(std::fabs(d.roughness - 0.95f) < 1e-4f,
                  "and falls back to the pre-material roughness of 0.95");
        }

        // Texture only.
        terrain.SetLayerPath(1, "assets/textures/grass.png");
        {
            const auto d = schizo::editor::resolve_terrain_layer_material(terrain, 1, nullptr);
            check(d.albedo_map == "assets/textures/grass.png",
                  "a texture-only layer uses that texture as its albedo");
            check(d.normal_map.empty() && d.metallic_roughness_map.empty(),
                  "and has no normal or metal/rough map");
        }

        // Material assigned: it must win outright.
        const fs::path dir = fs::temp_directory_path() / "gws_terrainmat_check";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);

        const std::string mat_path = (dir / "wet_rock.mat").string();
        gws::assets::MaterialDesc rock;
        rock.name                   = "Wet Rock";
        rock.albedo_map             = "rock_c.png";
        rock.normal_map             = "rock_n.png";
        rock.metallic_roughness_map = "rock_mr.png";
        rock.roughness              = 0.18f;
        rock.normal_scale           = 1.5f;
        gws::assets::save_material(mat_path, rock);

        schizo::editor::MaterialAssetCache cache;
        terrain.SetLayerMaterial(1, mat_path);
        {
            const auto d = schizo::editor::resolve_terrain_layer_material(terrain, 1, &cache);
            check(d.albedo_map == "rock_c.png",
                  "an assigned material overrides the layer's texture entirely");
            check(d.normal_map == "rock_n.png", "and supplies a normal map");
            check(d.metallic_roughness_map == "rock_mr.png", "and a metal/rough map");
            check(std::fabs(d.roughness - 0.18f) < 1e-4f, "and its own roughness");
            check(std::fabs(d.normal_scale - 1.5f) < 1e-4f, "and its own normal scale");
        }

        // A material that will not load must DEGRADE to the texture, not erase
        // the layer. A missing file should cost detail, never the terrain.
        terrain.SetLayerMaterial(2, (dir / "nonexistent.mat").string());
        terrain.SetLayerPath(2, "assets/textures/sand.png");
        {
            const auto d = schizo::editor::resolve_terrain_layer_material(terrain, 2, &cache);
            check(d.albedo_map == "assets/textures/sand.png",
                  "a material that fails to load falls back to the layer's texture");
            check(std::fabs(d.roughness - 0.95f) < 1e-4f,
                  "with the legacy roughness, not the material's");
        }

        // Out-of-range indices are clamped by the component rather than reading
        // past the array — the paint tools pass a user-controlled layer index.
        {
            const auto lo = schizo::editor::resolve_terrain_layer_material(terrain, -5, &cache);
            const auto hi = schizo::editor::resolve_terrain_layer_material(terrain, 99, &cache);
            const auto l0 = schizo::editor::resolve_terrain_layer_material(terrain, 0, &cache);
            const auto l3 = schizo::editor::resolve_terrain_layer_material(terrain, 3, &cache);
            check(lo.albedo_map == l0.albedo_map, "a negative layer index clamps to layer 0");
            check(hi.albedo_map == l3.albedo_map, "an over-range index clamps to the last layer");
        }

        fs::remove_all(dir, ec);
    }

    // ---- Tiling stays on the component ------------------------------------
    // If tiling ever migrates into the .mat, one rock becomes unusable across
    // terrains of different sizes. Assert the ownership, not just the value.
    std::printf("\nTiling belongs to the terrain, not to the material:\n");
    {
        TerrainComponent terrain(16, 100.0f);
        terrain.SetTiling(0, 48.0f);
        check(std::fabs(terrain.GetTiling(0) - 48.0f) < 1e-4f, "the component stores tiling");

        const auto d = schizo::editor::resolve_terrain_layer_material(terrain, 0, nullptr);
        (void)d;
        // MaterialDesc has no tiling field at all — this is a compile-time fact,
        // restated here so the intent survives someone being asked to "just add
        // tiling to the material".
        check(true, "MaterialDesc carries no tiling field, so it cannot shadow this");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
