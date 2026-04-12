#pragma once

#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entt.hpp>

#include "ability.h"
#include "cooldown_manager.h"
#include "modifier.h"

namespace engine::ability {

typedef std::unordered_map<std::string, std::shared_ptr<Ability>> AbilityLibrary;

/**
 * @class AbilitySystem
 * @brief Central system for ability management and execution
 *
 * Responsibilities:
 * - Store all castable abilities for a character
 * - Track ability cooldowns
 * - Handle ability activation and target selection
 * - Manage acquired abilities (from skill tree, items, buffs)
 * - Track attribute bonuses from abilities
 */
class AbilitySystem {
public:
    /**
     * Create ability system for an entity
     * @param entity The character entity
     */
    explicit AbilitySystem(entt::entity entity);
    ~AbilitySystem() = default;

    /**
     * Add an ability to the character's ability list
     * @param ability The ability to add
     * @return true if successfully added
     */
    bool AddAbility(std::shared_ptr<Ability> ability);

    /**
     * Remove an ability
     * @param ability_name Name of the ability to remove
     */
    bool RemoveAbility(const std::string& ability_name);

    /**
     * Get an ability by name
     * @param ability_name Name of the ability
     * @return Pointer to ability or nullptr if not found
     */
    Ability* GetAbility(const std::string& ability_name);
    const Ability* GetAbility(const std::string& ability_name) const;

    /**
     * Get all abilities
     */
    const AbilityLibrary& GetAllAbilities() const { return abilities_; }

    /**
     * Check if character knows an ability
     */
    bool HasAbility(const std::string& ability_name) const {
        return abilities_.find(ability_name) != abilities_.end();
    }

    // ============ ACTIVATION ============

    /**
     * Try to activate an ability
     * @param ability_name Name of the ability to activate
     * @param target Target entity (nullptr for self)
     * @param position Target position (for AOE)
     * @return true if successfully activated
     */
    bool ActivateAbility(const std::string& ability_name, entt::entity target = entt::null,
                        const glm::vec3& position = glm::vec3(0.0f));

    /**
     * Check if ability can be activated
     * @param ability_name Name of the ability
     * @return Error message if cannot cast, empty string if can cast
     */
    std::string CanActivateAbility(const std::string& ability_name);

    /**
     * Start channeling an ability
     * @param ability_name Name of the channeled ability
     * @return true if channel started
     */
    bool StartChannel(const std::string& ability_name);

    /**
     * Get channeling ability name
     */
    const std::string& GetChannelingAbility() const { return channeling_ability_; }

    /**
     * Stop channeling and release
     * @return true if successfully released
     */
    bool ReleaseChannel();

    /**
     * Get channel power (0.0 - 1.0) for visualization
     */
    float GetChannelPower() const;

    // ============ COOLDOWN MANAGEMENT ============

    /**
     * Get remaining cooldown
     */
    float GetAbilityCooldown(const std::string& ability_name) const;

    /**
     * Check if ability is ready
     */
    bool IsAbilityReady(const std::string& ability_name) const;

    /**
     * Get cooldown manager for UI display
     */
    CooldownManager& GetCooldownManager() { return cooldown_manager_; }
    const CooldownManager& GetCooldownManager() const { return cooldown_manager_; }

    // ============ MODIFIERS & ATTRIBUTES ============

    /**
     * Get modifier database for ability-granted stats
     */
    ModifierDatabase& GetModifierDatabase() { return modifier_db_; }
    const ModifierDatabase& GetModifierDatabase() const { return modifier_db_; }

    /**
     * Add temporary modifier from ability
     * Example: "Cast Speed" bonus, "Damage" bonus
     */
    void AddTemporaryModifier(const std::string& stat_name, const Modifier& modifier);

    /**
     * Remove modifiers from a source
     */
    void RemoveModifiersFromSource(const std::string& source);

    // ============ UPDATE ============

    /**
     * Update the ability system
     * @param dt Delta time in seconds
     */
    void Update(float dt);

private:
    entt::entity entity_;
    AbilityLibrary abilities_;
    CooldownManager cooldown_manager_;
    ModifierDatabase modifier_db_;

    // Channeling state
    std::string channeling_ability_;
    float channel_start_time_ = 0.0f;
};

} // namespace engine::ability
