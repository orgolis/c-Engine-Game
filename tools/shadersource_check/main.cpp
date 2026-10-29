// ============================================================================
// shadersource_check — the GLSL a human edits must still be the SPIR-V that runs.
//
// The engine ships precompiled SPIR-V baked into the binary as generated header
// arrays. Nothing connected those arrays to the GLSL anyone edits, and that gap
// is not hypothetical: `lighting_pass.frag` had been sitting in the tree in a
// state where it did not even COMPILE, because the engine runs the baked array
// and nobody ever fed the file to a compiler.
//
// A stale shader source is a nasty kind of wrong. It does not fail — you edit
// it, rebuild, see no change, and conclude the edit did nothing. Then you edit
// harder.
//
// So: compile each source and compare it to the array that actually ships.
//
// WHERE THE SOURCES LIVE — and why this check reads two directories.
//
// There are two shader directories, for a reason that is historical rather
// than designed:
//
//   tools/                              — the live sources. The regen scripts
//                                         (tools/regen_*_spirv.sh) cd here,
//                                         compile these files to the .spv
//                                         beside them, and spv_to_header.py
//                                         turns those into the shipped arrays.
//   engine/renderer/gpu/vulkan/shaders/ — created by #67, which extracted GLSL
//                                         that had only ever existed as inline
//                                         C++ string literals.
//
// For the shaders that were genuinely inline-only, that extraction gave them
// their first real home and `shaders/` is where they belong. But eight of the
// files #67 created already had a maintained source in tools/, so those became
// frozen 2026-08-10 snapshots of whatever shipped that day while tools/ kept
// moving. This check originally read only `shaders/`, which meant it compared
// the shipped array against the copy nobody edits: three cases went red for
// drift that had already been shipped correctly, and — far worse — a green
// result certified a file the engine does not build from. The check written to
// stop "you edit a shader and nothing changes" was reinforcing it.
//
// So a source is resolved from tools/ FIRST and `shaders/` only as a fallback,
// which is the order in which they are authoritative. And a name present in
// BOTH is itself a failure (see check_no_duplicate_sources) — one shader, one
// home, or this grows back.
//
// WHAT COUNTS AS EQUAL. SPIR-V carries debug annotations (OpSource, OpName,
// OpLine, OpSourceExtension…) and a header word identifying which compiler
// produced it. Those change with the toolchain and change nothing about what
// the GPU does — three of these shaders were originally built by glslc rather
// than glslangValidator and differ in exactly that way. Comparing raw bytes
// would report drift on every SDK upgrade, and a check that cries wolf gets
// switched off. So this walks the instruction stream and compares everything
// EXCEPT debug instructions, which is the strongest claim that is also stable.
//
// Skips (does not fail) when glslangValidator is absent, like the GPU checks do.
// ============================================================================
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// The arrays that actually ship.
#include "gbuffer_demo_spirv.h"
#include "gbuffer_scene_spirv.h"
#include "gbuffer_scene_instanced_spirv.h"
#include "lighting_pass_spirv.h"
#include "lighting_pass_nort_spirv.h"
#include "lighting_pass_rt_novote_spirv.h"
#include "shadow_caster_spirv.h"
#include "transparent_pass_spirv.h"
#include "transparent_composite_spirv.h"

namespace fs = std::filesystem;
using namespace gws::renderer::gpu;   // the generated SPIR-V arrays live here

