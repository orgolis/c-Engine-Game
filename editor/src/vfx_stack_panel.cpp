// ============================================================================
// vfx_stack_panel.cpp — see header.
// ============================================================================

#include "vfx_stack_panel.h"
#include "curve_editor.h"
#include "vfx/vfx_io.h"

#include <imgui.h>

#include <cstdio>
#include <vector>
#include <cstdint>

namespace schizo::editor {

using namespace schizo::vfx;

void VfxStackPanel::draw_module_params(VfxModule& m) {
    for (auto& kv : m.params) {
        const std::string& key = kv.first;
        ParamValue& v = kv.second;
        ImGui::PushID(key.c_str());
        if (float* f = std::get_if<float>(&v)) {
            if (ImGui::DragFloat(key.c_str(), f, 0.01f)) dirty_ = true;
        } else if (glm::vec3* p = std::get_if<glm::vec3>(&v)) {
            if (ImGui::DragFloat3(key.c_str(), &p->x, 0.01f)) dirty_ = true;
        } else if (glm::vec4* p = std::get_if<glm::vec4>(&v)) {
            if (ImGui::ColorEdit4(key.c_str(), &p->x)) dirty_ = true;
        } else if (auto* c = std::get_if<gws::anim::Curve>(&v)) {
            // Drawn through 4.5's editor, which plots the REAL evaluate():
            // straight lines between keys would show a curve that eases
            // nothing — an editor lying about the runtime.
            if (draw_curve_editor(key.c_str(), *c)) dirty_ = true;
        } else if (auto* g = std::get_if<gws::anim::Gradient>(&v)) {
            if (draw_gradient_editor(key.c_str(), *g)) dirty_ = true;
        }
        ImGui::PopID();
    }
}

void VfxStackPanel::draw_stage(VfxStage stage, const char* label) {
    auto& mods = graph_.stage(stage);
    ImGui::SeparatorText(label);

    for (size_t i = 0; i < mods.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const bool open = ImGui::TreeNodeEx(module_kind_name(mods[i].kind),
                                            ImGuiTreeNodeFlags_DefaultOpen);
        // Explicit reorder controls: order is an edit that changes the motion,
        // and a drag whose drop target is ambiguous changes it by accident.
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 78.0f);
        bool removed = false;
        if (ImGui::SmallButton("^")) {
            if (i > 0) { graph_.move_module(stage, i, i - 1); dirty_ = true; }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("v")) {
            if (i + 1 < mods.size()) { graph_.move_module(stage, i, i + 1); dirty_ = true; }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) { removed = true; }

        if (open) {
            if (!removed) draw_module_params(mods[i]);
            ImGui::TreePop();
        }
        ImGui::PopID();
        if (removed) { graph_.remove_module(stage, i); dirty_ = true; break; }
    }

    // The add menu is filtered to modules legal in THIS stage — offering an
    // Update module under Spawn invites a stack that runs at the wrong
    // frequency, which validate() would then have to report.
    const std::string btn = std::string("+ Add##") + label;
    const std::string pop = std::string("add_") + label;
    if (ImGui::Button(btn.c_str())) ImGui::OpenPopup(pop.c_str());
    if (ImGui::BeginPopup(pop.c_str())) {
        for (int i = 0; i <= static_cast<int>(ModuleKind::VelocityOverLife); ++i) {
            const auto k = static_cast<ModuleKind>(i);
            if (stage_of(k) != stage) continue;
            if (ImGui::MenuItem(module_kind_name(k))) { graph_.add_module(k); dirty_ = true; }
        }
        ImGui::EndPopup();
    }
}

void VfxStackPanel::draw(bool* open) {
    if (!ImGui::Begin("VFX Stack", open)) { ImGui::End(); return; }

    char name[128];
    std::snprintf(name, sizeof(name), "%s", graph_.name.c_str());
    if (ImGui::InputText("Name", name, sizeof(name))) { graph_.name = name; dirty_ = true; }

    int maxp = static_cast<int>(graph_.max_particles);
    if (ImGui::DragInt("Max particles", &maxp, 1.0f, 0, 1 << 20)) {
        graph_.max_particles = static_cast<uint32_t>(maxp < 0 ? 0 : maxp);
        dirty_ = true;
    }
    if (ImGui::Checkbox("World space", &graph_.world_space)) dirty_ = true;

    draw_stage(VfxStage::Spawn,  "Spawn");
    draw_stage(VfxStage::Init,   "Initialize");
    draw_stage(VfxStage::Update, "Update");

    // Problems are shown inline rather than on save: every case validate()
    // reports produces an effect that is invisible, motionless or the wrong
    // colour at runtime with no error, so the panel is the only place they can
    // be seen at all.
    const std::vector<std::string> problems = graph_.validate();
    if (!problems.empty()) {
        ImGui::SeparatorText("Problems");
        for (const std::string& p : problems)
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%s", p.c_str());
    }

    ImGui::Separator();
    char pathbuf[512];
    std::snprintf(pathbuf, sizeof(pathbuf), "%s", path_.c_str());
    if (ImGui::InputText("Asset path", pathbuf, sizeof(pathbuf))) path_ = pathbuf;

    ImGui::BeginDisabled(path_.empty());
    if (ImGui::Button("Save")) {
        if (schizo::vfx::save_vfx(path_, graph_)) dirty_ = false;
    }
    ImGui::EndDisabled();
    if (dirty_) { ImGui::SameLine(); ImGui::TextUnformatted("(unsaved)"); }

    ImGui::End();
}

}  // namespace schizo::editor
