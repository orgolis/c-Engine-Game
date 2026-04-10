#pragma once

#include "stat_modifier.h"
#include <glm/glm.hpp>
#include <glm/common.hpp>
#include <memory>
#include <unordered_map>

namespace schizo::gameplay {

/**
 * @brief Character stat container with modifier aggregation
 * 
 * Combines base stats with modifier pipeline to calculate final values.
 * All stat calculations go through the modifier system, enabling:
 * - Equipment bonuses
 * - Buff/debuff stacking
 * - Temporary effects
 * - Complex build interactions
 * 
 * Stats marked with ⚠ are deterministic-critical and should NOT vary frame-to-frame
 * for networking/replay purposes.
 */
struct BaseCharacterStats {
    // Health/Resource (⚠ deterministic-critical)
    float health = 100.0f;
    float max_health = 100.0f;
    float mana = 50.0f;
    float max_mana = 50.0f;
    float stamina = 100.0f;
    float max_stamina = 100.0f;
    
    // Offense (⚠ deterministic-critical)
    float damage = 10.0f;
    float weapon_damage = 1.0f;
    float spell_damage = 1.0f;
    float crit_chance = 0.05f;  // 5%
    float crit_damage = 1.5f;   // 150%
    
    // Defense (⚠ deterministic-critical)
    float armor = 10.0f;
    float magic_resist = 5.0f;
    float evasion = 0.0f;
    
    // Mobility
    float move_speed = 5.0f;      // units/sec
    float jump_height = 2.0f;     // units
    float dash_speed = 10.0f;     // units/sec
    
    // Recovery
    float health_regen_per_sec = 0.0f;
    float mana_regen_per_sec = 1.0f;
    float stamina_regen_per_sec = 1.0f;
    
    // Ability modifiers
    float cooldown_multiplier = 1.0f;  // 0.8 = 20% faster
    float duration_multiplier = 1.0f;  // 1.2 = 20% longer
    int charge_count_bonus = 0;        // +1 additional charge
    float effectiveness_multiplier = 1.0f;
    
    // Special
    float luck = 1.0f;
};

/**
 * @brief Character stats with live modifier aggregation
 * 
 * Tracks base stats + active modifiers, calculates final values on demand.
 */
class CharacterStats {
public:
    explicit CharacterStats(const BaseCharacterStats& base = {})
        : base_stats_(base),
          modifier_manager_(ModifierManager::Create()),
          last_update_time_(0.0f) {
    }
    
    virtual ~CharacterStats() = default;
    
    // ======================
    // Base stat access (MUTABLE)
    // ======================
    
    /**
     * @brief Set base stat - bypasses modifiers
     * 
     * Use only for initialization. Live changes should be via modifiers.
     */
    void SetBaseStat(StatType stat, float value) {
        switch (stat) {
            case StatType::Health: base_stats_.health = value; break;
            case StatType::Mana: base_stats_.mana = value; break;
            case StatType::Stamina: base_stats_.stamina = value; break;
            case StatType::Damage: base_stats_.damage = value; break;
            case StatType::WeaponDamage: base_stats_.weapon_damage = value; break;
            case StatType::SpellDamage: base_stats_.spell_damage = value; break;
            case StatType::CritChance: base_stats_.crit_chance = value; break;
            case StatType::CritDamage: base_stats_.crit_damage = value; break;
            case StatType::Armor: base_stats_.armor = value; break;
            case StatType::MagicResist: base_stats_.magic_resist = value; break;
            case StatType::Evasion: base_stats_.evasion = value; break;
            case StatType::MoveSpeed: base_stats_.move_speed = value; break;
            case StatType::JumpHeight: base_stats_.jump_height = value; break;
            case StatType::DashSpeed: base_stats_.dash_speed = value; break;
            case StatType::HealthRegen: base_stats_.health_regen_per_sec = value; break;
            case StatType::ManaRegen: base_stats_.mana_regen_per_sec = value; break;
            case StatType::StaminaRegen: base_stats_.stamina_regen_per_sec = value; break;
            case StatType::AbilityCooldown: base_stats_.cooldown_multiplier = value; break;
            case StatType::AbilityDuration: base_stats_.duration_multiplier = value; break;
            case StatType::AbilityEffectiveness: base_stats_.effectiveness_multiplier = value; break;
            case StatType::Luck: base_stats_.luck = value; break;
            case StatType::Count:
            case StatType::AbilityChargeCount: break;
        }
    }
    
    // ======================
    // Final calculated stats (READ-ONLY)
    // ====================== 
    
    /**
     * @brief Get final calculated stat value with all modifiers applied
     * 
     * Returns (base + additive) * multiplicative
     * 
     * ⚠ DETERMINISTIC: Must return same value for same inputs on replay
     */
    float GetFinalStat(StatType stat) {
        float base = GetBaseStat(stat);
        return modifier_manager_->CalculateStat(stat, base);
    }
    
    /**
     * @brief Get detailed stat breakdown for UI/debugging
     */
    AggregatedStat GetStatBreakdown(StatType stat) {
        float base = GetBaseStat(stat);
        return modifier_manager_->GetAggregatedStat(stat, base);
    }
    