namespace {

int g_pass = 0, g_fail = 0, g_skip = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

// SPIR-V debug instructions: present or absent, the GPU behaves identically.
bool is_debug_opcode(uint16_t op) {
    switch (op) {
        case 3:   // OpSource
        case 4:   // OpSourceExtension
        case 5:   // OpName
        case 6:   // OpMemberName
        case 7:   // OpString
        case 8:   // OpLine
        case 317: // OpNoLine
        case 330: // OpModuleProcessed
            return true;
        default:  return false;
    }
}

// Instruction stream with debug info and the header stripped. Two modules with
// the same significant stream do the same thing.
std::vector<uint32_t> significant(const std::vector<uint32_t>& words) {
    std::vector<uint32_t> out;
    if (words.size() < 5 || words[0] != 0x07230203u) return out;
    size_t i = 5;                                  // skip magic/version/generator/bound/schema
    while (i < words.size()) {
        const uint32_t first = words[i];
        const uint16_t len   = static_cast<uint16_t>(first >> 16);
        const uint16_t op    = static_cast<uint16_t>(first & 0xFFFFu);
        if (len == 0 || i + len > words.size()) break;   // malformed; stop rather than guess
        if (!is_debug_opcode(op))
            out.insert(out.end(), words.begin() + static_cast<long>(i),
                       words.begin() + static_cast<long>(i + len));
        i += len;
    }
    return out;
}

std::vector<uint32_t> read_spv(const fs::path& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const auto n = static_cast<size_t>(f.tellg());
    f.seekg(0);
    if (n < 20 || (n % 4) != 0) return {};
    std::vector<uint32_t> w(n / 4);
    f.read(reinterpret_cast<char*>(w.data()), static_cast<std::streamsize>(n));
    return f ? w : std::vector<uint32_t>{};
}

fs::path find_glslang() {
    if (const char* sdk = std::getenv("VULKAN_SDK")) {
        std::error_code ec;
        const fs::path c = fs::path(sdk) /
#ifdef _WIN32
            "Bin" / "glslangValidator.exe";
#else
            "bin" / "glslangValidator";
#endif
        if (fs::exists(c, ec)) return c;
    }
    return {};
}

// The repo root, found by walking up from the executable looking for a marker
// that only the source checkout has. An installed engine has neither.
fs::path find_repo_root(const fs::path& exe_dir) {
    std::error_code ec;
    for (fs::path d = exe_dir; !d.empty(); d = d.parent_path()) {
        if (fs::is_directory(d / "engine" / "renderer" / "gpu" / "vulkan" / "shaders", ec) &&
            fs::is_directory(d / "tools", ec))
            return d;
        if (d == d.parent_path()) break;
    }
    return {};
}

fs::path live_dir(const fs::path& root)  { return root / "tools"; }
fs::path extracted_dir(const fs::path& root) {
    return root / "engine" / "renderer" / "gpu" / "vulkan" / "shaders";
}

// tools/ first: that is the directory the regen scripts compile from, so it is
// the one whose contents actually become the shipped array.
fs::path resolve_source(const fs::path& root, const char* file) {
    std::error_code ec;
    const fs::path a = live_dir(root) / file;
    if (fs::exists(a, ec)) return a;
    const fs::path b = extracted_dir(root) / file;
    if (fs::exists(b, ec)) return b;
    return {};
}

struct Case {
    const char*     file;      // resolved via resolve_source()
    const uint32_t* shipped;
    uint32_t        shipped_bytes;
    const char*     defines = "";   // e.g. "-DGWS_RAY_QUERY" for a variant
};

// One shader, one home. A name in both directories is the defect that made
// this check validate the wrong file for two months: the copy nobody compiles
// drifts, and whichever way the check happens to resolve it, half its answers
// are about a file that does not ship. Fails by name so the fix is obvious.
void check_no_duplicate_sources(const fs::path& root) {
    std::error_code ec;
    const fs::path live = live_dir(root), extracted = extracted_dir(root);
    std::vector<std::string> dupes;
    for (const auto& e : fs::directory_iterator(extracted, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string name = e.path().filename().string();
        if (fs::exists(live / name, ec)) dupes.push_back(name);
    }
    std::sort(dupes.begin(), dupes.end());
    if (dupes.empty()) {
        check(true, "every shader source has exactly one home");
        return;
    }
    std::string msg = "shader sources exist in BOTH tools/ and "
                      "engine/renderer/gpu/vulkan/shaders/:";
    for (const auto& d : dupes) msg += "\n         " + d;
    msg += "\n       tools/ is the copy the regen scripts build from, so the"
           "\n       other is dead weight that silently drifts. Delete it.";
    check(false, msg);
}

}  // namespace

int main(int argc, char** argv) {
    std::printf("=== shadersource_check ===\n\n");

    const fs::path exe_dir = fs::absolute(fs::path(argv[0])).parent_path();
    const fs::path glslang = find_glslang();
    const fs::path root    = find_repo_root(exe_dir);

    if (glslang.empty()) {
        std::printf("SKIP shadersource_check: glslangValidator not found (set VULKAN_SDK)\n");
        return 0;
    }
    if (root.empty()) {
        std::printf("SKIP shadersource_check: needs a source checkout, not an installed engine\n");
        return 0;
    }

    check_no_duplicate_sources(root);
    const int dup_fail = g_fail;   // everything after this is drift

    const Case cases[] = {
        {"gbuffer_demo.vert",          kGBufferDemoVertSpv,          kGBufferDemoVertSpv_size},
        {"gbuffer_demo.frag",          kGBufferDemoFragSpv,          kGBufferDemoFragSpv_size},
        {"gbuffer_scene.vert",         kGBufferSceneVertSpv,         kGBufferSceneVertSpv_size},
        {"gbuffer_scene.frag",         kGBufferSceneFragSpv,         kGBufferSceneFragSpv_size},
        {"gbuffer_scene_instanced.vert", kGBufferSceneInstancedVertSpv, kGBufferSceneInstancedVertSpv_size},
        {"shadow_caster.vert",         kShadowCasterVertSpv,         kShadowCasterVertSpv_size},
        {"shadow_caster.frag",         kShadowCasterFragSpv,         kShadowCasterFragSpv_size},
        {"transparent.vert",           kTransparentVertSpv,          kTransparentVertSpv_size},
        {"transparent_composite.vert", kTransparentCompositeVertSpv, kTransparentCompositeVertSpv_size},
        {"transparent_composite.frag", kTransparentCompositeFragSpv, kTransparentCompositeFragSpv_size},
        // The lighting pass: the most expensive shader in the engine, the one
        // whose source was once in the tree not even compiling, and — until
        // tools/ became resolvable above — the one case this check could not
        // cover at all. All three variants come from the ONE source and must be
        // regenerated together (tools/regen_lighting_spirv.sh); covering only
        // the ray-query one would miss exactly the drift that script exists to
        // prevent, which shows up solely on machines without ray tracing.
        {"lighting_pass.vert",         kLightingPassVertSpv,         kLightingPassVertSpv_size},
        {"lighting_pass.frag",         kLightingPassFragSpv,         kLightingPassFragSpv_size,
                                       "-DGWS_RAY_QUERY -DGWS_SUBGROUP_VOTE"},
        {"lighting_pass.frag",         kLightingPassFragRtNoVoteSpv, kLightingPassFragRtNoVoteSpv_size,
                                       "-DGWS_RAY_QUERY"},
        {"lighting_pass.frag",         kLightingPassFragNoRtSpv,     kLightingPassFragNoRtSpv_size},
    };

    std::error_code ec;
    const fs::path tmp = fs::temp_directory_path() / "gws_shadersource_check";
    fs::create_directories(tmp, ec);

    for (const auto& c : cases) {
        // One source feeds two variants, so the file name alone is ambiguous
        // in the output. Name the variant by the define that selects it.
        const std::string label =
            *c.defines ? std::string(c.file) + " [" + c.defines + "]" : std::string(c.file);

        const fs::path src = resolve_source(root, c.file);
        if (src.empty()) {
            check(false, label + ": source file is in neither tools/ nor shaders/");
            continue;
        }

        // vulkan1.2 => SPIR-V 1.5, which is what the shipped arrays declare.
        // Targeting a different version changes the OpEntryPoint interface
        // rules and would report a difference that is purely about flags.
        //
        // -S <stage> rather than relying on the extension: a variant compiled
        // to a temp name keeps its stage explicit, and the regen scripts pass
        // it too, so this matches how the shipped array was actually built.
        const std::string stage = fs::path(c.file).extension().string().substr(1);
        const fs::path out = tmp / (label + ".spv");
        const std::string cmd = "\"\"" + glslang.string() + "\" -V --target-env vulkan1.2 " +
                                (*c.defines ? std::string(c.defines) + " " : std::string()) +
                                "-S " + stage + " \"" +
                                src.string() + "\" -o \"" + out.string() + "\" >nul 2>&1\"";
        if (std::system(cmd.c_str()) != 0) {
            check(false, label + ": does not compile");
            continue;
        }

        const auto built   = read_spv(out);
        const auto a       = significant(built);
        std::vector<uint32_t> shipped_words(c.shipped, c.shipped + c.shipped_bytes / sizeof(uint32_t));
        const auto b       = significant(shipped_words);

        if (a.empty() || b.empty()) {
            check(false, label + ": could not parse one of the modules");
            continue;
        }
        check(a == b, label + ": compiles to the SPIR-V that ships");
        fs::remove(out, ec);
    }

    fs::remove_all(tmp, ec);

    std::printf("\n%d passed, %d failed", g_pass, g_fail);
    if (g_skip) std::printf(", %d skipped", g_skip);
    std::printf("\n");
    // Two failures mean two different things and have two different fixes.
    // Printing the drift remedy for a duplicate-source failure sends the
    // reader to regenerate a header that was never the problem.
    const int drift_fail = g_fail - dup_fail;
    if (drift_fail)
        std::printf("FAIL shadersource_check — a shader source no longer matches the shipped SPIR-V.\n"
                    "Either the source drifted, or the header was not regenerated after an edit\n"
                    "(tools/spv_to_header.py).\n");
    if (dup_fail)
        std::printf("FAIL shadersource_check — a shader source exists in two directories.\n"
                    "Delete the copy under engine/renderer/gpu/vulkan/shaders/; tools/ is the\n"
                    "one the regen scripts compile and therefore the one that ships.\n");
    return g_fail ? 1 : 0;
}
