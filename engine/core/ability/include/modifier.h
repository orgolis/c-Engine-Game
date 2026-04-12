#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

namespace engine::ability {

/**
 * @enum ModifierOperation
 * @brief How modifiers apply to a stat
 */
enum class ModifierOperation {
    Add,        // Add flat amount: base + modifier
    Multiply,   // Multiply by percentage: base * (1 + modifier)
    Set         // Override the value: modifier (highest priority)
};

/**
 * @struct Modifier
 * @brief A single stat modification
 *
 * The modifier pipeline applies in order:
 * 1. All Add modifiers sum together
 * 2. All Multiply modifiers multiply together
 * 3. All Set modifiers take the highest value
 *
 * Example: 50 base damage
 * - Add 10: 50 + 10 = 60
 * - Multiply 0.5 (1.5x): 60 * 1.5 = 90
 * - Set 100: final = 100 (if it's the highest set)
 */
struct Modifier {
    std::string stat_name;          // "damage", "attack_speed", "health_regen"
    float value = 0.0f;
    ModifierOperation operation = ModifierOperation::Add;
    std::string source = "";        // "Fireball Ability", "Demon Armor Set", etc.
    float duration = 0.0f;          // 0 = permanent, >0 = temporary
    
    Modifier() = default;
    Modifier(const std::string& name, float val, ModifierOperation op, 
             const std::string& src = "", float dur = 0.0f)
        : stat_name(name), value(val), operation(op), source(src), duration(dur) {}
};

/**
 * @class ModifierStack
 * @brief Manages a stack of temporary and permanent modifiers for a stat
 *
 * This implements the Destiny 2-style modifier pipeline where multiple
 * sources can contribute to the final stat value.
 */
class ModifierStack {
public:
    ModifierStack() = default;

    /**
     * Add a modifier to the stack
     * @param modifier The modifier to add
     */
    void AddModifier(const Modifier& modifier);

    /**
     * Remove a modifier by source
     * @param source Source identifier (e.g., "Fireball Ability")
     */
    void RemoveModifierBySource(const std::string& source);

    /**
     * Calculate the final modified value
     * @param base_value The unmodified stat value
     * @return The final value after all modifiers applied
     */
    float CalculateModifiedValue(float base_value) const;

    /**
     * Get all modifiers
     */
    const std::vector<Modifier>& GetModifiers() const { return modifiers_; }

    /**
     * Get total Add bonus
     */
    float GetAddBonus() const;

    /**
     * Get total Multiply bonus (as multiplier, e.g., 1.5x = 0.5)
     */
    float GetMultiplyBonus() const;

    /**
     * Get Set overrides (returns highest)
     */
    float GetSetOverride() const;

    /**
     * Check if any Set modifiers exist
     */
    bool HasSetModifier() const;

    /**
     * Update temporary modifiers
     * @param dt Delta time in seconds
     */
    void Update(float dt);

    /**
     * Clear all modifiers
     */
    void Clear();

    /**
     * Get modifier count
     */
    size_t GetModifierCount() const { return modifiers_.size(); }

private:
    std::vector<Modifier> modifiers_;
};

/**
 * @class ModifierDatabase
 * @brief Central registry of modifier stacks for all stats
 */
class ModifierDatabase {
public:
    /**
     * Add a modifier to a stat
     * @param stat_name Name of the stat to modify
     * @param modifier The modifier to add
     */
    void AddModifier(const std::string& stat_name, const Modifier& modifier);

    /**
     * Calculate the modified value for a stat
     * @param stat_name Name of the stat
     * @param base_value Base stat value
     * @return Modified value
     */
    float GetModifiedValue(const std::string& stat_name, float base_value);

    /**
     * Remove all modifiers for a source
     */
    void RemoveModifiersFromSource(const std::string& source);

    /**
     * Update all temporary modifiers
     */
    void Update(float dt);

    /**
     * Get info about a stat's modifiers
     */
    ModifierStack* GetStack(const std::string& stat_name);

private:
    std::unordered_map<std::string, ModifierStack> stacks_;
};

} // namespace engine::ability
