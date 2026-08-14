// ============================================================================
// material_graph_panel — the material graph (4.1) on the generic node canvas.
//
// This file is the seam: NodeCanvas knows nothing about materials, MaterialGraph
// knows nothing about drawing, and everything material-specific lives here --
// node titles, the add menu, pin colours, per-node value widgets. That is the
// division the old logic_graph_panel did not have, which is why none of it
// could be reused for this.
// ============================================================================
#include "material_graph_panel.h"

#include "node_canvas.h"

#include <imgui.h>

#include <cstdint>
#include <string>
#include <vector>

namespace schizo::editor {

using namespace gws::shadergraph;

namespace {

// Pin colour by type, so a mismatch is visible before it is made rather than
// after it silently converts. Colour is the only channel available here -- the
// canvas deliberately does not know what a type is.
ImU32 type_color(ValueType t) {
    switch (t) {
        case ValueType::Float: return IM_COL32(150, 200, 150, 255);
        case ValueType::Vec2:  return IM_COL32(150, 190, 230, 255);
        case ValueType::Vec3:  return IM_COL32(230, 190, 130, 255);
        case ValueType::Vec4:  return IM_COL32(230, 150, 190, 255);
    }
    return IM_COL32(200, 200, 200, 255);
}

const char* node_title(NodeKind k) {
    switch (k) {
        case NodeKind::UV:            return "UV";
        case NodeKind::Time:          return "Time";
        case NodeKind::VertexColor:   return "Vertex Color";
        case NodeKind::ConstFloat:    return "Float";
        case NodeKind::ConstColor:    return "Color";
        case NodeKind::TextureSample: return "Texture Sample";
        case NodeKind::Add:           return "Add";
        case NodeKind::Multiply:      return "Multiply";
        case NodeKind::Lerp:          return "Lerp";
        case NodeKind::OneMinus:      return "One Minus";
        case NodeKind::Saturate:      return "Saturate";
        case NodeKind::Dot:           return "Dot";
        case NodeKind::Split:         return "Split";
        case NodeKind::Combine:       return "Combine";
        case NodeKind::Output:        return "Surface Output";
    }
    return "Node";
}

// Input labels per kind. Named rather than numbered because "A / B / T" on a
// Lerp is the difference between a graph you can read and one you have to
// experiment with.
const char* input_label(NodeKind k, uint32_t i) {
    switch (k) {
        case NodeKind::TextureSample: return "UV";
        case NodeKind::Lerp:          return i == 0 ? "A" : (i == 1 ? "B" : "T");
        case NodeKind::Add:
        case NodeKind::Multiply:
        case NodeKind::Dot:           return i == 0 ? "A" : "B";
        case NodeKind::Combine:       return i == 0 ? "R" : (i == 1 ? "G" : "B");
        case NodeKind::OneMinus:
        case NodeKind::Saturate:
        case NodeKind::Split:         return "In";
        case NodeKind::Output:        return output_slot_name(static_cast<OutputSlot>(i));
        default:                      return "";
    }
}

struct MenuEntry { const char* label; NodeKind kind; };

// Grouped the way someone building a material thinks, not the way the enum is
// declared.
const MenuEntry kInputs[] = {
    {"UV", NodeKind::UV}, {"Time", NodeKind::Time}, {"Vertex Color", NodeKind::VertexColor},
    {"Float", NodeKind::ConstFloat}, {"Color", NodeKind::ConstColor},
    {"Texture Sample", NodeKind::TextureSample},
};
const MenuEntry kMaths[] = {
    {"Add", NodeKind::Add}, {"Multiply", NodeKind::Multiply}, {"Lerp", NodeKind::Lerp},
    {"One Minus", NodeKind::OneMinus}, {"Saturate", NodeKind::Saturate}, {"Dot", NodeKind::Dot},
};
const MenuEntry kChannels[] = {
    {"Split", NodeKind::Split}, {"Combine", NodeKind::Combine},
};

}  // namespace

void draw_material_graph_panel(bool& open, MaterialGraph& graph, NodeCanvas& canvas,
                               std::string& out_glsl, bool& out_dirty) {
    if (!open) return;

    ImGui::SetNextWindowSize(ImVec2(980.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Material Graph", &open)) { ImGui::End(); return; }

    // ---- toolbar ----------------------------------------------------------
    const auto gen = graph.generate();
    if (gen.ok) {
        ImGui::TextDisabled("%u node(s) contribute", gen.emitted_nodes);
    } else {
        // Errors are shown here permanently rather than as a toast: a cycle
        // stays wrong until it is fixed, and a message that has already
        // disappeared cannot be acted on.
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", gen.error.c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy GLSL") && gen.ok) ImGui::SetClipboardText(gen.glsl.c_str());
    ImGui::SameLine();
    static bool show_source = false;
    ImGui::Checkbox("Show source", &show_source);

    if (show_source && gen.ok) {
        ImGui::BeginChild("##src", ImVec2(0.0f, 150.0f), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(gen.glsl.c_str());
        ImGui::EndChild();
    }

    // ---- the canvas -------------------------------------------------------
    canvas.begin("##matcanvas");

    for (const Node& n : graph.nodes()) {
        canvas.begin_node(n.id, node_title(n.kind), n.x, n.y);

        const uint32_t ins = input_count(n.kind);
        for (uint32_t i = 0; i < ins; ++i) {
            ValueType want = ValueType::Float;
            if (n.kind == NodeKind::Output) want = output_slot_type(static_cast<OutputSlot>(i));
            else if (n.kind == NodeKind::TextureSample) want = ValueType::Vec2;
            else want = graph.result_type(n.id);
            canvas.input_pin(i, input_label(n.kind, i), type_color(want));
        }
        if (n.kind != NodeKind::Output)
            canvas.output_pin("", type_color(graph.result_type(n.id)));

        canvas.end_node();
    }

    for (const Link& l : graph.links())
        canvas.link(l.src_node, l.dst_node, l.dst_input);

    canvas.end();

    // ---- interactions, each consumed once ---------------------------------
    if (auto moved = canvas.take_moved_node()) {
        if (Node* n = graph.node(moved->id)) { n->x = moved->x; n->y = moved->y; out_dirty = true; }
    }
    if (auto made = canvas.take_new_link()) {
        // connect() refuses cycles at the link stage only for self-links; a
        // longer cycle is caught by generate() and reported in the toolbar,
        // which is better than silently refusing an edit the user meant.
        if (graph.connect(made->src, made->dst, made->dst_input)) out_dirty = true;
    }
    if (auto del = canvas.take_deleted_node()) { graph.remove_node(*del); out_dirty = true; }

    // ---- add menu, at the click position ----------------------------------
    static float ctx_x = 0.0f, ctx_y = 0.0f;
    if (auto at = canvas.take_context_menu_at()) {
        ctx_x = at->first;
        ctx_y = at->second;
        ImGui::OpenPopup("##addnode");
    }
    if (ImGui::BeginPopup("##addnode")) {
        auto emit_group = [&](const char* title, const MenuEntry* e, size_t n) {
            if (!ImGui::BeginMenu(title)) return;
            for (size_t i = 0; i < n; ++i)
                if (ImGui::MenuItem(e[i].label)) {
                    graph.add_node(e[i].kind, ctx_x, ctx_y);
                    out_dirty = true;
                }
            ImGui::EndMenu();
        };
        emit_group("Input",    kInputs,   sizeof kInputs   / sizeof kInputs[0]);
        emit_group("Math",     kMaths,    sizeof kMaths    / sizeof kMaths[0]);
        emit_group("Channels", kChannels, sizeof kChannels / sizeof kChannels[0]);
        ImGui::EndPopup();
    }

    // ---- selected node's own values ---------------------------------------
    // In the panel rather than inside the node box: values live at a fixed
    // place instead of moving with the node and resizing it as you type.
    if (Node* sel = graph.node(canvas.selected_node())) {
        ImGui::Separator();
        ImGui::Text("%s", node_title(sel->kind));
        ImGui::SameLine();
        ImGui::TextDisabled("(Delete removes it)");
        switch (sel->kind) {
            case NodeKind::ConstFloat:
                if (ImGui::DragFloat("value", &sel->value[0], 0.01f)) out_dirty = true;
                break;
            case NodeKind::ConstColor:
                if (ImGui::ColorEdit4("color", sel->value)) out_dirty = true;
                break;
            case NodeKind::TextureSample: {
                int slot = static_cast<int>(sel->param_index);
                if (ImGui::DragInt("texture slot", &slot, 0.1f, 0, 7)) {
                    sel->param_index = static_cast<uint32_t>(slot < 0 ? 0 : slot);
                    out_dirty = true;
                }
                break;
            }
            case NodeKind::Split: {
                // Radios, not a combo: with four options a dropdown hides the
                // choices behind a click and people never find them.
                int ch = static_cast<int>(sel->param_index);
                bool changed = false;
                const char* names[4] = {"R", "G", "B", "A"};
                for (int i = 0; i < 4; ++i) {
                    if (i) ImGui::SameLine();
                    if (ImGui::RadioButton(names[i], &ch, i)) changed = true;
                }
                if (changed) { sel->param_index = static_cast<uint32_t>(ch); out_dirty = true; }
                break;
            }
            default: ImGui::TextDisabled("no parameters"); break;
        }
    }

    if (gen.ok) out_glsl = gen.glsl;
    ImGui::End();
}

}  // namespace schizo::editor
