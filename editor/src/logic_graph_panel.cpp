#include "logic_graph_panel.h"

#include "ecs_bridge.h"
#include "ecs/gameplay_logic.h"

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <cstdint>

namespace schizo::editor {

namespace ecs = schizo::ecs;

namespace {

constexpr float kNodeW = 156.0f;
constexpr float kNodeH = 64.0f;

// Persistent interaction state (only one logic panel exists).
ImVec2 g_pan{40.0f, 40.0f};
int    g_link_from = 0;   // EXEC output-pin node id awaiting an exec input pin (0 = none)
int    g_data_from = 0;   // DATA output-pin node id awaiting a data input pin (0 = none)

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
        case ecs::LogicNodeKind::SetVar:        return "Set Variable";
        case ecs::LogicNodeKind::LiteralBool:   return "Bool";
        case ecs::LogicNodeKind::LiteralInt:    return "Int";
        case ecs::LogicNodeKind::LiteralFloat:  return "Float";
        case ecs::LogicNodeKind::LiteralString: return "String";
        case ecs::LogicNodeKind::GetVar:        return "Get Variable";
        case ecs::LogicNodeKind::Add:           return "Add (+)";
        case ecs::LogicNodeKind::Sub:           return "Subtract (-)";
        case ecs::LogicNodeKind::Mul:           return "Multiply (x)";
        case ecs::LogicNodeKind::Div:           return "Divide (/)";
        case ecs::LogicNodeKind::Compare:       return "Compare";
        case ecs::LogicNodeKind::AndNode:       return "And";
        case ecs::LogicNodeKind::OrNode:        return "Or";
        case ecs::LogicNodeKind::NotNode:       return "Not";
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
        case ecs::LogicNodeKind::SetVar:        return "variable";
        case ecs::LogicNodeKind::GetVar:        return "variable";
        case ecs::LogicNodeKind::LiteralBool:   return "0 or 1";
        case ecs::LogicNodeKind::LiteralInt:    return "integer";
        case ecs::LogicNodeKind::LiteralFloat:  return "number";
        case ecs::LogicNodeKind::LiteralString: return "text";
        default: return "";   // math / logic nodes have no text field
    }
    return "";
}
// Hint for the secondary (param2) field, or "" if the node has none.
const char* param2_hint(ecs::LogicNodeKind k) {
    switch (k) {
        case ecs::LogicNodeKind::SetFlag: return "value (int)";
        case ecs::LogicNodeKind::OnFlag:  return "cond e.g. ==1";
        case ecs::LogicNodeKind::Branch:  return "cond e.g. >=1";
        case ecs::LogicNodeKind::Compare: return "op: == != > < >= <=";
        default: return "";
    }
}
// How many DATA input pins a node exposes on its left edge (below the exec pin).
// Exec actions expose one optional value pin that overrides the authored param;
// math/logic nodes pull their operands from these pins.
int data_input_count(ecs::LogicNodeKind k) {
    switch (k) {
        case ecs::LogicNodeKind::Add: case ecs::LogicNodeKind::Sub:
        case ecs::LogicNodeKind::Mul: case ecs::LogicNodeKind::Div:
        case ecs::LogicNodeKind::Compare:
        case ecs::LogicNodeKind::AndNode: case ecs::LogicNodeKind::OrNode:
            return 2;
        case ecs::LogicNodeKind::NotNode:
            return 1;
        case ecs::LogicNodeKind::SetFlag: case ecs::LogicNodeKind::SetVar:
        case ecs::LogicNodeKind::EmitEvent: case ecs::LogicNodeKind::Log:
        case ecs::LogicNodeKind::Branch:
            return 1;
        default:
            return 0;
    }
}
// Label shown when hovering a data input pin (slot is 1-based).
const char* data_input_label(ecs::LogicNodeKind k, int slot) {
    switch (k) {
        case ecs::LogicNodeKind::SetFlag: case ecs::LogicNodeKind::SetVar: return "value";
        case ecs::LogicNodeKind::EmitEvent: return "event name";
        case ecs::LogicNodeKind::Log:       return "text";
        case ecs::LogicNodeKind::Branch:    return "condition (bool)";
        case ecs::LogicNodeKind::NotNode:   return "in";
        default:                            return slot == 1 ? "A" : "B";
    }
}
float dist(const ImVec2& a, const ImVec2& b) { const float dx = a.x - b.x, dy = a.y - b.y; return std::sqrt(dx * dx + dy * dy); }

}  // namespace

