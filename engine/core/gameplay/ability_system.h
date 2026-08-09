#pragma once

#include <cstdint>
#include "character_stats.h"
#include "../input/input_action.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace schizo::gameplay {

// Forward declaration
class CharacterStats;
class AbilityEffect;

/**
 * @brief Ability definition and state
 * 
 * Represents a single ability that a character can use.
 * Abilities are immutable, shared templates. AbilityState tracks active instances.
 * 
 * Supports:
 * - Cooldown with charges (Fire 3 shots then wait for cooldown)
 * - Activation cost (mana, stamina)
 * - Duration (effects persist after activation)
 * - Effects (damage, healing, buffs, etc.)
 */
class Ability {
public:
    enum class ActivationType : uint8_t {
        Instant,        // Fires immediately on press
        Channel,        // Hold to charge
        Cast,           // Press, brief animation, triggers
        Toggle,         // Press toggles on/off
    };
    
    Ability(const std::string& name, uint32_t id = 0)
        : name_(name), id_(id), charges_(1), cooldown_seconds_(5.0f),
          mana_cost_(0.0f), stamina_cost_(0.0f), duration_(0.0f),
          activation_type_(ActivationType::Instant) {
    }
    
    virtual ~Ability() = default;
    
    // ======================
    // Properties
    // ======================
    
    const std::string& GetName() const { return name_; }
    uint32_t GetID() const { return id_; }
    
    void SetDescription(const std::string& desc) { description_ = desc; }
    const std::string& GetDescription() const { return description_; }
    
    // ======================
    // Cost & Activation
    // ======================
    
    void SetActivationType(ActivationType type) { activation_type_ = type; }
    ActivationType GetActivationType() const { return activation_type_; }
    
    void SetCharges(int charges) { charges_ = charges; }
    int GetCharges() const { return charges_; }
    
    void SetCooldown(float seconds) { cooldown_seconds_ = seconds; }
    float GetCooldown() const { return cooldown_seconds_; }
    
    void SetManaCost(float mana) { mana_cost_ = mana; }
    float GetManaCost() const { return mana_cost_; }
    
    void SetStaminaCost(float stamina) { stamina_cost_ = stamina; }
    float GetStaminaCost() const { return stamina_cost_; }
    
    void SetDuration(float seconds) { duration_ = seconds; }
    float GetDuration() const { return duration_; }
    
    // ======================
    // Ability Effects
    // ======================
    
    void AddEffect(std::shared_ptr<AbilityEffect> effect) {
        effects_.push_back(effect);
    }
    
    const std::vector<std::shared_ptr<AbilityEffect>>& GetEffects() const {
        return effects_;
    }
    
    // ======================
    // Validation
    // ======================
    
    /**
     * @brief Check if ability can be cast (cost available, cooldown ready)
     */
    virtual bool CanActivate(const std::shared_ptr<CharacterStats>& stats) const {
        // ⚠ DETERMINISTIC: No RNG, no frame-dependent checks
        return stats->GetFinalStat(StatType::Mana) >= mana_cost_ &&
               stats->GetFinalStat(StatType::Stamina) >= stamina_cost_;
    }
    
protected:
    std::string name_;
    uint32_t id_;
    std::string description_;
    
    int charges_;
    float cooldown_seconds_;
    float mana_cost_;
    float stamina_cost_;
    float duration_;
    ActivationType activation_type_;
    
    std::vector<std::shared_ptr<AbilityEffect>> effects_;
};

/**
 * @brief Active ability instance with state
 * 
 * Tracks cooldown, charges, duration of an active ability use.
 */
struct ActiveAbility {
    std::shared_ptr<Ability> ability;
    
    int charges_remaining;
    float cooldown_remaining = 0.0f;
    float duration_remaining = 0.0f;  // How long effect persists
    bool is_active = false;
    
    float charge_regeneration_rate;  // 1.0 = regain 1 charge per cooldown
    
    ActiveAbility(std::shared_ptr<Ability> ab, float charge_rate = 1.0f)
        : ability(ab), 
          charges_remaining(ab->GetCharges()),
          charge_regeneration_rate(charge_rate) {
    }
    
