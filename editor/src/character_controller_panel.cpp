#include "character_controller_panel.h"
#include "../core/character/include/character_controller.h"
#include "../core/character/include/character_stats.h"
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>

namespace schizo::editor {

CharacterControllerPanel::CharacterControllerPanel() {
    state_.walk_speed = 5.0f;
    state_.sprint_speed = 10.0f;
    state_.jump_force = 15.0f;
    state_.stamina_drain_rate = 20.0f;
    state_.stamina_regen_rate = 10.0f;
    state_.show_velocity_debug = false;
    state_.show_state_name = true;
    state_.show_ground_detection = false;
}

CharacterControllerPanel::~CharacterControllerPanel() = default;

void CharacterControllerPanel::Render(engine::character::CharacterController* controller) {
    if (!ImGui::CollapsingHeader("Character Controller", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    
    RenderMovementProperties();
    ImGui::Spacing();
    
    if (controller) {
        ImGui::Separator();
        ImGui::TextUnformatted("Runtime Status:");
        
        if (auto* stats = controller->GetStats()) {
            RenderCharacterStats(stats);
        }
        
        RenderCurrentState(controller);
        ImGui::Spacing();
        RenderInputBufferStatus(controller);
        ImGui::Spacing();
        RenderLocomotionBlending(controller);
        
        // Debug tools
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Debug Tools##char", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (auto* stats = controller->GetStats()) {
                RenderStatsDebugEditor(stats);
                ImGui::Spacing();
            }
            RenderDebugControls(controller);
        }
    }
    
    ImGui::Separator();
    RenderDebugOptions();
}

void CharacterControllerPanel::RenderMovementProperties() {
    ImGui::TextUnformatted("Movement Parameters:");
    ImGui::SliderFloat("Walk Speed##char", &state_.walk_speed, 1.0f, 15.0f);
    ImGui::SliderFloat("Sprint Speed##char", &state_.sprint_speed, 5.0f, 25.0f);
    ImGui::SliderFloat("Jump Force##char", &state_.jump_force, 5.0f, 30.0f);
    
    ImGui::Spacing();
    ImGui::TextUnformatted("Stamina Parameters:");
    ImGui::SliderFloat("Stamina Drain Rate##char", &state_.stamina_drain_rate, 5.0f, 50.0f);
    ImGui::SliderFloat("Stamina Regen Rate##char", &state_.stamina_regen_rate, 1.0f, 30.0f);
    
    ImGui::Spacing();
    ImGui::TextUnformatted("Ground Detection:");
    ImGui::SliderFloat("Ground Check Distance##char", &state_.ground_check_distance, 0.1f, 2.0f);
    ImGui::SliderFloat("Slope Limit (degrees)##char", &state_.slope_limit, 0.0f, 90.0f);
}

void CharacterControllerPanel::RenderCharacterStats(engine::character::CharacterStats* stats) {
    ImGui::TextUnformatted("Character Stats:");
    
    // Health display
    float health = stats->GetHealth();
    float max_health = stats->GetMaxHealth();
    ImGui::ProgressBar(health / max_health, ImVec2(-1.0f, 0), "");
    ImGui::SameLine();
    ImGui::Text("Health: %.1f/%.1f", health, max_health);
    
    // Stamina display
    float stamina = stats->GetStamina();
    float max_stamina = stats->GetMaxStamina();
    ImGui::ProgressBar(stamina / max_stamina, ImVec2(-1.0f, 0), "");
    ImGui::SameLine();
    ImGui::Text("Stamina: %.1f/%.1f", stamina, max_stamina);
    
    // Armor
    ImGui::Text("Armor: %.1f", stats->GetArmor());
    
    // Level and XP
    ImGui::Text("Level: %d", stats->GetLevel());
    ImGui::Text("Experience: %.0f/%.0f", stats->GetExperience(), stats->GetNextLevelXP());
}

void CharacterControllerPanel::RenderCurrentState(engine::character::CharacterController* controller) {
    ImGui::TextUnformatted("Current State:");
    
    // Get current state name (simplified - would need method in CharacterController)
    const char* state_name = "Unknown";
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "State: %s", state_name);
    
    // Velocity display
    ImGui::Text("Velocity: (%.2f, %.2f, %.2f)", 0.0f, 0.0f, 0.0f);
}

void CharacterControllerPanel::RenderInputBufferStatus(engine::character::CharacterController* controller) {
    ImGui::TextUnformatted("Input Buffer Status:");
    ImGui::ProgressBar(0.5f, ImVec2(-1.0f, 0), "");
    ImGui::SameLine();
    ImGui::Text("3/6 inputs buffered");
}

