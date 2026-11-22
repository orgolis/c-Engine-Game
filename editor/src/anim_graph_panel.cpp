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

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include "document_bar.h"

namespace schizo::editor {

namespace {

// Any State is drawn as a node like any other so transitions can be dragged
// from it. It gets an id no real state can have.
constexpr uint32_t kAnyNodeId = kAnyState;

ImU32 state_color(bool is_entry, int layer) {
    if (is_entry) return IM_COL32(70, 130, 80, 255);   // green: where playback begins
    return layer > 0 ? IM_COL32(88, 64, 110, 255)      // an upper layer's
                     : IM_COL32(52, 72, 104, 255);
}

// A text field over a std::string.
bool edit_string(const char* label, std::string& v, float width = 0.0f) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s", v.c_str());
    if (width > 0.0f) ImGui::SetNextItemWidth(width);
    if (!ImGui::InputText(label, buf, sizeof buf)) return false;
    v = buf;
    return true;
}

}  // namespace

void draw_anim_graph_panel(bool& open, AnimGraph& graph, NodeCanvas& canvas,
                           bool& out_modified, DocumentFile* doc) {
    if (!open) return;

    ImGui::SetNextWindowSize(ImVec2(1100.0f, 820.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Animation State Machine", &open)) { ImGui::End(); return; }

    // The file this document lives in (4.10). Null for callers that have no
    // file -- a test harness, or a scratch document.
    if (doc) draw_document_bar(*doc, "animation graph");

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

    // The canvas above, what is selected (and the document's parts) below it.
    const float canvas_h = std::max(220.0f, ImGui::GetContentRegionAvail().y * 0.55f);
    ImGui::BeginChild("##animcanvas_area", ImVec2(0.0f, canvas_h), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    canvas.begin("##animcanvas");

    // Any State first, so transitions from it read left-to-right like the rest.
    canvas.begin_node(kAnyNodeId, "Any State", -40.0f, 20.0f, IM_COL32(96, 72, 40, 255));
    canvas.output_pin("", IM_COL32(230, 190, 130, 255));
    canvas.end_node();

    for (const AnimGraphState& s : graph.states()) {
        const bool entry = s.id == graph.layer_entry(s.layer);
        std::string title = s.layer > 0 ? "[" + (static_cast<size_t>(s.layer - 1) < graph.layers().size() && !graph.layers()[static_cast<size_t>(s.layer - 1)].name.empty()
                                                     ? graph.layers()[static_cast<size_t>(s.layer - 1)].name : "Layer " + std::to_string(s.layer)) + "] " + s.name
                                        : s.name;
        if (s.kind == AnimGraphStateKind::Blend1D || s.kind == AnimGraphStateKind::Blend2D)
            title += "  (" + std::to_string(s.samples.size()) + " clips)";
        canvas.begin_node(s.id, entry ? (title + "  (entry)") : title,
                          s.x, s.y, state_color(entry, s.layer));
        // One input: a state is either entered or not. Unlike a material node
        // there is nothing to disambiguate, so every transition lands here.
        canvas.input_pin(0, "in", IM_COL32(180, 200, 220, 255));
        canvas.output_pin("", IM_COL32(180, 200, 220, 255));
        canvas.end_node();
    }

    for (const AnimGraphTransition& t : graph.transitions())
        canvas.link(t.from, t.to, 0);

    canvas.end();
    ImGui::EndChild();

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
    ImGui::BeginChild("##animgraph_details", ImVec2(0.0f, 0.0f));
    if (AnimGraphState* s = graph.state(canvas.selected_node())) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%s", s->name.c_str());
        if (ImGui::InputText("name", buf, sizeof buf)) { s->name = buf; out_modified = true; }
        int kind = static_cast<int>(s->kind);
        const char* kinds[] = {"a clip", "a 1D blend space", "a 2D blend space", "nothing (the layer below shows)"};
        if (ImGui::Combo("plays", &kind, kinds, IM_ARRAYSIZE(kinds))) { s->kind = static_cast<AnimGraphStateKind>(kind); out_modified = true; }
        const int layers = 1 + static_cast<int>(graph.layers().size());
        if (layers > 1 && ImGui::SliderInt("layer", &s->layer, 0, layers - 1)) out_modified = true;
        if (s->kind == AnimGraphStateKind::Clip) {
            if (edit_string("clip name", s->clip)) out_modified = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("a clip of the model by name (or part of it); empty: the index below");
            if (s->clip.empty() && ImGui::DragInt("clip", &s->clip_index, 0.2f, 0, 64)) out_modified = true;
        } else if (s->kind != AnimGraphStateKind::None) {
            const bool two = s->kind == AnimGraphStateKind::Blend2D;
            if (edit_string(two ? "parameter x" : "parameter", s->param_x, 140.0f)) out_modified = true;
            if (two) { ImGui::SameLine(); if (edit_string("parameter y", s->param_y, 140.0f)) out_modified = true; }
            if (ImGui::Checkbox("radial (locomotion: outside, clamped from the origin)", &s->radial)) out_modified = true;
            int remove_sample = -1;
            for (size_t k = 0; k < s->samples.size(); ++k) {
                AnimGraphSample& sm = s->samples[k];
                ImGui::PushID(static_cast<int>(k) + 1000);
                if (edit_string("##clip", sm.clip, 150.0f)) out_modified = true;
                ImGui::SameLine();
                if (ImGui::Checkbox("measured", &sm.measured)) out_modified = true;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("at the velocity its clip is measured to move at (a locomotion clip)");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70.0f);
                if (ImGui::DragFloat("##x", &sm.x, 0.02f)) out_modified = true;
                if (two) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(70.0f);
                    if (ImGui::DragFloat("##y", &sm.y, 0.02f)) out_modified = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) remove_sample = static_cast<int>(k);
                ImGui::PopID();
            }
            if (remove_sample >= 0) { s->samples.erase(s->samples.begin() + remove_sample); out_modified = true; }
            if (ImGui::SmallButton("add clip to the space")) { s->samples.push_back({"", 0.0f, 0.0f, false}); out_modified = true; }
        }
        if (ImGui::DragFloat("speed", &s->speed, 0.02f, -4.0f, 4.0f)) out_modified = true;
        if (edit_string("rate parameter", s->rate_param, 140.0f)) out_modified = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("a float parameter multiplying the speed (locomotion: loco_rate)");
        ImGui::SameLine();
        if (edit_string("sync group", s->sync, 100.0f)) out_modified = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("states of one group keep one phase between their feet's plants (in step)");
        if (ImGui::Checkbox("loop", &s->loop)) out_modified = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("root motion", &s->apply_root_motion)) out_modified = true;
        if (s->id != graph.layer_entry(s->layer) && ImGui::Button("Make entry state")) {
            if (s->layer <= 0) graph.set_entry_state(s->id);
            else if (static_cast<size_t>(s->layer - 1) < graph.layers().size()) graph.layers()[static_cast<size_t>(s->layer - 1)].entry = s->id;
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

    // What gameplay sets, the layers above the first, and the slots montages play on.
    if (ImGui::CollapsingHeader("Parameters, layers and slots")) {
        ImGui::TextDisabled("A character in Play sets: vx vz speed moving airborne jumped landing crouched dead aiming melee");
        int remove_param = -1;
        for (size_t i = 0; i < graph.params().size(); ++i) {
            AnimGraphParam& p = graph.params()[i];
            ImGui::PushID(static_cast<int>(i) + 2000);
            if (edit_string("##name", p.name, 130.0f)) out_modified = true;
            ImGui::SameLine();
            const char* types[] = {"float", "bool", "trigger"};
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::Combo("##type", &p.type, types, 3)) out_modified = true;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
            if (ImGui::DragFloat("##init", &p.init, 0.02f)) out_modified = true;
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) remove_param = static_cast<int>(i);
            ImGui::PopID();
        }
        if (remove_param >= 0) { graph.params().erase(graph.params().begin() + remove_param); out_modified = true; }
        if (ImGui::SmallButton("add parameter")) { graph.params().push_back({"param", 0, 0.0f}); out_modified = true; }
        ImGui::Separator();
        int remove_layer = -1;
        for (size_t i = 0; i < graph.layers().size(); ++i) {
            AnimGraphLayer& l = graph.layers()[i];
            ImGui::PushID(static_cast<int>(i) + 3000);
            ImGui::Text("layer %d", static_cast<int>(i) + 1);
            ImGui::SameLine();
            if (edit_string("##lname", l.name, 110.0f)) out_modified = true;
            ImGui::SameLine();
            if (edit_string("mask bone##lmask", l.mask_bone, 110.0f)) out_modified = true;
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) remove_layer = static_cast<int>(i);
            ImGui::PopID();
        }
        if (remove_layer >= 0) {
            graph.layers().erase(graph.layers().begin() + remove_layer);
            for (AnimGraphState& st : graph.states()) if (st.layer > remove_layer + 1) --st.layer; else if (st.layer == remove_layer + 1) st.layer = 0;
            out_modified = true;
        }
        if (ImGui::SmallButton("add layer")) { graph.layers().push_back({"Upper", "spine", 0}); out_modified = true; }
        ImGui::Separator();
        int remove_slot = -1;
        for (size_t i = 0; i < graph.slots().size(); ++i) {
            AnimGraphSlot& sl = graph.slots()[i];
            ImGui::PushID(static_cast<int>(i) + 4000);
            if (edit_string("slot##sname", sl.name, 110.0f)) out_modified = true;
            ImGui::SameLine();
            if (edit_string("mask bone##smask", sl.mask_bone, 110.0f)) out_modified = true;
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) remove_slot = static_cast<int>(i);
            ImGui::PopID();
        }
        if (remove_slot >= 0) { graph.slots().erase(graph.slots().begin() + remove_slot); out_modified = true; }
        if (ImGui::SmallButton("add slot")) { graph.slots().push_back({"upper", "spine"}); out_modified = true; }
    }
    ImGui::EndChild();

    ImGui::End();
}

}  // namespace schizo::editor
