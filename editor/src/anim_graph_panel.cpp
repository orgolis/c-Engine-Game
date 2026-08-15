// ============================================================================
// anim_graph_panel — the animation state machine on the generic canvas (4.6).
//
// This is the SECOND consumer of NodeCanvas, and the point of it. When the
// canvas shipped I wrote that "reusable" was supported by construction but not
// proven, because it had exactly one user. A state machine is about as far from
// a material graph as the editor gets -- nodes are states rather than
// expressions, links are transitions carrying their own data rather than typed
// value flow, and there is no evaluation order at all -- so if the canvas
// needed changing to accommodate it, the claim was wrong.
//
// It did not. The canvas is unchanged by this file.
// ============================================================================
#include "anim_graph_panel.h"

#include "anim_graph.h"
#include "node_canvas.h"

#include <imgui.h>

#include <cstdint>
#include <string>

namespace schizo::editor {

namespace {

// Any State is drawn as a node like any other so transitions can be dragged
// from it. It gets an id no real state can have.
constexpr uint32_t kAnyNodeId = kAnyState;

ImU32 state_color(bool is_entry) {
    return is_entry ? IM_COL32(70, 130, 80, 255)      // green: where playback begins
                    : IM_COL32(52, 72, 104, 255);
}

}  // namespace

void draw_anim_graph_panel(bool& open, AnimGraph& graph, NodeCanvas& canvas,
                           bool& out_modified) {
    if (!open) return;

    ImGui::SetNextWindowSize(ImVec2(940.0f, 600.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Animation State Machine", &open)) { ImGui::End(); return; }

    // ---- problems, permanently visible -------------------------------------
    // Every failure this catches is silent at runtime: the character simply
    // never does the thing. A toast would be gone before it was read.
    const auto problems = graph.validate();
    if (problems.empty()) {
        ImGui::TextDisabled("%zu state(s), %zu transition(s) — no problems",
                            graph.states().size(), graph.transitions().size());
    } else {
        for (const std::string& p : problems)
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "! %s", p.c_str());
    }
    ImGui::Separator();

    canvas.begin("##animcanvas");

    // Any State first, so transitions from it read left-to-right like the rest.
    canvas.begin_node(kAnyNodeId, "Any State", -40.0f, 20.0f, IM_COL32(96, 72, 40, 255));
    canvas.output_pin("", IM_COL32(230, 190, 130, 255));
    canvas.end_node();

    for (const AnimGraphState& s : graph.states()) {
        const bool entry = s.id == graph.entry_state();
        canvas.begin_node(s.id, entry ? (s.name + "  (entry)") : s.name,
                          s.x, s.y, state_color(entry));
        // One input: a state is either entered or not. Unlike a material node
        // there is nothing to disambiguate, so every transition lands here.
        canvas.input_pin(0, "in", IM_COL32(180, 200, 220, 255));
        canvas.output_pin("", IM_COL32(180, 200, 220, 255));
        canvas.end_node();
    }

    for (const AnimGraphTransition& t : graph.transitions())
        canvas.link(t.from, t.to, 0);

    canvas.end();

    // ---- interactions ------------------------------------------------------
    if (auto moved = canvas.take_moved_node()) {
        if (AnimGraphState* s = graph.state(moved->id)) {
            s->x = moved->x; s->y = moved->y; out_modified = true;
        }
    }
    if (auto made = canvas.take_new_link()) {
        if (graph.add_transition(made->src, made->dst)) out_modified = true;
    }
    if (auto del = canvas.take_deleted_node()) {
        // Any State is not a real state and cannot be deleted; remove_state
        // would simply not find it, but saying so is clearer than a silent
        // no-op the user has to infer.
        if (*del != kAnyNodeId) { graph.remove_state(*del); out_modified = true; }
    }
    if (auto at = canvas.take_context_menu_at()) {
        graph.add_state("State " + std::to_string(graph.states().size() + 1),
                        at->first, at->second);
        out_modified = true;
    }

    // ---- the selected state ------------------------------------------------
    ImGui::Separator();
    if (AnimGraphState* s = graph.state(canvas.selected_node())) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%s", s->name.c_str());
        if (ImGui::InputText("name", buf, sizeof buf)) { s->name = buf; out_modified = true; }
        if (ImGui::DragInt("clip", &s->clip_index, 0.2f, 0, 64)) out_modified = true;
        if (ImGui::DragFloat("speed", &s->speed, 0.02f, -4.0f, 4.0f)) out_modified = true;
        if (ImGui::Checkbox("loop", &s->loop)) out_modified = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("root motion", &s->apply_root_motion)) out_modified = true;
        if (s->id != graph.entry_state() && ImGui::Button("Make entry state")) {
            graph.set_entry_state(s->id);
            out_modified = true;
        }
    } else {
        ImGui::TextDisabled("Right-click the canvas to add a state. "
                            "Drag from a state's right pin to another's left pin "
                            "to make a transition.");
    }

