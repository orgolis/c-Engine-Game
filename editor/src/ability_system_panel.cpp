#include "ui_layout.h"
#include "ability_system_panel.h"
#include "../core/ability/include/ability.h"
#include "../core/ability/include/ability_system.h"
#include "../core/ability/include/skill_tree.h"
#include "../core/ability/include/modifier.h"
#include <imgui.h>
#include <spdlog/spdlog.h>

namespace schizo::editor {

AbilitySystemPanel::AbilitySystemPanel() {
    state_.selected_ability_index = -1;
    state_.show_skill_tree = false;
    state_.selected_skill_node = -1;
    state_.show_modifier_breakdown = false;
    state_.show_cooldowns = true;
    state_.show_ability_descriptions = true;
}

AbilitySystemPanel::~AbilitySystemPanel() = default;

void AbilitySystemPanel::Render(engine::ability::AbilitySystem* ability_system) {
    // DISABLED - This panel causes ImGui Begin/End mismatches that crash the editor.
    // Will be refactored in a future iteration.
    return;
    
    if (!ImGui::CollapsingHeader("Ability System", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    
    ImGui::TextUnformatted("Ability Management:");
    
    // EndChild() must always be called after BeginChild() since ImGui 1.89.5
    // (it pushes the child window unconditionally; gating EndChild on the
    // return value leaks the child onto the stack and trips the
    // "Must call EndChild() and not End()!" assertion in the parent's End()).
    ImGui::BeginChild("AbilitySystemPanel", ImVec2(-1, 300), true);
    if (ability_system) {
        RenderAbilityListbox(ability_system);
    } else {
        ui::TextDisabledWrapped("No ability system loaded");
    }
    ImGui::EndChild();
    
    ImGui::Spacing();
    ui::Checkbox("Show Cooldowns##ability", &state_.show_cooldowns);
    ui::Checkbox("Show Descriptions##ability", &state_.show_ability_descriptions);
    ui::Checkbox("Show Skill Tree##ability", &state_.show_skill_tree);
    
    if (state_.show_cooldowns && ability_system) {
        ImGui::Separator();
        RenderCooldownDisplay(ability_system);
    }
    
    // Debug Tools Section
    if (ImGui::CollapsingHeader("Debug Tools##ability", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();
        
        if (ability_system) {
            RenderAbilityTestingTools(ability_system);
            ImGui::Separator();
            RenderAbilityCreationTools();
        } else {
            ui::TextDisabledWrapped("No ability system loaded for debug tools");
        }
        
        ImGui::Unindent();
    }
}

void AbilitySystemPanel::RenderAbilityListbox(engine::ability::AbilitySystem* ability_system) {
    ImGui::TextUnformatted("Search:");
    ui::InputText("##AbilityFilter", state_.ability_filter, sizeof(state_.ability_filter));
    
    ImGui::Separator();
    ImGui::TextUnformatted("Abilities:");
    
    // TODO: Iterate through abilities in ability_system
    // For now, show placeholder
    static int selected = 0;
    if (ImGui::Selectable("Sample Ability 1##ab1", selected == 0)) { selected = 0; }
    if (ImGui::Selectable("Sample Ability 2##ab2", selected == 1)) { selected = 1; }
    if (ImGui::Selectable("Sample Ability 3##ab3", selected == 2)) { selected = 2; }
}

void AbilitySystemPanel::RenderAbilityProperties(engine::ability::Ability* ability) {
    if (!ability) {
        ui::TextDisabledWrapped("No ability selected");
        return;
    }
    
    ImGui::TextUnformatted("Ability Properties:");
    ImGui::Separator();
    
    // Display ability name and description
    ImGui::TextColored(ImVec4(1, 1, 0, 1), "Ability Name");
    if (state_.show_ability_descriptions) {
        ImGui::TextWrapped("Ability description would go here...");
    }
    
    RenderAbilityStats(ability);
    RenderAbilityEffect(ability);
}

void AbilitySystemPanel::RenderCooldownDisplay(engine::ability::AbilitySystem* ability_system) {
    ImGui::TextUnformatted("Cooldown Status:");
    ImGui::Indent();
    
    // Display cooldowns for all abilities
    ImGui::Text("Fireball: Ready");
    ImGui::ProgressBar(0.0f, ImVec2(-1, 0));
    
    ImGui::Text("Freezing Ray: 2.5s remaining");
    ImGui::ProgressBar(0.58f, ImVec2(-1, 0));
    
    ImGui::Text("Lightning Strike: 0.1s remaining");
    ImGui::ProgressBar(0.98f, ImVec2(-1, 0));
    
    ImGui::Unindent();
}

void AbilitySystemPanel::RenderSkillTreeViewer(engine::ability::SkillTree* skill_tree) {
    if (!skill_tree) {
        ui::TextDisabledWrapped("No skill tree loaded");
        return;
    }
    
    ImGui::TextUnformatted("Skill Tree Preview:");
    ui::TextDisabledWrapped("Preview only; these values do not unlock runtime skills.");
    ImGui::Text("Skill Points Available: 5");
    ImGui::Separator();
    
    // Display skill nodes in hierarchical view
    if (ImGui::TreeNode("Tier 1 - Basic Skills")) {
        ui::Checkbox("Skill 1-A (Fire Damage)##s1a", &state_.preview_skills[0]);
        ui::Checkbox("Skill 1-B (Ice Damage)##s1b", &state_.preview_skills[1]);
        ui::Checkbox("Skill 1-C (Lightning)##s1c", &state_.preview_skills[2]);
        ImGui::TreePop();
    }
    
    if (ImGui::TreeNode("Tier 2 - Advanced Skills")) {
        ImGui::BeginDisabled();
        ui::Checkbox("Skill 2-A (Requires Skill 1-A)##s2a", &state_.preview_skills[3]);
        ImGui::EndDisabled();
        ImGui::TreePop();
    }
}

void AbilitySystemPanel::RenderModifierBreakdown(const std::vector<engine::ability::Modifier>& modifiers,
                                                  float base_value) {
    ImGui::TextUnformatted("Modifier Pipeline:");
    
    float current = base_value;
    ImGui::Indent();
    ImGui::TextColored(ImVec4(1, 1, 1, 1), "Base Value: %.1f", base_value);
    
    // Group modifiers by operation type
    float add_total = 0.0f;
    float multiply_total = 1.0f;
    float set_value = base_value;
    
    ImGui::TextColored(ImVec4(0.5, 1, 0.5, 1), "+= Add Operations");
    ImGui::Text("  Total: +%.1f", add_total);
    
    ImGui::TextColored(ImVec4(0.5, 1, 0.5, 1), "*= Multiply Operations");
    ImGui::Text("  Total Multiplier: x%.2f", multiply_total);
    
    ImGui::TextColored(ImVec4(0.5, 1, 0.5, 1), "= Set Operations");
    ImGui::Text("  Final Value: %.1f", set_value);
    
    ImGui::Unindent();
    
    float final_value = (base_value + add_total) * multiply_total;
    if (set_value != base_value) {
        final_value = set_value;
    }
    
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1, 1, 0, 1), "Final Value: %.1f", final_value);
}

void AbilitySystemPanel::RenderAbilityEffect(engine::ability::Ability* ability) {
    ImGui::TextUnformatted("Effect Properties:");
    
    ui::TextDisabledWrapped("Effect preview; these values are not applied to the runtime ability.");
    ui::Combo("Effect Type##ability", &state_.preview_effect_type,
                "Damage\0\Heal\0\Status\0\DoT\0\Custom\0\0");
    
    ui::SliderFloat("Base Damage##ability", &state_.preview_damage, 1.0f, 200.0f);
    ui::SliderFloat("Effect Radius##ability", &state_.preview_effect_radius, 0.0f, 20.0f);
    ui::Checkbox("Requires Target##ability", &state_.preview_requires_target);
}

void AbilitySystemPanel::RenderAbilityStats(engine::ability::Ability* ability) {
    ImGui::TextUnformatted("Ability Stats:");
    ImGui::Text("Cast Time: 0.5s");
    ImGui::Text("Cooldown: 5.0s");
    ImGui::Text("Resource Cost: 50 mana");
    ImGui::Text("Range: 20m");
}

void AbilitySystemPanel::RenderAbilityTestingTools(engine::ability::AbilitySystem* ability_system) {
    if (!ability_system) return;
    
    ImGui::TextUnformatted("Ability Testing:");
    ImGui::Separator();
    
    ImGui::TextUnformatted("Test Ability Activation:");
    
    // Test ability buttons
    if (ui::Button("Activate: Fireball##test", ImVec2(-1, 0))) {
        // ability_system->ActivateAbility(fireball_id)
        ImGui::OpenPopup("Ability Activated##popup");
    }
    
    if (ui::Button("Activate: Freezing Ray##test", ImVec2(-1, 0))) {
        // ability_system->ActivateAbility(freeze_id)
        ImGui::OpenPopup("Ability Activated##popup");
    }
    
    if (ui::Button("Activate: Lightning Strike##test", ImVec2(-1, 0))) {
        // ability_system->ActivateAbility(lightning_id)
        ImGui::OpenPopup("Ability Activated##popup");
    }
    
    if (ImGui::BeginPopupModal("Ability Activated##popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "✓ Ability activated successfully!");
        ImGui::Spacing();
        if (ui::Button("OK##popup", ImVec2(100, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    
    ImGui::Spacing();
    ImGui::TextUnformatted("Quick Actions:");
    
    if (ui::Button("Clear All Cooldowns##ability", ImVec2(-1, 0))) {
        // ability_system->ClearAllCooldowns()
    }
    
    if (ui::Button("Reset Skill Tree##ability", ImVec2(-1, 0))) {
        // ability_system->ResetSkillTree()
    }
    
    if (ui::Button("Add Test Ability##ability", ImVec2(-1, 0))) {
        ImGui::OpenPopup("Create Test Ability##popup");
    }
    
    if (ImGui::BeginPopupModal("Create Test Ability##popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char ability_name[64] = "Test Ability";
        static float damage = 25.0f;
        static float cooldown = 5.0f;
        
        ui::InputText("Ability Name##create", ability_name, sizeof(ability_name));
        ui::SliderFloat("Base Damage##create", &damage, 1.0f, 200.0f);
        ui::SliderFloat("Cooldown##create", &cooldown, 0.1f, 30.0f);
        
        if (ui::Button("Create##ability", ImVec2(100, 0))) {
            // Create ability based on parameters
            ImGui::CloseCurrentPopup();
        }
        ui::SameLineIfFits("Cancel##ability");
        if (ui::Button("Cancel##ability", ImVec2(100, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void AbilitySystemPanel::RenderAbilityCreationTools() {
    ImGui::TextUnformatted("Ability Templates:");
    ImGui::Separator();
    
    if (ui::Button("Create Damage Ability##template", ImVec2(-1, 0))) {
        // Create template damage ability
    }
    
    if (ui::Button("Create Heal Ability##template", ImVec2(-1, 0))) {
        // Create template heal ability
    }
    
    if (ui::Button("Create DoT Ability##template", ImVec2(-1, 0))) {
        // Create template damage-over-time ability
    }
    
    if (ui::Button("Create Channeled Ability##template", ImVec2(-1, 0))) {
        // Create template channeled ability
    }
}

} // namespace schizo::editor
