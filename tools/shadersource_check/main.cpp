// ============================================================================
// shadersource_check — the GLSL a human edits must still be the SPIR-V that runs.
//
// The engine ships precompiled SPIR-V baked into the binary as generated header
// arrays. The GLSL those came from lives in
// engine/renderer/gpu/vulkan/shaders/. Nothing connected the two, and that gap
// is not hypothetical: `shaders/lighting_pass.frag` had been sitting in the
// tree in a state where it did not even COMPILE, because the engine runs the
// baked array and nobody ever fed the file to a compiler.
//
// A stale shader source is a nasty kind of wrong. It does not fail — you edit
// it, rebuild, see no change, and conclude the edit did nothing. Then you edit
// harder.
//
// So: compile each source and compare it to the array that actually ships.
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
        const fs::path c = fs::path(sdk) / "Bin" / "glslangValidator.exe";
        if (fs::exists(c, ec)) return c;
    }
    return {};
}

fs::path find_shader_dir(const fs::path& exe_dir) {
    std::error_code ec;
    for (fs::path d = exe_dir; !d.empty(); d = d.parent_path()) {
        const fs::path c = d / "engine" / "renderer" / "gpu" / "vulkan" / "shaders";
        if (fs::is_directory(c, ec)) return c;
        if (d == d.parent_path()) break;
    }
    return {};
}

struct Case {
    const char*     file;      // source under shaders/
    const uint32_t* shipped;
    uint32_t        shipped_bytes;
};

}  // namespace

int main(int argc, char** argv) {
    std::printf("=== shadersource_check ===\n\n");

    const fs::path exe_dir  = fs::absolute(fs::path(argv[0])).parent_path();
    const fs::path glslang  = find_glslang();
    const fs::path shad_dir = find_shader_dir(exe_dir);

    if (glslang.empty()) {
        std::printf("SKIP shadersource_check: glslangValidator not found (set VULKAN_SDK)\n");
        return 0;
    }
    if (shad_dir.empty()) {
        std::printf("SKIP shadersource_check: needs a source checkout, not an installed engine\n");
        return 0;
    }

    const Case cases[] = {
        {"gbuffer_demo.vert",          kGBufferDemoVertSpv,          kGBufferDemoVertSpv_size},
        {"gbuffer_demo.frag",          kGBufferDemoFragSpv,          kGBufferDemoFragSpv_size},
        {"gbuffer_scene.vert",         kGBufferSceneVertSpv,         kGBufferSceneVertSpv_size},
        {"gbuffer_scene.frag",         kGBufferSceneFragSpv,         kGBufferSceneFragSpv_size},
        {"shadow_caster.vert",         kShadowCasterVertSpv,         kShadowCasterVertSpv_size},
        {"shadow_caster.frag",         kShadowCasterFragSpv,         kShadowCasterFragSpv_size},
        {"transparent.vert",           kTransparentVertSpv,          kTransparentVertSpv_size},
        {"transparent_composite.vert", kTransparentCompositeVertSpv, kTransparentCompositeVertSpv_size},
        {"transparent_composite.frag", kTransparentCompositeFragSpv, kTransparentCompositeFragSpv_size},
    };

    std::error_code ec;
    const fs::path tmp = fs::temp_directory_path() / "gws_shadersource_check";
    fs::create_directories(tmp, ec);

    for (const auto& c : cases) {
        const fs::path src = shad_dir / c.file;
        if (!fs::exists(src, ec)) {
            check(false, std::string(c.file) + ": source file is missing");
            continue;
        }

        // vulkan1.2 => SPIR-V 1.5, which is what the shipped arrays declare.
        // Targeting a different version changes the OpEntryPoint interface
        // rules and would report a difference that is purely about flags.
        const fs::path out = tmp / (std::string(c.file) + ".spv");
        const std::string cmd = "\"\"" + glslang.string() + "\" -V --target-env vulkan1.2 \"" +
                                src.string() + "\" -o \"" + out.string() + "\" >nul 2>&1\"";
        if (std::system(cmd.c_str()) != 0) {
            check(false, std::string(c.file) + ": does not compile");
            continue;
        }

        const auto built   = read_spv(out);
        const auto a       = significant(built);
        std::vector<uint32_t> shipped_words(c.shipped, c.shipped + c.shipped_bytes / sizeof(uint32_t));
        const auto b       = significant(shipped_words);

        if (a.empty() || b.empty()) {
            check(false, std::string(c.file) + ": could not parse one of the modules");
            continue;
        }
        check(a == b, std::string(c.file) + ": compiles to the SPIR-V that ships");
        fs::remove(out, ec);
    }

    fs::remove_all(tmp, ec);

    std::printf("\n%d passed, %d failed", g_pass, g_fail);
    if (g_skip) std::printf(", %d skipped", g_skip);
    std::printf("\n");
    if (g_fail)
        std::printf("FAIL shadersource_check — a shader source no longer matches the shipped SPIR-V.\n"
                    "Either the source drifted, or the header was not regenerated after an edit\n"
                    "(tools/spv_to_header.py).\n");
    return g_fail ? 1 : 0;
}