    // ---- transitions -------------------------------------------------------
    // Listed rather than edited on the link itself: a transition carries a list
    // of conditions, and there is nowhere on a bezier to put one.
    if (!graph.transitions().empty() && ImGui::CollapsingHeader("Transitions",
                                                                ImGuiTreeNodeFlags_DefaultOpen)) {
        int remove = -1;
        for (size_t i = 0; i < graph.transitions().size(); ++i) {
            AnimGraphTransition& t = graph.transitions()[i];
            ImGui::PushID(static_cast<int>(i));

            const AnimGraphState* from = graph.state(t.from);
            const AnimGraphState* to   = graph.state(t.to);
            const std::string label =
                (t.from == kAnyState ? std::string("Any State") : (from ? from->name : "?")) +
                "  ->  " + (to ? to->name : "?");

            if (ImGui::TreeNode(label.c_str())) {
                if (ImGui::DragFloat("blend", &t.blend_duration, 0.01f, 0.0f, 4.0f))
                    out_modified = true;
                if (ImGui::Checkbox("has exit time", &t.has_exit_time)) out_modified = true;
                if (t.has_exit_time) {
                    ImGui::SameLine();
                    if (ImGui::DragFloat("exit", &t.exit_time, 0.01f, 0.0f, 1.0f))
                        out_modified = true;
                }

                for (size_t c = 0; c < t.conditions.size(); ++c) {
                    AnimGraphCondition& cond = t.conditions[c];
                    ImGui::PushID(static_cast<int>(c));
                    char pbuf[48];
                    std::snprintf(pbuf, sizeof pbuf, "%s", cond.param.c_str());
                    ImGui::SetNextItemWidth(110.0f);
                    if (ImGui::InputText("##param", pbuf, sizeof pbuf)) {
                        cond.param = pbuf; out_modified = true;
                    }
                    ImGui::SameLine();
                    int op = static_cast<int>(cond.op);
                    const char* ops[] = {">", ">=", "<", "<=", "==", "!=",
                                         "is true", "is false", "triggered"};
                    ImGui::SetNextItemWidth(90.0f);
                    if (ImGui::Combo("##op", &op, ops, IM_ARRAYSIZE(ops))) {
                        cond.op = static_cast<AnimGraphCondOp>(op); out_modified = true;
                    }
                    // The threshold box only appears for operators that use
                    // one; otherwise it invites setting a number that does
                    // nothing.
                    if (cond_uses_threshold(cond.op)) {
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(70.0f);
                        if (ImGui::DragFloat("##thr", &cond.threshold, 0.02f))
                            out_modified = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x")) {
                        t.conditions.erase(t.conditions.begin() + static_cast<long>(c));
                        out_modified = true;
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }

                if (ImGui::SmallButton("add condition")) {
                    t.conditions.push_back({"speed", AnimGraphCondOp::Greater, 0.1f});
                    out_modified = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("delete transition")) remove = static_cast<int>(i);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (remove >= 0) { graph.remove_transition(static_cast<size_t>(remove)); out_modified = true; }
    }

    ImGui::End();
}

}  // namespace schizo::editor