    /**
     * @brief Can this ability be activated right now?
     * 
     * ⚠ DETERMINISTIC: No RNG here, cost checks done elsewhere
     */
    bool IsReady() const {
        return charges_remaining > 0 && cooldown_remaining <= 0.0f;
    }
    
    /**
     * @brief Activate ability, start cooldown
     */
    void Activate(float cooldown_multiplier = 1.0f) {
        charges_remaining--;
        cooldown_remaining = ability->GetCooldown() * cooldown_multiplier;
        duration_remaining = ability->GetDuration();
        is_active = true;
    }
    
    /**
     * @brief Update cooldown and charge regeneration
     */
    void Update(float delta_time) {
        if (cooldown_remaining > 0.0f) {
            cooldown_remaining -= delta_time;
        }
        
        // Regenerate charges
        if (charges_remaining < ability->GetCharges()) {
            charges_remaining++;
            cooldown_remaining = ability->GetCooldown();
        }
        
        // Decay duration
        if (duration_remaining > 0.0f) {
            duration_remaining -= delta_time;
            if (duration_remaining <= 0.0f) {
                is_active = false;
            }
        }
    }
};

/**
 * @brief Ability loadout
 * 
 * A character's current set of ready abilities.
 * Destiny 2 model: 3 ability slots + 1 ultimate
 */
struct AbilityLoadout {
    std::shared_ptr<ActiveAbility> slot1;
    std::shared_ptr<ActiveAbility> slot2;
    std::shared_ptr<ActiveAbility> slot3;
    std::shared_ptr<ActiveAbility> slot4;
    std::shared_ptr<ActiveAbility> ultimate;
    
    std::vector<std::shared_ptr<ActiveAbility>> GetAllAbilities() {
        return { slot1, slot2, slot3, slot4, ultimate };
    }
};

/**
 * @brief Ability effect base class
 * 
 * Effects are executed when ability activates:
 * - Damage to targets
 * - Healing
 * - Buffs/debuffs
 * - Knockback
 * - Status effects
 */
class AbilityEffect {
public:
    virtual ~AbilityEffect() = default;
    
    /**
     * @brief Apply this effect
     * 
     * @param caster Character casting the ability
     * @param target Character receiving the effect (nullptr = apply to caster)
     * @param effectiveness Stat modifier for effect potency
     * 
     * ⚠ DETERMINISTIC: RNG must use seeded generator, not platform RNG
     */
    virtual void Apply(
        const std::shared_ptr<CharacterStats>& caster,
        const std::shared_ptr<CharacterStats>& target,
        float effectiveness = 1.0f
    ) = 0;
    
    virtual std::string GetName() const = 0;
};

/**
 * @brief Damage effect
 */
class DamageEffect : public AbilityEffect {
private:
    float base_damage_;
    bool scales_with_caster_damage_ = true;
    
public:
    explicit DamageEffect(float damage)
        : base_damage_(damage) {
    }
    
    void Apply(
        const std::shared_ptr<CharacterStats>& caster,
        const std::shared_ptr<CharacterStats>& target,
        float effectiveness = 1.0f
    ) override;
    
    std::string GetName() const override { return "Damage"; }
};

/**
 * @brief Healing effect
 */
class HealingEffect : public AbilityEffect {
private:
    float base_healing_;
    
public:
    explicit HealingEffect(float healing)
        : base_healing_(healing) {
    }
    
    void Apply(
        const std::shared_ptr<CharacterStats>& caster,
        const std::shared_ptr<CharacterStats>& target,
        float effectiveness = 1.0f
    ) override;
    
    std::string GetName() const override { return "Healing"; }
};

/**
 * @brief Status effect (buff/debuff)
 */
class StatusEffectComponent : public AbilityEffect {
private:
    std::shared_ptr<ModifierSource> modifier_source_;
    
public:
    explicit StatusEffectComponent(const std::shared_ptr<ModifierSource>& modifier)
        : modifier_source_(modifier) {
    }
    
    void Apply(
        const std::shared_ptr<CharacterStats>& caster,
        const std::shared_ptr<CharacterStats>& target,
        float effectiveness = 1.0f
    ) override;
    
    std::string GetName() const override {
        return modifier_source_ ? modifier_source_->name : "Unknown Effect";
    }
};

} // namespace schizo::gameplay