    // Convenience getters for final stats
    float GetMaxHealth() { return GetFinalStat(StatType::Health); }
    float GetMaxMana() { return GetFinalStat(StatType::Mana); }
    float GetMaxStamina() { return GetFinalStat(StatType::Stamina); }
    float GetDamage() { return GetFinalStat(StatType::Damage); }
    float GetArmor() { return GetFinalStat(StatType::Armor); }
    float GetMagicResist() { return GetFinalStat(StatType::MagicResist); }
    float GetMoveSpeed() { return GetFinalStat(StatType::MoveSpeed); }
    float GetJumpHeight() { return GetFinalStat(StatType::JumpHeight); }
    float GetCooldownMultiplier() { return GetFinalStat(StatType::AbilityCooldown); }
    
    // ======================
    // Resource management
    // ======================
    
    /**
     * @brief Apply healing
     * @param amount Amount to heal
     * @return Actual amount healed
     */
    virtual float Heal(float amount) {
        float max_hp = GetMaxHealth();
        float prev_hp = base_stats_.health;
        base_stats_.health = glm::min(base_stats_.health + amount, max_hp);
        return base_stats_.health - prev_hp;
    }
    
    /**
     * @brief Apply damage
     * @param amount Raw damage
     * @return Actual damage taken (after armor)
     */
    virtual float TakeDamage(float amount) {
        float armor = GetArmor();
        float armor_reduction = 1.0f / (1.0f + armor / 100.0f);  // DR formula
        float final_damage = amount * armor_reduction;
        
        base_stats_.health -= final_damage;
        base_stats_.health = glm::max(base_stats_.health, 0.0f);
        return final_damage;
    }
    
    /**
     * @brief Check if character is alive
     */
    bool IsAlive() const {
        return base_stats_.health > 0.0f;
    }
    
    // ======================
    // Modifier management
    // ======================
    
    /**
     * @brief Add a modifier source (buff, equipment, debuff, etc.)
     */
    void AddModifier(const std::shared_ptr<ModifierSource>& source) {
        modifier_manager_->AddModifierSource(source);
    }
    
    /**
     * @brief Remove modifier source by name
     */
    void RemoveModifier(const std::string& source_name) {
        modifier_manager_->RemoveModifierSource(source_name);
    }
    
    /**
     * @brief Update temporary modifiers (decay timers, remove expired)
     */
    void Update(float delta_time) {
        last_update_time_ += delta_time;
        modifier_manager_->Update(delta_time);
        
        // Auto-regen resources
        base_stats_.health = glm::min(
            base_stats_.health + base_stats_.health_regen_per_sec * delta_time,
            GetMaxHealth()
        );
        base_stats_.mana = glm::min(
            base_stats_.mana + base_stats_.mana_regen_per_sec * delta_time,
            GetMaxMana()
        );
        base_stats_.stamina = glm::min(
            base_stats_.stamina + base_stats_.stamina_regen_per_sec * delta_time,
            GetMaxStamina()
        );
    }
    
    /**
     * @brief Get base stats (before modifiers)
     */
    const BaseCharacterStats& GetBaseStats() const {
        return base_stats_;
    }
    
protected:
    BaseCharacterStats base_stats_;
    std::shared_ptr<ModifierManager> modifier_manager_;
    float last_update_time_;
    
private:
    float GetBaseStat(StatType stat) {
        switch (stat) {
            case StatType::Health: return base_stats_.health;
            case StatType::Mana: return base_stats_.mana;
            case StatType::Stamina: return base_stats_.stamina;
            case StatType::Damage: return base_stats_.damage;
            case StatType::WeaponDamage: return base_stats_.weapon_damage;
            case StatType::SpellDamage: return base_stats_.spell_damage;
            case StatType::CritChance: return base_stats_.crit_chance;
            case StatType::CritDamage: return base_stats_.crit_damage;
            case StatType::Armor: return base_stats_.armor;
            case StatType::MagicResist: return base_stats_.magic_resist;
            case StatType::Evasion: return base_stats_.evasion;
            case StatType::MoveSpeed: return base_stats_.move_speed;
            case StatType::JumpHeight: return base_stats_.jump_height;
            case StatType::DashSpeed: return base_stats_.dash_speed;
            case StatType::HealthRegen: return base_stats_.health_regen_per_sec;
            case StatType::ManaRegen: return base_stats_.mana_regen_per_sec;
            case StatType::StaminaRegen: return base_stats_.stamina_regen_per_sec;
            case StatType::AbilityCooldown: return base_stats_.cooldown_multiplier;
            case StatType::AbilityDuration: return base_stats_.duration_multiplier;
            case StatType::AbilityEffectiveness: return base_stats_.effectiveness_multiplier;
            case StatType::Luck: return base_stats_.luck;
            case StatType::Count:
            case StatType::AbilityChargeCount:
            default: return 0.0f;
        }
    }
};

} // namespace schizo::gameplay
