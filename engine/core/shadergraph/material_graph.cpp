// ============================================================================
// material_graph — see material_graph.h.
// ============================================================================
#include "material_graph.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <sstream>

namespace gws::shadergraph {

namespace {

// Float literals are printed with enough digits to round-trip and ALWAYS with a
// decimal point. "1" is an int in GLSL and `1 / 2` is 0, which is the classic
// way a generated shader compiles and quietly computes the wrong thing.
std::string flt(float v) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.6g", static_cast<double>(v));
    std::string s(buf);
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos &&
        s.find("inf") == std::string::npos && s.find("nan") == std::string::npos)
        s += ".0";
    return s;
}

std::string var_name(NodeId id) { return "n" + std::to_string(id); }

// Convert `expr` of type `from` to type `to`.
//
// Written out rather than left to GLSL's implicit rules, because those rules
// are the reason a graph can compile and shade wrongly: assigning a float to a
// vec3 is legal and broadcasts, dropping components is not, and the difference
// only shows up as a colour being subtly off.
std::string convert(const std::string& expr, ValueType from, ValueType to) {
    if (from == to) return expr;

    const uint32_t fc = component_count(from);
    const uint32_t tc = component_count(to);

    if (fc == 1) {                                   // broadcast a scalar
        if (to == ValueType::Vec2) return "vec2(" + expr + ")";
        if (to == ValueType::Vec3) return "vec3(" + expr + ")";
        return "vec4(" + expr + ")";
    }
    if (tc == 1) return "(" + expr + ").x";          // take the first component

    if (tc < fc) {                                   // narrow by swizzle
        static const char* sw[5] = {"", "x", "xy", "xyz", "xyzw"};
        return "(" + expr + ")." + sw[tc];
    }
    // widen: pad with 0, and alpha with 1 so a vec3 colour promoted to vec4 is
    // opaque rather than invisible.
    if (from == ValueType::Vec2 && to == ValueType::Vec3) return "vec3(" + expr + ", 0.0)";
    if (from == ValueType::Vec2 && to == ValueType::Vec4) return "vec4(" + expr + ", 0.0, 1.0)";
    if (from == ValueType::Vec3 && to == ValueType::Vec4) return "vec4(" + expr + ", 1.0)";
    return expr;
}

// The value an unconnected input contributes.
//
// Chosen to be the IDENTITY for each operation rather than zero: an unconnected
// Multiply input defaulting to 0 turns the whole branch black, which reads as
// "the graph is broken" when the user has simply not wired one socket yet.
std::string default_input(NodeKind kind, uint32_t input, ValueType want) {
    const char* v = "0.0";
    switch (kind) {
        case NodeKind::Multiply: v = "1.0"; break;
        case NodeKind::Lerp:     v = (input == 2) ? "0.0" : "0.0"; break;
        default: break;
    }
    return convert(v, ValueType::Float, want);
}

}  // namespace

MaterialGraph::MaterialGraph() {
    // Every graph has exactly one Output, created up front. A graph without one
    // cannot generate anything, and letting the user delete it would make an
    // unrecoverable document.
    output_ = add_node(NodeKind::Output, 520.0f, 180.0f);
}

void MaterialGraph::clear() {
    nodes_.clear();
    links_.clear();
    next_id_ = 0;
    output_  = add_node(NodeKind::Output, 520.0f, 180.0f);
}

NodeId MaterialGraph::add_node(NodeKind kind, float x, float y) {
    Node n;
    n.id   = next_id_++;
    n.kind = kind;
    n.x    = x;
    n.y    = y;
    if (kind == NodeKind::ConstColor) { n.value[0] = n.value[1] = n.value[2] = n.value[3] = 1.0f; }
    if (kind == NodeKind::ConstFloat) { n.value[0] = 1.0f; }
    nodes_.push_back(n);
    return n.id;
}

void MaterialGraph::remove_node(NodeId id) {
    if (id == output_) return;                       // see the constructor
    nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(),
                                [id](const Node& n) { return n.id == id; }),
                 nodes_.end());
    // Links referencing it go too, or generate() would chase a dangling id.
    links_.erase(std::remove_if(links_.begin(), links_.end(),
                                [id](const Link& l) {
                                    return l.src_node == id || l.dst_node == id;
                                }),
                 links_.end());
}

Node* MaterialGraph::node(NodeId id) {
    for (Node& n : nodes_) if (n.id == id) return &n;
    return nullptr;
}
const Node* MaterialGraph::node(NodeId id) const {
    for (const Node& n : nodes_) if (n.id == id) return &n;
    return nullptr;
}