void apply_logic_graph_text(EcsSceneBridge& bridge, const std::string& text) {
    bridge.logic_graph() = ecs::logic_from_text(text);
}

void draw_logic_graph_panel(EcsSceneBridge& bridge, bool* open,
                            EditCoalescer* coalescer,
                            const std::function<void(const CoalescedEdit&)>& on_edit) {
    if (!ImGui::Begin("Logic Graph", open)) { ImGui::End(); return; }
    ecs::LogicGraph& g = bridge.logic_graph();

    // Snapshot before the widgets run: they edit the graph in place, so this is
    // the only moment the pre-edit state exists.
    std::vector<uint8_t> graph_before;
    if (coalescer) {
        const std::string t = ecs::logic_to_text(g);
        graph_before.assign(t.begin(), t.end());
        coalescer->observe(graph_before);
    }

    ImGui::TextDisabled("EVENTS (blue) -> ACTIONS (purple) / FLOW (teal). DATA nodes (slate) feed values into "
                        "cyan input pins: Get Variable / Literal / Math / Compare -> a Set Flag value, a Branch "
                        "condition, etc. Gold pins = exec flow, cyan pins = values. Right-click canvas to add.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) { g = ecs::LogicGraph{}; g_link_from = 0; g_data_from = 0; }

    // ---- Variables: graph-scoped typed values, read/written by Get/Set nodes ----
    if (ImGui::CollapsingHeader("Variables")) {
        int remove_var = -1;
        for (int i = 0; i < static_cast<int>(g.variables.size()); ++i) {
            auto& v = g.variables[i];
            ImGui::PushID(i);
            ImGui::SetNextItemWidth(110);
            char nm[64]; std::snprintf(nm, sizeof nm, "%s", v.name.c_str());
            if (ImGui::InputTextWithHint("##vn", "name", nm, sizeof nm)) v.name = nm;
            ImGui::SameLine();
            const int t = static_cast<int>(v.value.type);
            if (ImGui::RadioButton("Bool",  t == 0)) v.value.type = ecs::LogicValueType::Bool;   ImGui::SameLine();
            if (ImGui::RadioButton("Int",   t == 1)) v.value.type = ecs::LogicValueType::Int;    ImGui::SameLine();
            if (ImGui::RadioButton("Float", t == 2)) v.value.type = ecs::LogicValueType::Float;  ImGui::SameLine();
            if (ImGui::RadioButton("Str",   t == 3)) v.value.type = ecs::LogicValueType::String; ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            switch (v.value.type) {
                case ecs::LogicValueType::Bool:   { bool b = v.value.i != 0; if (ImGui::Checkbox("##vv", &b)) v.value.i = b ? 1 : 0; break; }
                case ecs::LogicValueType::Int:    { ImGui::InputInt("##vv", &v.value.i, 0, 0); break; }
                case ecs::LogicValueType::Float:  { ImGui::InputFloat("##vv", &v.value.f, 0.0f, 0.0f, "%.3f"); break; }
                case ecs::LogicValueType::String: { char sv[96]; std::snprintf(sv, sizeof sv, "%s", v.value.s.c_str());
                                                    if (ImGui::InputTextWithHint("##vv", "value", sv, sizeof sv)) v.value.s = sv; break; }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) remove_var = i;
            ImGui::PopID();
        }
        if (remove_var >= 0) g.variables.erase(g.variables.begin() + remove_var);
        if (ImGui::SmallButton("+ Add variable"))
            g.variables.push_back({ std::string("var") + std::to_string(g.variables.size() + 1), ecs::LogicValue::make_int(0) });
    }

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

    // Pin geometry. EXEC pins sit near the top (title row); a node's DATA input
    // pins run down the left edge below them, and a DATA node's single output pin
    // is centred on the right. Exec = gold; data = cyan.
    const ImU32 kExecCol = IM_COL32(210, 200, 120, 255);
    const ImU32 kDataCol = IM_COL32(120, 200, 220, 255);
    auto exec_out = [&](const ecs::LogicNode& n) { return ImVec2(origin.x + n.x + kNodeW + 6.0f, origin.y + n.y + 12.0f); };
    auto exec_in  = [&](const ecs::LogicNode& n) { return ImVec2(origin.x + n.x - 6.0f,          origin.y + n.y + 12.0f); };
    auto data_out = [&](const ecs::LogicNode& n) { return ImVec2(origin.x + n.x + kNodeW + 6.0f, origin.y + n.y + kNodeH * 0.5f); };
    auto data_in  = [&](const ecs::LogicNode& n, int slot) { return ImVec2(origin.x + n.x - 6.0f, origin.y + n.y + 30.0f + (slot - 1) * 18.0f); };

    // links — exec (gold, top pins) vs data (cyan, into a specific input slot)
    for (const auto& l : g.links) {
        ecs::LogicNode* a = g.find(l.from); ecs::LogicNode* b = g.find(l.to);
        if (!a || !b) continue;
        ImVec2 s, e; ImU32 col;
        if (ecs::logic_link_is_data(l)) { s = data_out(*a); e = data_in(*b, l.to_pin); col = kDataCol; }
        else                            { s = exec_out(*a); e = exec_in(*b);          col = kExecCol; }
        dl->AddBezierCubic(s, ImVec2(s.x + 50, s.y), ImVec2(e.x - 50, e.y), e, col, 2.5f);
    }
    if (g_link_from != 0) {
        if (ecs::LogicNode* a = g.find(g_link_from)) {
            const ImVec2 s = exec_out(*a);
            dl->AddBezierCubic(s, ImVec2(s.x + 50, s.y), ImVec2(io.MousePos.x - 50, io.MousePos.y), io.MousePos, IM_COL32(210, 200, 120, 180), 2.0f);
        } else g_link_from = 0;
    }
    if (g_data_from != 0) {
        if (ecs::LogicNode* a = g.find(g_data_from)) {
            const ImVec2 s = data_out(*a);
            dl->AddBezierCubic(s, ImVec2(s.x + 50, s.y), ImVec2(io.MousePos.x - 50, io.MousePos.y), io.MousePos, IM_COL32(120, 200, 220, 180), 2.0f);
        } else g_data_from = 0;
    }

    // nodes
    int delete_node = 0;
    bool consumed_click = false;
    for (auto& n : g.nodes) {
        const ImVec2 tl(origin.x + n.x, origin.y + n.y);
        const ImVec2 br(tl.x + kNodeW, tl.y + kNodeH);
        const bool ev  = ecs::logic_is_event(n.kind);
        const bool dat = ecs::logic_is_data(n.kind);                                  // data / pure (300+)
        const bool brn = !dat && static_cast<uint32_t>(n.kind) >= 200;                // flow (Branch/Do Once/Delay)
        // Blue = event, teal = flow/branch, purple = action, slate = data/value.
        const ImU32 body  = ev  ? IM_COL32(46, 74, 112, 255)
                          : dat ? IM_COL32(52, 66, 82, 255)
                          : brn ? IM_COL32(44, 92, 84, 255)
                                : IM_COL32(86, 58, 92, 255);
        const ImU32 title = ev  ? IM_COL32(60, 96, 150, 255)
                          : dat ? IM_COL32(70, 92, 116, 255)
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

        // EXEC pins (gold): click an output, then an input, to wire the flow.
        // Events have no input (runtime-fired); data nodes have no exec pins at all.
        if (ecs::logic_has_input(n.kind)) {
            const ImVec2 ip = exec_in(n);
            dl->AddCircleFilled(ip, 5.0f, IM_COL32(230, 210, 130, 255));
            if (!consumed_click && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && dist(io.MousePos, ip) < 9.0f) {
                if (g_link_from != 0 && g_link_from != n.id) { g.link(g_link_from, n.id); g_link_from = 0; }
                consumed_click = true;
            }
        }
        if (!dat) {
            const ImVec2 op = exec_out(n);
            dl->AddCircleFilled(op, 5.0f, g_link_from == n.id ? IM_COL32(255, 240, 120, 255) : IM_COL32(230, 210, 130, 255));
            if (!consumed_click && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && dist(io.MousePos, op) < 9.0f) {
                g_link_from = n.id; g_data_from = 0; consumed_click = true;
            }
        }

        // DATA pins (cyan): a data node's single output feeds a receiver's input
        // slot. Click a data output, then a data input, to wire a value.
        if (dat) {
            const ImVec2 op = data_out(n);
            dl->AddCircleFilled(op, 5.0f, g_data_from == n.id ? IM_COL32(180, 240, 255, 255) : IM_COL32(120, 200, 220, 255));
            if (!consumed_click && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && dist(io.MousePos, op) < 9.0f) {
                g_data_from = n.id; g_link_from = 0; consumed_click = true;
            }
        }
        const int nin = data_input_count(n.kind);
        for (int slot = 1; slot <= nin; ++slot) {
            const ImVec2 ip = data_in(n, slot);
            const bool wired = [&] { for (const auto& l : g.links) if (l.to == n.id && l.to_pin == slot) return true; return false; }();
            dl->AddCircleFilled(ip, 5.0f, wired ? IM_COL32(120, 200, 220, 255) : IM_COL32(90, 130, 150, 255));
            if (dist(io.MousePos, ip) < 9.0f) ImGui::SetTooltip("%s", data_input_label(n.kind, slot));
            if (!consumed_click && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && dist(io.MousePos, ip) < 9.0f) {
                if (g_data_from != 0 && g_data_from != n.id) { g.link_data(g_data_from, 0, n.id, slot); g_data_from = 0; }
                consumed_click = true;
            }
        }
    }
    if (delete_node) g.remove_node(delete_node);

    dl->PopClipRect();

    // click empty canvas cancels a pending link (exec or data)
    if (canvas_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !consumed_click) { g_link_from = 0; g_data_from = 0; }

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
        if (ImGui::MenuItem("Set Variable")) g.add_node(ecs::LogicNodeKind::SetVar, ax, ay);
        ImGui::Separator();
        ImGui::TextDisabled("Flow");
        if (ImGui::MenuItem("Branch (if)")) g.add_node(ecs::LogicNodeKind::Branch, ax, ay);
        if (ImGui::MenuItem("Do Once"))     g.add_node(ecs::LogicNodeKind::DoOnce, ax, ay);
        if (ImGui::MenuItem("Delay"))       g.add_node(ecs::LogicNodeKind::Delay, ax, ay);
        ImGui::Separator();
        ImGui::TextDisabled("Data / values");
        if (ImGui::BeginMenu("Literal")) {
            if (ImGui::MenuItem("Bool"))   g.add_node(ecs::LogicNodeKind::LiteralBool, ax, ay);
            if (ImGui::MenuItem("Int"))    g.add_node(ecs::LogicNodeKind::LiteralInt, ax, ay);
            if (ImGui::MenuItem("Float"))  g.add_node(ecs::LogicNodeKind::LiteralFloat, ax, ay);
            if (ImGui::MenuItem("String")) g.add_node(ecs::LogicNodeKind::LiteralString, ax, ay);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Get Variable")) {
            if (g.variables.empty()) ImGui::TextDisabled("(add variables above)");
            for (const auto& v : g.variables)
                if (ImGui::MenuItem(v.name.c_str())) { const int id = g.add_node(ecs::LogicNodeKind::GetVar, ax, ay); g.find(id)->param = v.name; }
            ImGui::Separator();
            if (ImGui::MenuItem("(blank Get)")) g.add_node(ecs::LogicNodeKind::GetVar, ax, ay);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Math")) {
            if (ImGui::MenuItem("Add (+)"))      g.add_node(ecs::LogicNodeKind::Add, ax, ay);
            if (ImGui::MenuItem("Subtract (-)")) g.add_node(ecs::LogicNodeKind::Sub, ax, ay);
            if (ImGui::MenuItem("Multiply (x)")) g.add_node(ecs::LogicNodeKind::Mul, ax, ay);
            if (ImGui::MenuItem("Divide (/)"))   g.add_node(ecs::LogicNodeKind::Div, ax, ay);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Logic")) {
            if (ImGui::MenuItem("Compare")) { const int id = g.add_node(ecs::LogicNodeKind::Compare, ax, ay); g.find(id)->param2 = "=="; }
            if (ImGui::MenuItem("And"))     g.add_node(ecs::LogicNodeKind::AndNode, ax, ay);
            if (ImGui::MenuItem("Or"))      g.add_node(ecs::LogicNodeKind::OrNode, ax, ay);
            if (ImGui::MenuItem("Not"))     g.add_node(ecs::LogicNodeKind::NotNode, ax, ay);
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    // Commit a completed gesture. Node drags move the graph every frame while
    // the mouse is held, so this is coalesced exactly like an inspector field:
    // one entry per gesture, and none at all if the graph ended up unchanged.
    if (coalescer && on_edit) {
        const std::string after_text = ecs::logic_to_text(g);
        std::vector<uint8_t> after(after_text.begin(), after_text.end());
        const bool changed = (after != graph_before);
        // Entity id 0: the logic graph is scene-scoped, not per-entity.
        if (auto edit = coalescer->update(0, "Logic Graph", after,
                                          changed, ImGui::IsAnyItemActive()))
            on_edit(*edit);
    }

    ImGui::End();
}

}  // namespace schizo::editor
