#pragma once

#include <memory>
#include <string>
#include <vector>
#include <cstdint>

// Forward declarations
namespace engine::ability {
    class Ability;
    class AbilitySystem;
    class SkillTree;
    class Modifier;
}

namespace schizo::editor {

/**
 * @struct AbilitySystemState
 * @brief Editor state for ability system properties
 */
struct AbilitySystemState {
    // Listbox state
    int selected_ability_index = -1;
    char ability_filter[128] = "";
    
    // Skill tree state
    bool show_skill_tree = false;
    int selected_skill_node = -1;
    
    // Modifier display
    bool show_modifier_breakdown = false;
    
    // Display flags
    bool show_cooldowns = true;
    bool show_ability_descriptions = true;
};

/**
 * @class AbilitySystemPanel
 * @brief Inspector panel for ability system editing and debugging
 * 
 * Features:
 * - Ability listing with search/filter
 * - Ability properties (cooldown, cost, damage)
 * - Cooldown timer display
 * - Skill tree viewer with unlock status
 * - Modifier pipeline visualization
 * - Ability effect configuration
 */
class AbilitySystemPanel {
public:
    AbilitySystemPanel();
    ~AbilitySystemPanel();
    
    /**
     * Render inspector panel for ability system
     * @param ability_system Pointer to ability system to inspect (optional)
     */
    void Render(engine::ability::AbilitySystem* ability_system);
    
    /**
     * Render ability listbox with filtering
     * @param ability_system Ability system to display abilities from
     */
    void RenderAbilityListbox(engine::ability::AbilitySystem* ability_system);
    
    /**
     * Render selected ability properties
     * @param ability Ability to display properties for
     */
    void RenderAbilityProperties(engine::ability::Ability* ability);
    
    /**
     * Render cooldown display for all abilities
     * @param ability_system Ability system with cooldowns
     */
    void RenderCooldownDisplay(engine::ability::AbilitySystem* ability_system);
    
    /**
     * Render skill tree viewer
     * @param skill_tree Skill tree to display
     */
    void RenderSkillTreeViewer(engine::ability::SkillTree* skill_tree);
    
    /**
     * Render modifier pipeline breakdown
     * @param modifiers Vector of modifiers to display
     * @param base_value Base value before modifiers
     */
    void RenderModifierBreakdown(const std::vector<engine::ability::Modifier>& modifiers, 
                                float base_value);
    
    /**
     * Get current editor state
     */
    const AbilitySystemState& GetState() const { return state_; }

    /**
     * Debug tools: Activate ability for testing
     */
    void RenderAbilityTestingTools(engine::ability::AbilitySystem* ability_system);
    
    /**
     * Debug tools: Ability spawn/creation
     */
    void RenderAbilityCreationTools();

private:
    AbilitySystemState state_;
    
    // Helper methods
    void RenderAbilityEffect(engine::ability::Ability* ability);
    void RenderAbilityStats(engine::ability::Ability* ability);
    void RenderModifierPipeline(const std::vector<engine::ability::Modifier>& modifiers);
};

} // namespace schizo::editor