bool MaterialGraph::connect(NodeId src, NodeId dst, uint32_t dst_input) {
    const Node* s = node(src);
    const Node* d = node(dst);
    if (!s || !d) return false;
    if (src == dst) return false;                    // a node feeding itself
    if (dst_input >= input_count(d->kind)) return false;

    disconnect(dst, dst_input);                      // one source per input
    links_.push_back({src, dst, dst_input});
    return true;
}

void MaterialGraph::disconnect(NodeId dst, uint32_t dst_input) {
    links_.erase(std::remove_if(links_.begin(), links_.end(),
                                [&](const Link& l) {
                                    return l.dst_node == dst && l.dst_input == dst_input;
                                }),
                 links_.end());
}

NodeId MaterialGraph::source_of(NodeId dst, uint32_t input) const {
    for (const Link& l : links_)
        if (l.dst_node == dst && l.dst_input == input) return l.src_node;
    return kInvalidNode;
}

ValueType MaterialGraph::result_type(NodeId id) const {
    const Node* n = node(id);
    if (!n) return ValueType::Float;
    switch (n->kind) {
        case NodeKind::UV:            return ValueType::Vec2;
        case NodeKind::Time:          return ValueType::Float;
        case NodeKind::VertexColor:   return ValueType::Vec4;
        case NodeKind::ConstFloat:    return ValueType::Float;
        case NodeKind::ConstColor:    return ValueType::Vec4;
        case NodeKind::TextureSample: return ValueType::Vec4;
        case NodeKind::Dot:           return ValueType::Float;
        case NodeKind::Split:         return ValueType::Float;
        case NodeKind::Combine:       return ValueType::Vec3;
        case NodeKind::Output:        return ValueType::Vec4;

        // Maths nodes take the type of their widest input, so a vec3 * float
        // stays a vec3 -- the behaviour people expect from a tint.
        case NodeKind::Add:
        case NodeKind::Multiply:
        case NodeKind::Lerp: {
            ValueType widest = ValueType::Float;
            for (uint32_t i = 0; i < input_count(n->kind); ++i) {
                const NodeId s = source_of(id, i);
                if (s == kInvalidNode) continue;
                // Lerp's third input is the blend factor and must not widen the
                // result: lerping two floats by a vec3 t is not a vec3.
                if (n->kind == NodeKind::Lerp && i == 2) continue;
                const ValueType t = result_type(s);
                if (component_count(t) > component_count(widest)) widest = t;
            }
            return widest;
        }
        case NodeKind::OneMinus:
        case NodeKind::Saturate: {
            const NodeId s = source_of(id, 0);
            return s == kInvalidNode ? ValueType::Float : result_type(s);
        }
    }
    return ValueType::Float;
}

// Depth-first post-order with cycle detection.
//
// state: 0 = unvisited, 1 = on the current path, 2 = done. Reaching a node that
// is on the current path is a cycle, and it must be REPORTED rather than
// followed -- an editor makes cycles trivially easy to create by dragging one
// link, and the alternative to detecting them is a hung editor.
bool MaterialGraph::collect(NodeId id, std::vector<NodeId>& order,
                            std::unordered_map<NodeId, int>& state,
                            std::string& error) const {
    auto it = state.find(id);
    if (it != state.end()) {
        if (it->second == 1) {
            error = "the graph contains a cycle (node " + std::to_string(id) +
                    " feeds back into itself)";
            return false;
        }
        return true;                                  // already emitted
    }
    const Node* n = node(id);
    if (!n) {
        error = "link points at a node that no longer exists";
        return false;
    }

    state[id] = 1;
    for (uint32_t i = 0; i < input_count(n->kind); ++i) {
        const NodeId s = source_of(id, i);
        if (s == kInvalidNode) continue;
        if (!collect(s, order, state, error)) return false;
    }
    state[id] = 2;
    order.push_back(id);                              // after its inputs
    return true;
}

