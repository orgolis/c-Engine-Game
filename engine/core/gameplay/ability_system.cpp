#include "ability_system.h"
#include <spdlog/spdlog.h>

namespace schizo::gameplay {

void DamageEffect::Apply(
    const std::shared_ptr<CharacterStats>& caster,
    const std::shared_ptr<CharacterStats>& target,
    float effectiveness
) {
    if (!target) return;
    
    float damage = base_damage_;
    
    // Scale with caster damage if enabled
    if (scales_with_caster_damage_ && caster) {
        damage *= caster->GetFinalStat(StatType::Damage);
    }
    
    // Apply effectiveness multiplier
    damage *= effectiveness;
    
    // Apply to target (handles armor reduction)
    float actual_damage = target->TakeDamage(damage);
    
    auto logger = spdlog::get("engine");
    if (logger) {
        logger->debug("Damage effect applied: {} damage dealt", actual_damage);
    }
}

void HealingEffect::Apply(
    const std::shared_ptr<CharacterStats>& caster,
    const std::shared_ptr<CharacterStats>& target,
    float effectiveness
) {
    if (!target) return;
    
    float healing = base_healing_ * effectiveness;
    float actual_healing = target->Heal(healing);
    
    auto logger = spdlog::get("engine");
    if (logger) {
        logger->debug("Healing effect applied: {} HP restored", actual_healing);
    }
}

void StatusEffectComponent::Apply(
    const std::shared_ptr<CharacterStats>& caster,
    const std::shared_ptr<CharacterStats>& target,
    float effectiveness
) {
    if (!target || !modifier_source_) return;
    
    target->AddModifier(modifier_source_);
    
    auto logger = spdlog::get("engine");
    if (logger) {
        logger->debug("Status effect applied: {}", modifier_source_->name);
    }
}

} // namespace schizo::gameplay
