#include "effect.h"
#include <spdlog/spdlog.h>

namespace engine::ability {

// ============ DamageEffect ============

DamageEffect::DamageEffect(float base_damage, DamageType type)
    : base_damage_(base_damage), damage_type_(type) {
}

const std::string& DamageEffect::GetName() const {
    static const std::string name = "Damage Effect";
    return name;
}

EffectResult DamageEffect::Apply(entt::entity caster, entt::entity target, float magnitude) {
    EffectResult result;
    result.success = true;
    result.damage_dealt = base_damage_ * magnitude;

    auto logger = spdlog::get("engine");
    if (logger) {
        logger->debug("DamageEffect applied: {} damage", result.damage_dealt);
    }

    return result;
}

// ============ HealEffect ============

HealEffect::HealEffect(float heal_amount)
    : heal_amount_(heal_amount) {
}

const std::string& HealEffect::GetName() const {
    static const std::string name = "Heal Effect";
    return name;
}

EffectResult HealEffect::Apply(entt::entity caster, entt::entity target, float magnitude) {
    EffectResult result;
    result.success = true;
    result.healing_applied = heal_amount_ * magnitude;

    auto logger = spdlog::get("engine");
    if (logger) {
        logger->debug("HealEffect applied: {} healing", result.healing_applied);
    }

    return result;
}

// ============ StatusEffect ============

StatusEffect::StatusEffect(StatusEffectType status_type, float duration)
    : status_type_(status_type), duration_(duration) {
}

const std::string& StatusEffect::GetName() const {
    static const std::string name = "Status Effect";
    return name;
}

EffectResult StatusEffect::Apply(entt::entity caster, entt::entity target, float magnitude) {
    EffectResult result;
    result.success = true;
    remaining_time_ = duration_;

    auto logger = spdlog::get("engine");
    if (logger) {
        logger->debug("StatusEffect applied: duration {}", duration_);
    }

    return result;
}

void StatusEffect::Update(entt::entity target, float dt) {
    remaining_time_ -= dt;
}

bool StatusEffect::IsFinished() const {
    return remaining_time_ <= 0.0f;
}

// ============ DamageOverTimeEffect ============

DamageOverTimeEffect::DamageOverTimeEffect(float damage_per_second, float duration,
                                          DamageEffect::DamageType damage_type)
    : damage_per_second_(damage_per_second), duration_(duration), damage_type_(damage_type) {
}

const std::string& DamageOverTimeEffect::GetName() const {
    static const std::string name = "Damage Over Time Effect";
    return name;
}

EffectResult DamageOverTimeEffect::Apply(entt::entity caster, entt::entity target, float magnitude) {
    EffectResult result;
    result.success = true;
    remaining_time_ = duration_;
    result.damage_dealt = 0.0f;  // Initial damage is 0, happens over time

    auto logger = spdlog::get("engine");
    if (logger) {
        logger->debug("DamageOverTimeEffect applied: {} dps for {} seconds", 
                     damage_per_second_, duration_);
    }

    return result;
}

void DamageOverTimeEffect::Update(entt::entity target, float dt) {
    remaining_time_ -= dt;
    tick_timer_ -= dt;

    if (tick_timer_ <= 0.0f) {
        // Apply tick damage
        float damage = damage_per_second_ * tick_interval_;
        tick_timer_ = tick_interval_;

        auto logger = spdlog::get("engine");
        if (logger) {
            logger->debug("DoT tick: {} damage", damage);
        }
    }
}

bool DamageOverTimeEffect::IsFinished() const {
    return remaining_time_ <= 0.0f;
}

} // namespace engine::ability