CodegenResult MaterialGraph::generate() const {
    CodegenResult r;
    if (output_ == kInvalidNode || !node(output_)) {
        r.error = "the graph has no output node";
        return r;
    }

    std::vector<NodeId> order;
    std::unordered_map<NodeId, int> state;
    if (!collect(output_, order, state, r.error)) return r;

    std::ostringstream out;

    // An input expression, converted to the type this node needs.
    auto in_expr = [&](const Node& n, uint32_t i, ValueType want) {
        const NodeId s = source_of(n.id, i);
        if (s == kInvalidNode) return default_input(n.kind, i, want);
        return convert(var_name(s), result_type(s), want);
    };

    for (NodeId id : order) {
        const Node& n = *node(id);
        if (n.kind == NodeKind::Output) continue;     // emitted separately below

        const ValueType t  = result_type(id);
        const std::string v = var_name(id);
        out << "    " << glsl_type(t) << ' ' << v << " = ";

        switch (n.kind) {
            case NodeKind::UV:          out << "gs_uv"; break;
            case NodeKind::Time:        out << "gs_time"; break;
            case NodeKind::VertexColor: out << "gs_vertex_color"; break;
            case NodeKind::ConstFloat:  out << flt(n.value[0]); break;
            case NodeKind::ConstColor:
                out << "vec4(" << flt(n.value[0]) << ", " << flt(n.value[1]) << ", "
                    << flt(n.value[2]) << ", " << flt(n.value[3]) << ')';
                break;
            case NodeKind::TextureSample:
                out << "texture(gs_tex" << n.param_index << ", "
                    << in_expr(n, 0, ValueType::Vec2) << ')';
                break;
            case NodeKind::Add:
                out << in_expr(n, 0, t) << " + " << in_expr(n, 1, t);
                break;
            case NodeKind::Multiply:
                out << in_expr(n, 0, t) << " * " << in_expr(n, 1, t);
                break;
            case NodeKind::Lerp:
                out << "mix(" << in_expr(n, 0, t) << ", " << in_expr(n, 1, t) << ", "
                    << in_expr(n, 2, ValueType::Float) << ')';
                break;
            case NodeKind::OneMinus:
                out << convert("1.0", ValueType::Float, t) << " - " << in_expr(n, 0, t);
                break;
            case NodeKind::Saturate:
                out << "clamp(" << in_expr(n, 0, t) << ", "
                    << convert("0.0", ValueType::Float, t) << ", "
                    << convert("1.0", ValueType::Float, t) << ')';
                break;
            case NodeKind::Dot: {
                // Both operands must share a type for dot(); widen to the wider.
                const NodeId a = source_of(id, 0), b = source_of(id, 1);
                ValueType common = ValueType::Float;
                if (a != kInvalidNode) common = result_type(a);
                if (b != kInvalidNode &&
                    component_count(result_type(b)) > component_count(common))
                    common = result_type(b);
                out << "dot(" << in_expr(n, 0, common) << ", " << in_expr(n, 1, common) << ')';
                break;
            }
            case NodeKind::Split: {
                static const char* ch[4] = {"x", "y", "z", "w"};
                const NodeId s = source_of(id, 0);
                const ValueType st = s == kInvalidNode ? ValueType::Float : result_type(s);
                const uint32_t idx = std::min<uint32_t>(n.param_index,
                                                        component_count(st) - 1);
                out << (s == kInvalidNode ? std::string("0.0")
                                          : "(" + var_name(s) + ")." + ch[idx]);
                break;
            }
            case NodeKind::Combine:
                out << "vec3(" << in_expr(n, 0, ValueType::Float) << ", "
                    << in_expr(n, 1, ValueType::Float) << ", "
                    << in_expr(n, 2, ValueType::Float) << ')';
                break;
            case NodeKind::Output: break;
        }
        out << ";\n";
        ++r.emitted_nodes;
    }

    // The output assignments, in a fixed slot order so the generated source is
    // deterministic -- an unchanged graph must produce byte-identical GLSL, or
    // skipping the compile step is unsafe.
    const Node& on = *node(output_);
    for (uint32_t i = 0; i < static_cast<uint32_t>(OutputSlot::Count); ++i) {
        const auto slot = static_cast<OutputSlot>(i);
        const ValueType want = output_slot_type(slot);
        const NodeId s = source_of(output_, i);
        out << "    gs_out_" << output_slot_name(slot) << " = ";
        if (s == kInvalidNode) {
            // Unconnected slots keep the engine's existing defaults rather than
            // going black, so a half-built graph still renders something.
            switch (slot) {
                case OutputSlot::BaseColor: out << "vec3(0.8)"; break;
                case OutputSlot::Metallic:  out << "0.0";       break;
                case OutputSlot::Roughness: out << "1.0";       break;
                case OutputSlot::Emissive:  out << "vec3(0.0)"; break;
                default:                    out << "1.0";       break;
            }
        } else {
            out << convert(var_name(s), result_type(s), want);
        }
        out << ";\n";
    }
    (void)on;

    r.glsl = out.str();
    r.ok   = true;
    return r;
}

}  // namespace gws::shadergraph
