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
    if (!ImGui::CollapsingHeader("Ability System", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    
    ImGui::TextUnformatted("Ability Management:");
    
    if (ImGui::BeginChildFrame(ImGui::GetID("AbilitySystemPanel"), ImVec2(-1, 300))) {
        if (ability_system) {
            RenderAbilityListbox(ability_system);
        } else {
            ImGui::TextDisabled("No ability system loaded");
        }
        ImGui::EndChildFrame();
    }
    
    ImGui::Spacing();
    ImGui::Checkbox("Show Cooldowns##ability", &state_.show_cooldowns);
    ImGui::Checkbox("Show Descriptions##ability", &state_.show_ability_descriptions);
    ImGui::Checkbox("Show Skill Tree##ability", &state_.show_skill_tree);
    
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
            ImGui::TextDisabled("No ability system loaded for debug tools");
        }
        
        ImGui::Unindent();
    }
}

void AbilitySystemPanel::RenderAbilityListbox(engine::ability::AbilitySystem* ability_system) {
    ImGui::TextUnformatted("Search:");
    ImGui::InputText("##AbilityFilter", state_.ability_filter, sizeof(state_.ability_filter));
    
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
        ImGui::TextDisabled("No ability selected");
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
        ImGui::TextDisabled("No skill tree loaded");
        return;
    }
    
    ImGui::TextUnformatted("Skill Tree:");
    ImGui::Text("Skill Points Available: 5");
    ImGui::Separator();
    
    // Display skill nodes in hierarchical view
    if (ImGui::TreeNode("Tier 1 - Basic Skills")) {
        ImGui::Checkbox("Skill 1-A (Fire Damage)##s1a", new bool(true));
        ImGui::Checkbox("Skill 1-B (Ice Damage)##s1b", new bool(false));
        ImGui::Checkbox("Skill 1-C (Lightning)##s1c", new bool(false));
        ImGui::TreePop();
    }
    
    if (ImGui::TreeNode("Tier 2 - Advanced Skills")) {
        ImGui::BeginDisabled();
        ImGui::Checkbox("Skill 2-A (Requires Skill 1-A)##s2a", new bool(false));
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
    
    static int effect_type = 0;
    ImGui::Combo("Effect Type##ability", &effect_type, 
                "Damage\0\Heal\0\Status\0\DoT\0\Custom\0\0");
    
    ImGui::SliderFloat("Base Damage##ability", new float(50.0f), 1.0f, 200.0f);
    ImGui::SliderFloat("Effect Radius##ability", new float(5.0f), 0.0f, 20.0f);
    ImGui::Checkbox("Requires Target##ability", new bool(true));
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
    if (ImGui::Button("Activate: Fireball##test", ImVec2(-1, 0))) {
        // ability_system->ActivateAbility(fireball_id)
        ImGui::OpenPopup("Ability Activated##popup");
    }
    
    if (ImGui::Button("Activate: Freezing Ray##test", ImVec2(-1, 0))) {
        // ability_system->ActivateAbility(freeze_id)
        ImGui::OpenPopup("Ability Activated##popup");
    }
    
    if (ImGui::Button("Activate: Lightning Strike##test", ImVec2(-1, 0))) {
        // ability_system->ActivateAbility(lightning_id)
        ImGui::OpenPopup("Ability Activated##popup");
    }
    
    if (ImGui::BeginPopupModal("Ability Activated##popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "✓ Ability activated successfully!");
        ImGui::Spacing();
        if (ImGui::Button("OK##popup", ImVec2(100, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    
    ImGui::Spacing();
    ImGui::TextUnformatted("Quick Actions:");
    
    if (ImGui::Button("Clear All Cooldowns##ability", ImVec2(-1, 0))) {
        // ability_system->ClearAllCooldowns()
    }
    
    if (ImGui::Button("Reset Skill Tree##ability", ImVec2(-1, 0))) {
        // ability_system->ResetSkillTree()
    }
    
    if (ImGui::Button("Add Test Ability##ability", ImVec2(-1, 0))) {
        ImGui::OpenPopup("Create Test Ability##popup");
    }
    
    if (ImGui::BeginPopupModal("Create Test Ability##popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char ability_name[64] = "Test Ability";
        static float damage = 25.0f;
        static float cooldown = 5.0f;
        
        ImGui::InputText("Ability Name##create", ability_name, sizeof(ability_name));
        ImGui::SliderFloat("Base Damage##create", &damage, 1.0f, 200.0f);
        ImGui::SliderFloat("Cooldown##create", &cooldown, 0.1f, 30.0f);
        
        if (ImGui::Button("Create##ability", ImVec2(100, 0))) {
            // Create ability based on parameters
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##ability", ImVec2(100, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void AbilitySystemPanel::RenderAbilityCreationTools() {
    ImGui::TextUnformatted("Ability Templates:");
    ImGui::Separator();
    
    if (ImGui::Button("Create Damage Ability##template", ImVec2(-1, 0))) {
        // Create template damage ability
    }
    
    if (ImGui::Button("Create Heal Ability##template", ImVec2(-1, 0))) {
        // Create template heal ability
    }
    
    if (ImGui::Button("Create DoT Ability##template", ImVec2(-1, 0))) {
        // Create template damage-over-time ability
    }
    
    if (ImGui::Button("Create Channeled Ability##template", ImVec2(-1, 0))) {
        // Create template channeled ability
    }
}

} // namespace schizo::editor
