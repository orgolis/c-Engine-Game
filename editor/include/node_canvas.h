#pragma once
// ============================================================================
// node_canvas — a graph editor surface that knows nothing about your nodes.
//
// Three Phase 4 items need one of these: the material graph (4.1), the timeline
// (4.4) and the animation state machine (4.6).
//
// The VFX editor (4.3) was expected to be a fourth and is NOT. It turned out to
// be an ordered stack of modules per stage -- a LIST, whose order is semantic --
// so it is drawn as a stack panel (vfx_stack_panel.h). A canvas would earn its
// place there only one level down, if a module's parameters ever become
// computed rather than constant. Building it here to match the item's title
// would have been the wrong UI for the architecture.
//
// The engine
// already had a node editor -- logic_graph_panel.cpp -- but it is 255 lines
// welded to ecs::LogicNodeKind: it draws logic node titles, it enumerates logic
// node kinds in its context menu, and its link model is logic-specific. Nothing
// of it can be reused, which is why the plan's claim that "the hard half is
// solved" did not survive being looked at.
//
// So this owns the SURFACE and nothing else: pan, zoom, node boxes, pins, link
// dragging, selection, hit-testing. It never learns what a node means. Callers
// describe what exists and read back what the user did.
//
// Immediate mode, matching the rest of the editor:
//
//     canvas.begin("##mat");
//     for (auto& n : graph.nodes()) {
//         canvas.begin_node(n.id, title_of(n), n.x, n.y);
//         for (...) canvas.input_pin(i, label, colour);
//         canvas.output_pin(label, colour);
//         canvas.end_node();
//     }
//     for (auto& l : graph.links()) canvas.link(l.src_node, l.dst_node, l.dst_input);
//     canvas.end();
//
//     if (auto made = canvas.take_new_link())   graph.connect(...);
//     if (auto moved = canvas.take_moved_node()) { ... }
//
// Interactions are TAKEN rather than polled, so each one is consumed exactly
// once. A "did the user connect something" flag that stays true for a frame is
// how an editor makes the same edit twice and puts two entries in the undo
// stack for one drag.
// ============================================================================

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct ImVec2;

namespace schizo::editor {

/// Screen <-> canvas conversion, split out because it is pure arithmetic and
/// therefore the part of a graph editor that can be unit-tested. Everything
/// that feels wrong in a node editor -- clicks landing beside the cursor, zoom
/// walking away from the pointer -- is a bug in these four lines.
struct CanvasView {
    float pan_x = 0.0f, pan_y = 0.0f;
    float zoom  = 1.0f;

    float to_screen_x(float wx, float origin_x) const { return origin_x + (wx + pan_x) * zoom; }
    float to_screen_y(float wy, float origin_y) const { return origin_y + (wy + pan_y) * zoom; }
    float to_world_x(float sx, float origin_x)  const { return (sx - origin_x) / zoom - pan_x; }
    float to_world_y(float sy, float origin_y)  const { return (sy - origin_y) / zoom - pan_y; }

    /// Zoom about a fixed screen point, so the thing under the cursor stays
    /// under the cursor. Zooming about the canvas origin instead is the single
    /// most common way a node editor feels broken: the graph slides away as you
    /// scroll and you chase it.
    void zoom_at(float screen_x, float screen_y, float origin_x, float origin_y,
                 float factor, float min_zoom = 0.25f, float max_zoom = 3.0f);
};

struct NewLink   { uint32_t src; uint32_t dst; uint32_t dst_input; };
struct MovedNode { uint32_t id;  float x;      float y; };

class NodeCanvas {
public:
    /// Begin the canvas inside the current ImGui window. `size` of 0 fills the
    /// available region.
    void begin(const char* id, float width = 0.0f, float height = 0.0f);
    void end();

    /// Start a node at canvas coordinates. Returns true if it is on screen --
    /// callers may skip describing pins for nodes that are not, though it is
    /// only worth doing for large graphs.
    bool begin_node(uint32_t id, const std::string& title, float x, float y,
                    uint32_t title_color = 0);
    void end_node();

    /// Declare an input pin. `index` is the caller's own input index and comes
    /// straight back in NewLink, so the canvas never needs to know what it means.
    void input_pin(uint32_t index, const std::string& label, uint32_t color);
    void output_pin(const std::string& label, uint32_t color);

    /// Draw an existing connection.
    void link(uint32_t src_node, uint32_t dst_node, uint32_t dst_input);

    /// One-shot interaction results.
    std::optional<NewLink>   take_new_link();
    std::optional<MovedNode> take_moved_node();
    std::optional<uint32_t>  take_deleted_node();
    /// Canvas position of the last right-click, for placing a new node where
    /// the user actually pointed rather than at the origin.
    std::optional<std::pair<float, float>> take_context_menu_at();

    uint32_t selected_node() const { return selected_; }
    void     set_selected(uint32_t id) { selected_ = id; }

    CanvasView&       view()       { return view_; }
    const CanvasView& view() const { return view_; }

private:
    struct PinSlot { uint32_t index; float sx, sy; uint32_t color; };
    struct NodeRec {
        uint32_t id;
        float    sx, sy, w, h;          // screen rect
        float    out_x, out_y;          // output pin centre
        std::vector<PinSlot> inputs;
    };

    NodeRec* find(uint32_t id);

    CanvasView view_;
    float      origin_x_ = 0.0f, origin_y_ = 0.0f;   // canvas top-left, screen space
    float      width_ = 0.0f, height_ = 0.0f;

    std::vector<NodeRec> nodes_;
    NodeRec*   current_ = nullptr;
    uint32_t   cur_input_row_ = 0;

    uint32_t   selected_ = 0xFFFFFFFFu;
    uint32_t   dragging_ = 0xFFFFFFFFu;      // node being moved
    uint32_t   link_from_ = 0xFFFFFFFFu;     // output pin awaiting an input

    std::optional<NewLink>   new_link_;
    std::optional<MovedNode> moved_;
    std::optional<uint32_t>  deleted_;
    std::optional<std::pair<float, float>> ctx_at_;
};

}  // namespace schizo::editor
