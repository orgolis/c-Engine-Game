#include "logic_graph_panel.h"

#include "ecs_bridge.h"
#include "ecs/gameplay_logic.h"

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <string>

namespace schizo::editor {

namespace ecs = schizo::ecs;

namespace {

constexpr float kNodeW = 156.0f;
constexpr float kNodeH = 64.0f;

// Persistent interaction state (only one logic panel exists).
ImVec2 g_pan{40.0f, 40.0f};
int    g_link_from = 0;   // output-pin node id awaiting an input pin (0 = none)

const char* kind_label(ecs::LogicNodeKind k) {
    switch (k) {
        case ecs::LogicNodeKind::OnStart:    return "On Start";
        case ecs::LogicNodeKind::OnEvent:    return "On Event";
        case ecs::LogicNodeKind::OnKey:      return "On Key";
        case ecs::LogicNodeKind::OnKeyUp:    return "On Key Up";
        case ecs::LogicNodeKind::OnTick:     return "On Tick";
        case ecs::LogicNodeKind::OnFlag:     return "On Flag";
        case ecs::LogicNodeKind::EmitEvent:  return "Emit Event";
        case ecs::LogicNodeKind::SetFlag:    return "Set Flag";
        case ecs::LogicNodeKind::Log:        return "Log";
        case ecs::LogicNodeKind::ClearFlag:  return "Clear Flag";
        case ecs::LogicNodeKind::ToggleFlag: return "Toggle Flag";
        case ecs::LogicNodeKind::Branch:     return "Branch (if)";
        case ecs::LogicNodeKind::DoOnce:     return "Do Once";
        case ecs::LogicNodeKind::Delay:      return "Delay";
    }
    return "Node";
}
// Hint for the primary (param) text field.
const char* param_hint(ecs::LogicNodeKind k) {
    switch (k) {
        case ecs::LogicNodeKind::OnStart:    return "";
        case ecs::LogicNodeKind::OnEvent:    return "event name";
        case ecs::LogicNodeKind::OnKey:      return "GLFW key code";
        case ecs::LogicNodeKind::OnKeyUp:    return "GLFW key code";
        case ecs::LogicNodeKind::OnTick:     return "interval (s)";
        case ecs::LogicNodeKind::OnFlag:     return "flag key";
        case ecs::LogicNodeKind::EmitEvent:  return "event name";
        case ecs::LogicNodeKind::SetFlag:    return "flag key";
        case ecs::LogicNodeKind::Log:        return "message";
        case ecs::LogicNodeKind::ClearFlag:  return "flag key";
        case ecs::LogicNodeKind::ToggleFlag: return "flag key";
        case ecs::LogicNodeKind::Branch:     return "flag key";
        case ecs::LogicNodeKind::DoOnce:     return "";
        case ecs::LogicNodeKind::Delay:      return "seconds";
    }
    return "";
}
// Hint for the secondary (param2) field, or "" if the node has none.
const char* param2_hint(ecs::LogicNodeKind k) {
    switch (k) {
        case ecs::LogicNodeKind::SetFlag: return "value (int)";
        case ecs::LogicNodeKind::OnFlag:  return "cond e.g. ==1";
        case ecs::LogicNodeKind::Branch:  return "cond e.g. >=1";
        default: return "";
    }
}
float dist(const ImVec2& a, const ImVec2& b) { const float dx = a.x - b.x, dy = a.y - b.y; return std::sqrt(dx * dx + dy * dy); }

}  // namespace

void draw_logic_graph_panel(EcsSceneBridge& bridge, bool* open) {
    if (!ImGui::Begin("Logic Graph", open)) { ImGui::End(); return; }
    ecs::LogicGraph& g = bridge.logic_graph();

    ImGui::TextDisabled("Scene logic: EVENTS (blue) -> ACTIONS (purple) / BRANCH (teal, if-condition). "
                        "Nodes chain: On Event -> Branch -> Set Flag -> Log. Right-click canvas to add; "
                        "click an output pin then an input pin to wire; right-click a node to delete.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) { g = ecs::LogicGraph{}; g_link_from = 0; }

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 sz = ImGui::GetContentRegionAvail();
    if (sz.x < 60) sz.x = 60; if (sz.y < 60) sz.y = 60;
    const ImVec2 p1(p0.x + sz.x, p0.y + sz.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(28, 28, 33, 255));
    dl->AddRect(p0, p1, IM_COL32(60, 60, 70, 255));

    ImGui::InvisibleButton("##logic_canvas", sz,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool canvas_hovered = ImGui::IsItemHovered();
    const ImGuiIO& io = ImGui::GetIO();
    if (canvas_hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) { g_pan.x += io.MouseDelta.x; g_pan.y += io.MouseDelta.y; }
    const ImVec2 origin(p0.x + g_pan.x, p0.y + g_pan.y);

    // grid
    for (float x = std::fmod(g_pan.x, 32.0f); x < sz.x; x += 32.0f)
        dl->AddLine(ImVec2(p0.x + x, p0.y), ImVec2(p0.x + x, p1.y), IM_COL32(48, 48, 55, 255));
    for (float y = std::fmod(g_pan.y, 32.0f); y < sz.y; y += 32.0f)
        dl->AddLine(ImVec2(p0.x, p0.y + y), ImVec2(p1.x, p0.y + y), IM_COL32(48, 48, 55, 255));

    dl->PushClipRect(p0, p1, true);

    auto out_pin = [&](const ecs::LogicNode& n) { return ImVec2(origin.x + n.x + kNodeW + 6.0f, origin.y + n.y + kNodeH * 0.5f); };
    auto in_pin  = [&](const ecs::LogicNode& n) { return ImVec2(origin.x + n.x - 6.0f, origin.y + n.y + kNodeH * 0.5f); };

    // links
    for (const auto& l : g.links) {
        ecs::LogicNode* a = g.find(l.from); ecs::LogicNode* b = g.find(l.to);
        if (!a || !b) continue;
        const ImVec2 s = out_pin(*a), e = in_pin(*b);
        dl->AddBezierCubic(s, ImVec2(s.x + 50, s.y), ImVec2(e.x - 50, e.y), e, IM_COL32(210, 200, 120, 255), 2.5f);
    }
    if (g_link_from != 0) {
        if (ecs::LogicNode* a = g.find(g_link_from)) {
            const ImVec2 s = out_pin(*a);
            dl->AddBezierCubic(s, ImVec2(s.x + 50, s.y), ImVec2(io.MousePos.x - 50, io.MousePos.y), io.MousePos, IM_COL32(210, 200, 120, 180), 2.0f);
        } else g_link_from = 0;
    }

    // nodes
    int delete_node = 0;
    bool consumed_click = false;
    for (auto& n : g.nodes) {
        const ImVec2 tl(origin.x + n.x, origin.y + n.y);
        const ImVec2 br(tl.x + kNodeW, tl.y + kNodeH);
        const bool ev  = ecs::logic_is_event(n.kind);
        const bool brn = static_cast<uint32_t>(n.kind) >= 200;   // flow nodes (Branch/Do Once/Delay)
        // Blue = event, teal = flow/branch, purple = action.
        const ImU32 body  = ev  ? IM_COL32(46, 74, 112, 255)
                          : brn ? IM_COL32(44, 92, 84, 255)
                                : IM_COL32(86, 58, 92, 255);
        const ImU32 title = ev  ? IM_COL32(60, 96, 150, 255)
                          : brn ? IM_COL32(60, 124, 112, 255)
                                : IM_COL32(116, 78, 124, 255);
        dl->AddRectFilled(tl, br, body, 5.0f);
        dl->AddRectFilled(tl, ImVec2(br.x, tl.y + 20), title, 5.0f);
        dl->AddRect(tl, br, IM_COL32(180, 180, 200, 255), 5.0f);
        dl->AddText(ImVec2(tl.x + 8, tl.y + 4), IM_COL32_WHITE, kind_label(n.kind));

        // param field(s)
        ImGui::PushID(n.id);
        if (param_hint(n.kind)[0]) {
            ImGui::SetCursorScreenPos(ImVec2(tl.x + 8, tl.y + 26));
            ImGui::SetNextItemWidth(kNodeW - 16);
            char buf[96]; std::snprintf(buf, sizeof buf, "%s", n.param.c_str());
            if (ImGui::InputTextWithHint("##p", param_hint(n.kind), buf, sizeof buf)) n.param = buf;
        }
        if (param2_hint(n.kind)[0]) {
            ImGui::SetCursorScreenPos(ImVec2(tl.x + 8, tl.y + 26 + 22));
            ImGui::SetNextItemWidth(kNodeW - 16);
            char v[32]; std::snprintf(v, sizeof v, "%s", n.param2.c_str());
            if (ImGui::InputTextWithHint("##v", param2_hint(n.kind), v, sizeof v)) n.param2 = v;
        }
        ImGui::PopID();

        // drag handle (title bar)
        ImGui::SetCursorScreenPos(tl);
        ImGui::InvisibleButton((std::string("nh") + std::to_string(n.id)).c_str(), ImVec2(kNodeW, 20));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) { n.x += io.MouseDelta.x; n.y += io.MouseDelta.y; }
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup((std::string("npop") + std::to_string(n.id)).c_str());
        if (ImGui::BeginPopup((std::string("npop") + std::to_string(n.id)).c_str())) {
            if (ImGui::MenuItem("Delete node")) delete_node = n.id;
            ImGui::EndPopup();
        }

        // pins: click an OUTPUT pin (right), then an INPUT pin (left), to wire.
        // Events have no input (runtime-fired); every node has an output so
        // actions and branches chain (On Event -> Branch -> Set Flag -> Log).
        if (ecs::logic_has_input(n.kind)) {
            const ImVec2 ip = in_pin(n);
            dl->AddCircleFilled(ip, 5.0f, IM_COL32(150, 230, 150, 255));
            if (!consumed_click && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && dist(io.MousePos, ip) < 9.0f) {
                if (g_link_from != 0 && g_link_from != n.id) { g.link(g_link_from, n.id); g_link_from = 0; }
                consumed_click = true;
            }
        }
        {
            const ImVec2 op = out_pin(n);
            dl->AddCircleFilled(op, 5.0f, g_link_from == n.id ? IM_COL32(255, 240, 120, 255) : IM_COL32(230, 210, 130, 255));
            if (!consumed_click && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && dist(io.MousePos, op) < 9.0f) { g_link_from = n.id; consumed_click = true; }
        }
    }
    if (delete_node) g.remove_node(delete_node);

    dl->PopClipRect();

    // click empty canvas cancels a pending link
    if (g_link_from != 0 && canvas_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !consumed_click) g_link_from = 0;

    // right-click empty canvas -> add-node menu
    if (canvas_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("##logic_add");
    if (ImGui::BeginPopup("##logic_add")) {
        const float ax = io.MousePos.x - origin.x, ay = io.MousePos.y - origin.y;
        ImGui::TextDisabled("Events");
        if (ImGui::MenuItem("On Start"))    g.add_node(ecs::LogicNodeKind::OnStart, ax, ay);
        if (ImGui::MenuItem("On Event"))    g.add_node(ecs::LogicNodeKind::OnEvent, ax, ay);
        if (ImGui::MenuItem("On Key"))      g.add_node(ecs::LogicNodeKind::OnKey, ax, ay);
        if (ImGui::MenuItem("On Key Up"))   g.add_node(ecs::LogicNodeKind::OnKeyUp, ax, ay);
        if (ImGui::MenuItem("On Tick"))     g.add_node(ecs::LogicNodeKind::OnTick, ax, ay);
        if (ImGui::MenuItem("On Flag"))     g.add_node(ecs::LogicNodeKind::OnFlag, ax, ay);
        ImGui::Separator();
        ImGui::TextDisabled("Actions");
        if (ImGui::MenuItem("Emit Event"))  g.add_node(ecs::LogicNodeKind::EmitEvent, ax, ay);
        if (ImGui::MenuItem("Set Flag"))    g.add_node(ecs::LogicNodeKind::SetFlag, ax, ay);
        if (ImGui::MenuItem("Clear Flag"))  g.add_node(ecs::LogicNodeKind::ClearFlag, ax, ay);
        if (ImGui::MenuItem("Toggle Flag")) g.add_node(ecs::LogicNodeKind::ToggleFlag, ax, ay);
        if (ImGui::MenuItem("Log"))         g.add_node(ecs::LogicNodeKind::Log, ax, ay);
        ImGui::Separator();
        ImGui::TextDisabled("Flow");
        if (ImGui::MenuItem("Branch (if)")) g.add_node(ecs::LogicNodeKind::Branch, ax, ay);
        if (ImGui::MenuItem("Do Once"))     g.add_node(ecs::LogicNodeKind::DoOnce, ax, ay);
        if (ImGui::MenuItem("Delay"))       g.add_node(ecs::LogicNodeKind::Delay, ax, ay);
        ImGui::EndPopup();
    }

    ImGui::End();
}

}  // namespace schizo::editor
