#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <array>

namespace engine::character {

// Element types for resistances
enum class ElementType : uint8_t {
    Fire,
    Cold,
    Lightning,
    Poison,
    Holy,
    Count
};

/**
 * @class CharacterStats
 * @brief Character vital stats and resource management
 *
 * Tracks:
 * - Health and damage
 * - Stamina and energy
 * - Armor and resistances
 * - Character progression metrics
 */
class CharacterStats {
public:
    /**
     * Create character stats component for an entity
     * @param entity The entity these stats belong to
     */
    explicit CharacterStats(entt::entity entity);

    /**
     * Update stats (stamina regeneration, health effects, etc)
     * @param dt Delta time in seconds
     */
    void Update(float dt);

    // ============ HEALTH ============

    /**
     * Get current health points
     */
    float GetHealth() const { return health_; }

    /**
     * Get maximum health points
     */
    float GetMaxHealth() const { return max_health_; }

    /**
     * Get health as percentage (0.0 - 1.0)
     */
    float GetHealthPercent() const {
        return max_health_ > 0 ? health_ / max_health_ : 0.0f;
    }

    /**
     * Take damage
     * @param damage Amount of damage to apply
     */
    void TakeDamage(float damage);

    /**
     * Heal the character
     * @param amount Amount to heal
     */
    void Heal(float amount);

    /**
     * Check if character is dead
     */
    bool IsDead() const { return health_ <= 0.0f; }

    // ============ STAMINA/ENERGY ============

    /**
     * Get current stamina
     */
    float GetStamina() const { return stamina_; }

    /**
     * Get maximum stamina
     */
    float GetMaxStamina() const { return max_stamina_; }

    /**
     * Get stamina as percentage (0.0 - 1.0)
     */
    float GetStaminaPercent() const {
        return max_stamina_ > 0 ? stamina_ / max_stamina_ : 0.0f;
    }

    /**
     * Consume stamina
     * @param amount Amount to consume
     * @return True if enough stamina was available
     */
    bool ConsumeStamina(float amount);

    /**
     * Check if has enough stamina
     * @param amount Required stamina
     * @return true if stamina >= amount
     */
    bool HasStamina(float amount) const { return stamina_ >= amount; }

    // ============ ARMOR & RESISTANCES ============

    /**
     * Get armor value (reduces physical damage)
     */
    float GetArmor() const { return armor_; }

    /**
     * Set armor value
     */
    void SetArmor(float value) { armor_ = glm::max(0.0f, value); }

    /**
     * Get resistance to a specific element type
     * @param element The element type
     * @return Resistance value (0.0 = no resistance, 1.0 = immune)
     */
    float GetResistance(ElementType element) const {
        return resistances_[static_cast<size_t>(element)];
    }

    /**
     * Set resistance to an element
     * @param element The element type
     * @param value Resistance value (0.0 - 1.0)
     */
    void SetResistance(ElementType element, float value);

    /**
     * Add temporary resistance modifier
     */
    void AddTemporaryResistance(ElementType element, float bonus, float duration);

    // ============ LEVEL & EXPERIENCE ============

    /**
     * Get character level
     */
    uint8_t GetLevel() const { return level_; }

    /**
     * Get current experience points
     */
    uint64_t GetExperience() const { return experience_; }

    /**
     * Add experience points
     * @param amount Amount of XP to add
     * @return true if level up occurred
     */
    bool AddExperience(uint64_t amount);

    /**
     * Get experience needed for next level
     */
    uint64_t GetExperienceForNextLevel() const;

    // ============ DAMAGE CALCULATION ============

    /**
     * Calculate damage after armor and resistances
     * @param damage Base damage
     * @param armor_penetration How much armor to ignore
     * @param element Element type for resistance calculation
     * @return Final damage to apply
     */
    float CalculateFinalDamage(float damage, float armor_penetration = 0.0f,
                               ElementType element = ElementType::Fire) const;

private:
    entt::entity entity_;

    // Health system
    float health_ = 100.0f;
    float max_health_ = 100.0f;
    float health_regen_rate_ = 0.0f;

    // Stamina system
    float stamina_ = 100.0f;
    float max_stamina_ = 100.0f;
    float stamina_regen_rate_ = 15.0f;

    // Defense
    float armor_ = 0.0f;
    std::array<float, static_cast<size_t>(ElementType::Count)> resistances_{};

    // Progression
    uint8_t level_ = 1;
    uint64_t experience_ = 0;

    // Temporary effects
    struct TemporaryEffect {
        float bonus;
        float remaining_duration;
    };
    std::array<std::vector<TemporaryEffect>, static_cast<size_t>(ElementType::Count)> temporary_resistances_;

    // Helper to get XP needed for a level
    static uint64_t GetXPForLevel(uint8_t level);
};

} // namespace engine::character
