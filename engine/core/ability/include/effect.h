#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entt.hpp>

namespace engine::ability {

/**
 * @enum EffectType
 * @brief Different categories of effects
 */
enum class EffectType {
    Damage,
    Heal,
    StatusEffect,
    Modifier,  // Stat modification
    Summon,
    Teleport,
    Custom
};

/**
 * @enum StatusEffectType
 * @brief Debuffs and buffs
 */
enum class StatusEffectType {
    Stun,       // Can't move or act
    Slow,       // Movement speed reduced
    Burn,       // Damage over time (fire)
    Poison,     // Damage over time (poison)
    Bleed,      // Damage over time (physical)
    Frozen,     // Immobilized
    Weakness,   // Damage reduced
    Strength,   // Damage increased
    Invisible,  // Can't be targeted
    Invulnerable, // Take no damage
    Count
};

/**
 * @struct EffectResult
 * @brief Information about an effect application
 */
struct EffectResult {
    bool success = false;
    std::string message;
    float damage_dealt = 0.0f;
    float healing_applied = 0.0f;
};

/**
 * @class Effect
 * @brief Base class for ability effects
 *
 * Effects are what actually happen when an ability activates:
 * - Fireball casts -> DamageEffect to all entities in radius
 * - Heal spell -> HealEffect to target
 * - Poison cloud -> StatusEffect (poison) to all in area
 */
class Effect {
public:
    virtual ~Effect() = default;

    /**
     * Get effect type
     */
    virtual EffectType GetType() const = 0;

    /**
     * Get human-readable name
     */
    virtual const std::string& GetName() const = 0;

    /**
     * Apply the effect to a target
     * @param caster Entity that cast the ability
     * @param target Entity to apply effect to
     * @param magnitude Base magnitude of the effect (damage, healing, etc)
     */
    virtual EffectResult Apply(entt::entity caster, entt::entity target, float magnitude = 1.0f) = 0;

    /**
     * Update effect state (for DoT effects, etc)
     * @param target Entity being affected
     * @param dt Delta time
     */
    virtual void Update(entt::entity target, float dt) {}

    /**
     * Check if effect should be removed
     */
    virtual bool IsFinished() const { return true; }
};

/**
 * @class DamageEffect
 * @brief Applies damage to targets
 */
class DamageEffect : public Effect {
public:
    enum class DamageType {
        Physical,
        Fire,
        Cold,
        Lightning,
        Poison,
        Holy,
        Count
    };

    explicit DamageEffect(float base_damage, DamageType type = DamageType::Physical);

    EffectType GetType() const override { return EffectType::Damage; }
    const std::string& GetName() const override;
    EffectResult Apply(entt::entity caster, entt::entity target, float magnitude = 1.0f) override;

private:
    float base_damage_;
    DamageType damage_type_;
};

/**
 * @class HealEffect
 * @brief Restores health to targets
 */
class HealEffect : public Effect {
public:
    explicit HealEffect(float heal_amount);

    EffectType GetType() const override { return EffectType::Heal; }
    const std::string& GetName() const override;
    EffectResult Apply(entt::entity caster, entt::entity target, float magnitude = 1.0f) override;

private:
    float heal_amount_;
};

/**
 * @class StatusEffect
 * @brief Applies status effects (buffs/debuffs)
 */
class StatusEffect : public Effect {
public:
    explicit StatusEffect(StatusEffectType status_type, float duration = 5.0f);

    EffectType GetType() const override { return EffectType::StatusEffect; }
    const std::string& GetName() const override;
    EffectResult Apply(entt::entity caster, entt::entity target, float magnitude = 1.0f) override;
    void Update(entt::entity target, float dt) override;
    bool IsFinished() const override;

private:
    StatusEffectType status_type_;
    float duration_;
    float remaining_time_ = 0.0f;
};

/**
 * @class DamageOverTimeEffect
 * @brief Applies periodic damage (burn, poison, bleed)
 */
class DamageOverTimeEffect : public Effect {
public:
    explicit DamageOverTimeEffect(float damage_per_second, float duration, 
                                 DamageEffect::DamageType damage_type = DamageEffect::DamageType::Poison);

    EffectType GetType() const override { return EffectType::Damage; }
    const std::string& GetName() const override;
    EffectResult Apply(entt::entity caster, entt::entity target, float magnitude = 1.0f) override;
    void Update(entt::entity target, float dt) override;
    bool IsFinished() const override;

private:
    float damage_per_second_;
    float duration_;
    float remaining_time_ = 0.0f;
    float tick_interval_ = 0.5f;
    float tick_timer_ = 0.0f;
    DamageEffect::DamageType damage_type_;
};

} // namespace engine::ability
