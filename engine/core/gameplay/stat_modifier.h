#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace schizo::gameplay {

/**
 * @brief Stat modifier - Destiny 2-style buff/debuff system
 * 
 * Modifiers add/multiply stat values. Multiple modifiers stack and aggregate
 * into final calculated values. This enables complex builds with many active effects.
 * 
 * Example: Base Health 100 → +20 armor modifier → ×1.1 toughness modifier = 115 health
 */
enum class ModifierType : uint8_t {
    Additive,       // +20 health
    Multiplicative, // ×1.1 health multiplier
};

enum class StatType : uint8_t {
    // Basic stats
    Health,
    Mana,
    Stamina,
    
    // Offense
    Damage,
    WeaponDamage,
    SpellDamage,
    CritChance,
    CritDamage,
    
    // Defense
    Armor,
    MagicResist,
    Evasion,
    
    // Mobility
    MoveSpeed,
    JumpHeight,
    DashSpeed,
    
    // Recovery
    HealthRegen,
    ManaRegen,
    StaminaRegen,
    
    // Ability
    AbilityCooldown,          // Multiplier: 0.8 = 20% faster
    AbilityDuration,         // Multiplier: 1.2 = 20% longer
    AbilityChargeCount,      // Additive: +1 charge
    AbilityEffectiveness,    // Multiplier: 1.1 = 10% more potent
    
    // Special
    Luck,
    Count
};

/**
 * @brief Individual stat modifier
 * 
 * Applied to a stat and aggregated during calculation.
 * Can be from equipment, buffs, debuffs, conditions, etc.
 */
struct StatModifier {
    StatType stat;
    ModifierType type;
    float value;              // For additive: amount to add. For multiplicative: multiplier (1.0 = no change)
    std::string source;       // "Armor Plate", "Ethereal Form", "Buff_HasteLevel2", etc.
    float duration = -1.0f;   // -1 = permanent, >= 0 = seconds remaining
    bool active = true;       // Can be disabled without removing
    
    StatModifier() = default;
    StatModifier(StatType s, ModifierType t, float v, const std::string& src, float dur = -1.0f)
        : stat(s), type(t), value(v), source(src), duration(dur) {}
};

/**
 * @brief Modifier source - tracks where a modifier came from
 * 
 * Used for debugging, UI display, and effect management.
 */
struct ModifierSource {
    enum class Type : uint8_t {
        Equipment,        // Armor, weapons
        ActiveEffect,     // Buff from ability/item on cooldown
        Condition,        // Bleeding, poisoned, chilled
        Passive,          // Always active from perks
        Temporary,        // Timed buff
        Status            // Stunned, frozen, etc.
    };
    
    Type type;
    std::string name;
    std::vector<StatModifier> modifiers;
    float time_remaining = -1.0f;  // -1 = permanent
    
    ModifierSource() = default;
    ModifierSource(Type t, const std::string& n) : type(t), name(n) {}
    
    void AddModifier(const StatModifier& mod) {
        modifiers.push_back(mod);
    }
    
    bool IsExpired(float delta_time) {
        if (time_remaining < 0.0f) return false;  // Permanent
        time_remaining -= delta_time;
        return time_remaining <= 0.0f;
    }
};

/**
 * @brief Aggregated stat value with breakdown
 * 
 * Tracks base value and all applied modifiers for debugging/UI.
 */
struct AggregatedStat {
    StatType stat;
    float base_value;
    float additive_bonus = 0.0f;
    float multiplicative_bonus = 1.0f;
    
    // Breakdown for debugging
    struct Breakdown {
        std::string source;
        float base_contribution;
        float additive_contribution;
        float multiplicative_contribution;
    };
    std::vector<Breakdown> breakdown;
    
    /**
     * @brief Calculate final value: (base + additive) * multiplicative
     */
    float GetFinalValue() const {
        return (base_value + additive_bonus) * multiplicative_bonus;
    }
    
    void Clear() {
        additive_bonus = 0.0f;
        multiplicative_bonus = 1.0f;
        breakdown.clear();
    }
};

/**
 * @brief Stat modifier manager
 * 
 * Aggregates modifiers into final stat values.
 * Handles cleanup of expired temporary modifiers.
 */
class ModifierManager {
public:
    ModifierManager() = default;
    virtual ~ModifierManager() = default;
    
    /**
     * @brief Add a modifier source (equipment, buff, debuff, etc.)
     */
    virtual void AddModifierSource(const std::shared_ptr<ModifierSource>& source) = 0;
    
    /**
     * @brief Remove a modifier source
     */
    virtual void RemoveModifierSource(const std::string& source_name) = 0;
    
    /**
     * @brief Update temporary modifiers (decay time_remaining)
     */
    virtual void Update(float delta_time) = 0;
    
    /**
     * @brief Get aggregated stat value
     * 
     * Combines all active modifiers for a stat.
     * Returns AggregatedStat with breakdown for debugging.
     */
    virtual AggregatedStat GetAggregatedStat(StatType stat, float base_value) = 0;
    
    /**
     * @brief Get final calculated value for a stat
     */
    virtual float CalculateStat(StatType stat, float base_value) = 0;
    
    /**
     * @brief Factory
     */
    static std::shared_ptr<ModifierManager> Create();
};

} // namespace schizo::gameplay
