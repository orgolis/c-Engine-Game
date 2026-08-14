#pragma once
// ============================================================================
// material_graph — a node graph that emits GLSL (4.1).
//
// The workflow plan calls this "the most impactful missing editor. Without it
// every game made here looks identical." That is the whole point: the renderer
// has exactly one fragment shader, so every surface in every project built with
// this engine shades the same way. A material graph is what makes two games
// look like two games.
//
// This half is deliberately free of ImGui and of Vulkan. Codegen is where the
// silent failures live -- a graph that emits statements in the wrong order, or
// silently drops a branch, produces a shader that compiles cleanly and renders
// something subtly wrong, which is the hardest kind of bug to attribute. It is
// pure data in / string out so it can be tested without a GPU.
//
// The engine cannot compile GLSL in-process (glslang links only under MSVC and
// this is a MinGW build), so the generated source is compiled by a bundled
// glslangValidator on save rather than per frame. That shapes the design:
// generation must be DETERMINISTIC, so the same graph produces byte-identical
// GLSL and an unchanged graph can skip the compile entirely.
// ============================================================================

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace gws::shadergraph {

// ---- types ----------------------------------------------------------------
// Deliberately few. GLSL's implicit conversions are a common source of "it
// compiled but the colour is wrong"; keeping the type set small makes the
// conversion rules small enough to state exactly.
enum class ValueType : uint8_t { Float, Vec2, Vec3, Vec4 };

inline const char* glsl_type(ValueType t) {
    switch (t) {
        case ValueType::Float: return "float";
        case ValueType::Vec2:  return "vec2";
        case ValueType::Vec3:  return "vec3";
        case ValueType::Vec4:  return "vec4";
    }
    return "float";
}

inline uint32_t component_count(ValueType t) {
    switch (t) {
        case ValueType::Float: return 1;
        case ValueType::Vec2:  return 2;
        case ValueType::Vec3:  return 3;
        case ValueType::Vec4:  return 4;
    }
    return 1;
}

// ---- nodes ----------------------------------------------------------------
enum class NodeKind : uint8_t {
    // inputs
    UV,             // vec2 — the surface's texture coordinates
    Time,           // float — seconds, for animated materials
    VertexColor,    // vec4

    // constants
    ConstFloat,
    ConstColor,     // vec4

    // sampling
    TextureSample,  // vec4 — `param_index` selects which bound texture

    // maths
    Add, Multiply, Lerp, OneMinus, Saturate, Dot,

    // channel plumbing
    Split,          // any -> float (channel selected by param_index)
    Combine,        // 3 floats -> vec3

    // output
    Output,         // the material's surface result
};

/// Which surface property an Output input drives. Chosen to match the existing
/// MaterialUniforms so a graph can drive the renderer that already exists,
/// rather than requiring a new lighting model on day one.
enum class OutputSlot : uint8_t { BaseColor, Metallic, Roughness, Emissive, Alpha, Count };

inline const char* output_slot_name(OutputSlot s) {
    switch (s) {
        case OutputSlot::BaseColor: return "base_color";
        case OutputSlot::Metallic:  return "metallic";
        case OutputSlot::Roughness: return "roughness";
        case OutputSlot::Emissive:  return "emissive";
        case OutputSlot::Alpha:     return "alpha";
        default: return "unknown";
    }
}

inline ValueType output_slot_type(OutputSlot s) {
    switch (s) {
        case OutputSlot::BaseColor:
        case OutputSlot::Emissive:  return ValueType::Vec3;
        default:                    return ValueType::Float;
    }
}

using NodeId = uint32_t;
inline constexpr NodeId kInvalidNode = 0xFFFFFFFFu;

struct Node {
    NodeId    id   = kInvalidNode;
    NodeKind  kind = NodeKind::ConstFloat;

    /// Literal payload: ConstFloat uses [0]; ConstColor uses all four.
    float     value[4] = {0, 0, 0, 0};

    /// Texture slot for TextureSample, channel index for Split.
    uint32_t  param_index = 0;

    /// Editor-only. Kept in the model so a graph round-trips through a file
    /// with its layout intact -- a graph that reopens as a pile of overlapping
    /// nodes in the corner is one people rearrange instead of using.
    float     x = 0.0f, y = 0.0f;
};

/// A connection into `dst_node`'s input `dst_input`, from `src_node`'s output.
/// Nodes here have exactly one output, so no source index is needed; that keeps
/// the link model small and is the reason Split exists as its own node.
struct Link {
    NodeId   src_node  = kInvalidNode;
    NodeId   dst_node  = kInvalidNode;
    uint32_t dst_input = 0;
};

/// How many inputs a node kind accepts.
inline uint32_t input_count(NodeKind k) {
    switch (k) {
        case NodeKind::UV:
        case NodeKind::Time:
        case NodeKind::VertexColor:
        case NodeKind::ConstFloat:
        case NodeKind::ConstColor:    return 0;
        case NodeKind::TextureSample: return 1;   // uv
        case NodeKind::OneMinus:
        case NodeKind::Saturate:
        case NodeKind::Split:         return 1;
        case NodeKind::Add:
        case NodeKind::Multiply:
        case NodeKind::Dot:           return 2;
        case NodeKind::Lerp:          return 3;   // a, b, t
        case NodeKind::Combine:       return 3;
        case NodeKind::Output:        return static_cast<uint32_t>(OutputSlot::Count);
    }
    return 0;
}

struct CodegenResult {
    bool        ok = false;
    std::string glsl;              // the generated function body
    std::string error;             // first problem found, in plain language
    uint32_t    emitted_nodes = 0; // nodes that actually contributed
};

class MaterialGraph {
public:
    MaterialGraph();

    NodeId add_node(NodeKind kind, float x = 0.0f, float y = 0.0f);
    void   remove_node(NodeId id);
    Node*  node(NodeId id);
    const Node* node(NodeId id) const;

    /// Connect src -> dst.input. Replaces any existing link on that input,
    /// because an input with two sources has no defined value and silently
    /// picking one is worse than refusing.
    bool connect(NodeId src, NodeId dst, uint32_t dst_input);
    void disconnect(NodeId dst, uint32_t dst_input);

    NodeId output_node() const { return output_; }
    const std::vector<Node>& nodes() const { return nodes_; }
    const std::vector<Link>& links() const { return links_; }

    /// The type a node produces, given what is currently connected to it.
    ValueType result_type(NodeId id) const;

    /// Emit GLSL for everything the Output node depends on.
    ///
    /// Only reachable nodes are emitted -- an editor accumulates disconnected
    /// experiments, and emitting them would put dead code in every shader and
    /// break the "unchanged graph, unchanged output" property that makes
    /// compile-skipping safe.
    CodegenResult generate() const;

private:
    NodeId source_of(NodeId dst, uint32_t input) const;
    bool   collect(NodeId id, std::vector<NodeId>& order,
                   std::unordered_map<NodeId, int>& state, std::string& error) const;

    std::vector<Node> nodes_;
    std::vector<Link> links_;
    NodeId            next_id_ = 0;
    NodeId            output_  = kInvalidNode;
};

}  // namespace gws::shadergraph