void CharacterControllerPanel::RenderLocomotionBlending(engine::character::CharacterController* controller) {
    ImGui::TextUnformatted("Animation Blending:");
    
    // Blend parameters (0.0 = Idle, 0.5 = Walk, 1.0 = Sprint)
    static float idle_blend = 1.0f;
    static float walk_blend = 0.0f;
    static float sprint_blend = 0.0f;
    
    ImGui::Text("Idle Blend:   %.2f", idle_blend);
    ImGui::ProgressBar(idle_blend, ImVec2(-1.0f, 0));
    
    ImGui::Text("Walk Blend:   %.2f", walk_blend);
    ImGui::ProgressBar(walk_blend, ImVec2(-1.0f, 0));
    
    ImGui::Text("Sprint Blend: %.2f", sprint_blend);
    ImGui::ProgressBar(sprint_blend, ImVec2(-1.0f, 0));
}

void CharacterControllerPanel::RenderDebugOptions() {
    ImGui::TextUnformatted("Debug Visualization:");
    ImGui::Checkbox("Show Velocity Vector##char", &state_.show_velocity_debug);
    ImGui::Checkbox("Show State Name##char", &state_.show_state_name);
    ImGui::Checkbox("Show Ground Detection##char", &state_.show_ground_detection);
}

void CharacterControllerPanel::RenderViewportDebug(engine::character::CharacterController* controller,
                                                   int viewport_width, int viewport_height) {
    if (!controller) return;
    
    // Draw debug information in viewport
    if (state_.show_state_name) {
        // Render state name overhead
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowBgAlpha(0.35f);
        ImGui::Begin("##CharacterDebug", nullptr, 
                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | 
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "Character State: [State Name]");
        ImGui::TextColored(ImVec4(0.5, 1, 0.5, 1), "Velocity: (0.0, 0.0, 0.0)");
        ImGui::End();
    }
    
    if (state_.show_velocity_debug) {
        // Would draw velocity vector as a 3D arrow from character position
        // Using viewport renderer
    }
    
    if (state_.show_ground_detection) {
        // Would draw ground detection raycast result as a sphere
        // Using viewport renderer
    }
}

void CharacterControllerPanel::ApplyState(engine::character::CharacterController* controller) {
    if (!controller) return;
    
    // Apply properties to character controller
    // (Would need update methods in CharacterController)
}

void CharacterControllerPanel::RenderStatsDebugEditor(engine::character::CharacterStats* stats) {
    if (!stats) {
        ImGui::TextDisabled("No character stats");
        return;
    }
    
    ImGui::TextUnformatted("Character Stats Debug Editor:");
    ImGui::Separator();
    
    // Health editing
    static float debug_health = 100.0f;
    ImGui::SliderFloat("Health##debug", &debug_health, 0.0f, stats->GetMaxHealth());
    if (ImGui::Button("Set Health##debug")) {
        // Would call stats->SetHealth(debug_health)
    }
    
    // Stamina editing
    static float debug_stamina = 100.0f;
    ImGui::SliderFloat("Stamina##debug", &debug_stamina, 0.0f, stats->GetMaxStamina());
    if (ImGui::Button("Set Stamina##debug")) {
        // Would call stats->SetStamina(debug_stamina)
    }
    
    // Armor editing
    static float debug_armor = 10.0f;
    ImGui::SliderFloat("Armor##debug", &debug_armor, 0.0f, 100.0f);
    if (ImGui::Button("Set Armor##debug")) {
        // Would call stats->SetArmor(debug_armor)
    }
    
    // XP editing
    static float debug_xp = 0.0f;
    ImGui::SliderFloat("Experience##debug", &debug_xp, 0.0f, 10000.0f);
    if (ImGui::Button("Set XP##debug")) {
        // Would call stats->AddExperience(debug_xp)
    }
    
    ImGui::Spacing();
    ImGui::TextUnformatted("Resistance Editing:");
    static float fire_resist = 0.0f;
    static float cold_resist = 0.0f;
    ImGui::SliderFloat("Fire Resistance##debug", &fire_resist, 0.0f, 1.0f);
    ImGui::SliderFloat("Cold Resistance##debug", &cold_resist, 0.0f, 1.0f);
}

void CharacterControllerPanel::RenderDebugControls(engine::character::CharacterController* controller) {
    if (!controller) return;
    
    ImGui::TextUnformatted("Debug Controls:");
    ImGui::Separator();
    
    if (ImGui::Button("Reset Character State##debug", ImVec2(-1, 0))) {
        // Reset to idle, clear velocity, etc
    }
    
    if (ImGui::Button("Clear All Cooldowns##debug", ImVec2(-1, 0))) {
        // Clear ability cooldowns
    }
    
    if (ImGui::Button("Restore to Defaults##debug", ImVec2(-1, 0))) {
        // Reset all parameters to defaults
    }
    
    if (ImGui::Button("Simulate Damage (10 HP)##debug", ImVec2(-1, 0))) {
        // Deal 10 damage to character
    }
    
    if (ImGui::Button("Restore Full Health##debug", ImVec2(-1, 0))) {
        // Restore full health
    }
    
    if (ImGui::Button("Restore Full Stamina##debug", ImVec2(-1, 0))) {
        // Restore full stamina
    }
}

} // namespace schizo::editor
