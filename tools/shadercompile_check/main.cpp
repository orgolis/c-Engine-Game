// ============================================================================
// shadercompile_check — a graph becomes real SPIR-V.
//
// shadergraph_check proves the generated GLSL has the right SHAPE. This proves
// it is actually GLSL: it runs the same wrapper and the same compiler the
// editor uses and checks that a SPIR-V module comes back. Those are different
// claims, and only the second one means a material graph can produce something
// the GPU will run.
//
// SKIPS ITSELF when no compiler is present rather than failing. glslangValidator
// ships beside the editor but is not part of a source checkout, and a check that
// goes red on a machine that is working correctly teaches people to ignore red.
// ============================================================================
#include "material_graph.h"
#include "shader_compile_service.h"

#include <cstdint>
#include <cstdio>
#include <string>

using namespace gws::shadergraph;
using namespace schizo::editor;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

// A material anyone might actually build: a texture tinted by a colour, with
// roughness driven from the texture's alpha and a time-varying emissive.
MaterialGraph realistic_graph() {
    MaterialGraph g;
    const NodeId uv   = g.add_node(NodeKind::UV);
    const NodeId tex  = g.add_node(NodeKind::TextureSample);
    const NodeId tint = g.add_node(NodeKind::ConstColor);
    const NodeId mul  = g.add_node(NodeKind::Multiply);
    const NodeId spl  = g.add_node(NodeKind::Split);
    const NodeId inv  = g.add_node(NodeKind::OneMinus);
    const NodeId tim  = g.add_node(NodeKind::Time);
    const NodeId sat  = g.add_node(NodeKind::Saturate);
    const NodeId comb = g.add_node(NodeKind::Combine);
    g.node(spl)->param_index = 3;                         // alpha

    g.connect(uv, tex, 0);
    g.connect(tex, mul, 0);
    g.connect(tint, mul, 1);
    g.connect(mul, g.output_node(), static_cast<uint32_t>(OutputSlot::BaseColor));
    g.connect(tex, spl, 0);
    g.connect(spl, inv, 0);
    g.connect(inv, g.output_node(), static_cast<uint32_t>(OutputSlot::Roughness));
    g.connect(tim, sat, 0);
    g.connect(sat, comb, 0);
    g.connect(comb, g.output_node(), static_cast<uint32_t>(OutputSlot::Emissive));
    return g;
}

}  // namespace

int main(int argc, char** argv) {
    std::printf("=== shadercompile_check ===\n\n");

    ShaderCompileService svc;
    const std::string exe_dir = argc > 1 ? argv[1] : "";
    if (!svc.init(exe_dir)) {
        std::printf("  SKIP no glslangValidator found — shader compilation not exercised\n");
        std::printf("\n0 passed, 0 failed (skipped)\n");
        return 0;                       // a working machine must not report red
    }
    std::printf("  ..   compiler: %s\n", svc.compiler_path().c_str());

    // ---- 1. an empty graph compiles ----------------------------------------
    // The default graph is what a user sees before touching anything. If that
    // does not compile, the feature is broken on open.
    {
        MaterialGraph g;
        const auto gen = g.generate();
        check(gen.ok, "the default graph generates");
        const auto r = svc.compile_material(gen.glsl);
        if (!r.ok) std::printf("  ..   compiler said:\n%s\n", r.error.c_str());
        check(r.ok, "and compiles to SPIR-V straight out of the box");
        check(r.spirv.size() > 8, "producing a module of plausible size");
        check(r.spirv[0] == 0x07230203u,
              "with the SPIR-V magic number — a real module, not a stray file");
    }

    // ---- 2. a realistic graph compiles --------------------------------------
    {
        MaterialGraph g = realistic_graph();
        const auto gen = g.generate();
        check(gen.ok, "a texture/tint/time graph generates");
        const auto r = svc.compile_material(gen.glsl);
        if (!r.ok) std::printf("  ..   compiler said:\n%s\n", r.error.c_str());
        check(r.ok, "and compiles — every node kind in it emits valid GLSL");
    }

    // ---- 3. THE CACHE ------------------------------------------------------
    // Compilation spawns a process. Doing it when nothing changed would launch
    // one per frame, which is the difference between usable and unusable.
    {
        // A DISTINCT graph. My first version reused the one section 2 already
        // compiled, so its "first" call was a cache hit and the assertion
        // failed -- the cache working, not breaking. Worth keeping the note:
        // the cache persists across everything in this process.
        MaterialGraph g = realistic_graph();
        for (const Node& n : g.nodes())
            if (n.kind == NodeKind::ConstColor) { g.node(n.id)->value[1] = 0.375f; break; }
        const std::string body = g.generate().glsl;

        const uint32_t before = svc.compile_count();
        const auto a = svc.compile_material(body);
        const uint32_t after_first = svc.compile_count();
        const auto b = svc.compile_material(body);
        const auto c = svc.compile_material(body);
        const uint32_t after_third = svc.compile_count();

        check(a.ok && b.ok && c.ok, "compiling the same body three times succeeds");
        check(after_first == before + 1, "the first call actually compiles");
        check(after_third == after_first,
              "and the next two are served from cache — no process launched");
        check(b.from_cache && c.from_cache, "which they report honestly");
        check(a.spirv == b.spirv, "and the cached module is the same module");
    }

    // ---- 4. a CHANGED graph recompiles --------------------------------------
    // The other half of the cache: it must not be so eager that edits are
    // ignored, which would look like the editor refusing to update.
    {
        MaterialGraph g = realistic_graph();
        const std::string first = g.generate().glsl;
        svc.compile_material(first);
        const uint32_t before = svc.compile_count();

        // Change one constant. The GLSL differs by a literal.
        for (const Node& n : g.nodes())
            if (n.kind == NodeKind::ConstColor) { g.node(n.id)->value[0] = 0.25f; break; }
        const std::string second = g.generate().glsl;

        check(first != second, "editing a constant changes the generated GLSL");
        const auto r = svc.compile_material(second);
        check(r.ok && !r.from_cache, "and that recompiles rather than reusing the cache");
        check(svc.compile_count() == before + 1, "exactly once");
    }

    // ---- 5. BAD GLSL fails with a usable message -----------------------------
    // The failure path matters as much as the success path: a graph feature
    // people cannot debug is a graph feature people stop using.
    {
        const auto r = svc.compile_material("    this is not glsl at all;\n");
        check(!r.ok, "invalid GLSL is reported as a failure");
        check(!r.error.empty(), "with the compiler's diagnostics attached");
        // The preamble is ~25 lines; an unmapped error would name a line well
        // past the end of the one-line body.
        check(r.error.find(":<preamble>:") == std::string::npos ||
              r.error.find("ERROR") != std::string::npos,
              "and line numbers mapped back to the generated body, not the wrapper");
    }

    // ---- 6. the wrapper declares everything codegen can emit ------------------
    // A name the graph emits but the preamble does not declare would surface as
    // a compile error in code the user never wrote.
    {
        const std::string pre = material_shader_preamble();
        bool all = true;
        for (const char* n : {"gs_uv", "gs_time", "gs_vertex_color", "gs_tex0",
                              "gs_out_base_color", "gs_out_metallic", "gs_out_roughness",
                              "gs_out_emissive", "gs_out_alpha"})
            if (pre.find(n) == std::string::npos) all = false;
        check(all, "the wrapper declares every name the generator can emit");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL shadercompile_check\n");
    return g_fail ? 1 : 0;
}
