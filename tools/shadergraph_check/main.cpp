// ============================================================================
// shadergraph_check — the material graph emits correct, deterministic GLSL.
//
// Codegen is where a shader graph fails silently. A graph that emits statements
// in the wrong order, drops a branch, or writes an integer literal produces a
// shader that COMPILES CLEANLY and shades subtly wrong — and the symptom
// ("this material looks a bit off") points nowhere near the cause.
//
// So these assert the properties that survive being unable to see the result:
// declaration order, type conversion, cycle refusal, determinism, and dead-code
// exclusion. Checking that "it produced some text" would pass for almost any
// broken implementation.
// ============================================================================
#include "material_graph.h"

#include <cstdio>
#include <string>

using namespace gws::shadergraph;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

bool has(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// Position of a substring, for asserting that one line precedes another.
size_t at(const std::string& hay, const std::string& needle) {
    return hay.find(needle);
}

}  // namespace

int main() {
    std::printf("=== shadergraph_check ===\n\n");

    // ---- 1. a graph starts usable ------------------------------------------
    {
        MaterialGraph g;
        check(g.output_node() != kInvalidNode, "a new graph has an output node");
        const auto r = g.generate();
        check(r.ok, "an empty graph still generates");
        check(has(r.glsl, "gs_out_base_color") && has(r.glsl, "gs_out_roughness"),
              "writing every surface slot, so a half-built graph still renders");
        check(has(r.glsl, "gs_out_base_color = vec3(0.8)"),
              "unconnected slots use the engine's defaults rather than black");
    }

    // ---- 2. DECLARATION ORDER — the property that matters most -------------
    // GLSL requires a variable to be declared before use. A graph walked in the
    // wrong order emits `n2 = n5 * 2.0;` before n5 exists, which is a compile
    // error at best and, with reordering, a wrong value at worst.
    {
        MaterialGraph g;
        const NodeId c  = g.add_node(NodeKind::ConstColor);
        const NodeId m  = g.add_node(NodeKind::Multiply);
        const NodeId f  = g.add_node(NodeKind::ConstFloat);
        g.connect(c, m, 0);
        g.connect(f, m, 1);
        g.connect(m, g.output_node(), static_cast<uint32_t>(OutputSlot::BaseColor));

        const auto r = g.generate();
        check(r.ok, "a small graph generates");
        const size_t pc = at(r.glsl, "n" + std::to_string(c) + " =");
        const size_t pf = at(r.glsl, "n" + std::to_string(f) + " =");
        const size_t pm = at(r.glsl, "n" + std::to_string(m) + " =");
        check(pc != std::string::npos && pf != std::string::npos && pm != std::string::npos,
              "every contributing node is emitted");
        check(pc < pm && pf < pm,
              "and BOTH inputs are declared before the node that consumes them");
    }

    // ---- 3. type conversion is explicit ------------------------------------
    // A vec4 driving a vec3 slot must be narrowed deliberately. GLSL will not
    // do it implicitly, and doing it wrongly is how a colour picks up alpha.
    {
        MaterialGraph g;
        const NodeId c = g.add_node(NodeKind::ConstColor);          // vec4
        g.connect(c, g.output_node(), static_cast<uint32_t>(OutputSlot::BaseColor));
        const auto r = g.generate();
        check(has(r.glsl, ".xyz"), "a vec4 feeding a vec3 slot is swizzled, not truncated by luck");

        MaterialGraph g2;
        const NodeId f = g2.add_node(NodeKind::ConstFloat);         // float
        g2.connect(f, g2.output_node(), static_cast<uint32_t>(OutputSlot::Emissive));
        const auto r2 = g2.generate();
        check(has(r2.glsl, "vec3("),
              "and a float feeding a vec3 slot is broadcast explicitly");
    }

    // ---- 4. float literals always carry a decimal point --------------------
    // "1" is an int in GLSL, and integer division is the classic way generated
    // code compiles and computes something else entirely.
    {
        MaterialGraph g;
        const NodeId f = g.add_node(NodeKind::ConstFloat);
        g.node(f)->value[0] = 2.0f;                                 // prints as "2"
        g.connect(f, g.output_node(), static_cast<uint32_t>(OutputSlot::Roughness));
        const auto r = g.generate();
        check(has(r.glsl, "2.0"), "a whole-number constant is emitted as 2.0, not 2");
    }

    // ---- 5. CYCLES are refused, not followed -------------------------------
    // One dragged link makes a cycle. The alternative to detecting it is an
    // editor that hangs.
    {
        MaterialGraph g;
        const NodeId a = g.add_node(NodeKind::Add);
        const NodeId b = g.add_node(NodeKind::Add);
        g.connect(a, b, 0);
        g.connect(b, a, 0);                                          // the loop
        g.connect(b, g.output_node(), static_cast<uint32_t>(OutputSlot::BaseColor));

        const auto r = g.generate();
        check(!r.ok, "a cyclic graph fails generation instead of hanging");
        check(has(r.error, "cycle"), "and says so in the error");
    }

    // ---- 6. a node cannot feed itself --------------------------------------
    {
        MaterialGraph g;
        const NodeId a = g.add_node(NodeKind::Add);
        check(!g.connect(a, a, 0), "self-connection is refused at the link stage");
    }

    // ---- 7. one source per input -------------------------------------------
    // Two sources on one input has no defined value; silently picking one is
    // worse than replacing.
    {
        MaterialGraph g;
        const NodeId c1 = g.add_node(NodeKind::ConstFloat);
        const NodeId c2 = g.add_node(NodeKind::ConstFloat);
        const NodeId m  = g.add_node(NodeKind::Multiply);
        g.connect(c1, m, 0);
        g.connect(c2, m, 0);                                         // same input
        size_t into_m0 = 0;
        for (const auto& l : g.links())
            if (l.dst_node == m && l.dst_input == 0) ++into_m0;
        check(into_m0 == 1, "connecting a second source replaces the first");
    }

    // ---- 8. DEAD NODES are not emitted -------------------------------------
    // An editor accumulates disconnected experiments. Emitting them puts dead
    // code in every shader and breaks byte-identical regeneration.
    {
        MaterialGraph g;
        const NodeId used   = g.add_node(NodeKind::ConstColor);
        const NodeId unused = g.add_node(NodeKind::ConstColor);
        g.connect(used, g.output_node(), static_cast<uint32_t>(OutputSlot::BaseColor));
        const auto r = g.generate();
        check(has(r.glsl, "n" + std::to_string(used) + " ="), "a connected node is emitted");
        check(!has(r.glsl, "n" + std::to_string(unused) + " ="),
              "and a disconnected one is NOT");
    }

    // ---- 9. DETERMINISM ----------------------------------------------------
    // Compilation is skipped when the graph has not changed, so identical input
    // must give byte-identical output. Anything iterating a hash map in a
    // different order would break this and nothing else would notice.
    {
        MaterialGraph g;
        const NodeId t = g.add_node(NodeKind::TextureSample);
        const NodeId u = g.add_node(NodeKind::UV);
        const NodeId s = g.add_node(NodeKind::Saturate);
        g.connect(u, t, 0);
        g.connect(t, s, 0);
        g.connect(s, g.output_node(), static_cast<uint32_t>(OutputSlot::BaseColor));

        const std::string a = g.generate().glsl;
        const std::string b = g.generate().glsl;
        const std::string c = g.generate().glsl;
        check(a == b && b == c, "generating three times gives byte-identical GLSL");
        check(has(a, "texture(gs_tex0"), "and the texture sample reads its slot");
    }

    // ---- 10. removing a node takes its links with it ------------------------
    // A link to a deleted node would be chased during generation.
    {
        MaterialGraph g;
        const NodeId c = g.add_node(NodeKind::ConstColor);
        g.connect(c, g.output_node(), static_cast<uint32_t>(OutputSlot::BaseColor));
        g.remove_node(c);
        check(g.links().empty(), "deleting a node deletes the links that referenced it");
        const auto r = g.generate();
        check(r.ok, "and the graph still generates afterwards");
    }

    // ---- 11. the output node cannot be deleted ------------------------------
    {
        MaterialGraph g;
        const NodeId o = g.output_node();
        g.remove_node(o);
        check(g.node(o) != nullptr, "the output node survives a delete attempt");
    }

    // ---- 12. Lerp's blend factor does not widen the result ------------------
    // Lerping two floats by a vec3 t is not a vec3; getting this wrong turns a
    // scalar roughness into a colour and the shader stops compiling.
    {
        MaterialGraph g;
        const NodeId a = g.add_node(NodeKind::ConstFloat);
        const NodeId b = g.add_node(NodeKind::ConstFloat);
        const NodeId t = g.add_node(NodeKind::ConstColor);           // vec4 factor
        const NodeId l = g.add_node(NodeKind::Lerp);
        g.connect(a, l, 0);
        g.connect(b, l, 1);
        g.connect(t, l, 2);
        check(g.result_type(l) == ValueType::Float,
              "a Lerp of two floats stays a float regardless of the factor's type");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL shadergraph_check\n");
    return g_fail ? 1 : 0;
}
