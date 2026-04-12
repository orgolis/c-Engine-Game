#include "ability.h"
#include "effect.h"
#include <spdlog/spdlog.h>

namespace engine::ability {

// ============ ActiveAbility ============

ActiveAbility::ActiveAbility(const AbilityConfig& config)
    : config_(config) {
}

std::string ActiveAbility::CanCast(entt::entity caster) const {
    // Check cooldown
    if (cooldown_remaining_ > 0.0f) {
        return "Ability is on cooldown";
    }
    // Check mana/stamina (in real implementation, would access character stats)
    return "";  // Can cast
}

bool ActiveAbility::Activate(entt::entity caster, entt::entity target, const glm::vec3& position) {
    auto error = CanCast(caster);
    if (!error.empty()) {
        auto logger = spdlog::get("engine");
        if (logger) logger->debug("Cannot cast {}: {}", config_.name, error);
        return false;
    }

    // Start cooldown
    cooldown_remaining_ = config_.cooldown_duration;

    // Call subclass implementation
    OnActivate(caster, target, position);

    // Apply effects
    if (target != entt::null) {
        ApplyEffects(target);
    }

    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Ability {} activated", config_.name);

    return true;
}

void ActiveAbility::ApplyEffects(entt::entity target) {
    for (auto& effect : effects_) {
        if (effect) {
            effect->Apply(entt::null, target, 1.0f);
        }
    }
}

void ActiveAbility::Update(float dt) {
    if (cooldown_remaining_ > 0.0f) {
        cooldown_remaining_ -= dt;
    }

    // Update effects
    for (auto& effect : effects_) {
        if (effect) {
            effect->Update(entt::null, dt);
        }
    }
}

void ActiveAbility::Reset() {
    cooldown_remaining_ = 0.0f;
    effects_.clear();
}

// ============ PassiveAbility ============

PassiveAbility::PassiveAbility(const AbilityConfig& config)
    : config_(config) {
}

void PassiveAbility::ApplyEffects(entt::entity target) {
    // Passive abilities apply their modifiers continuously
    // This would typically be handled by the character stats system
}

void PassiveAbility::Update(float dt) {
    // Passive abilities don't need per-frame updates typically
}

void PassiveAbility::Reset() {
    modifiers_.clear();
}

// ============ ChanneledAbility ============

ChanneledAbility::ChanneledAbility(const AbilityConfig& config)
    : config_(config) {
}

std::string ChanneledAbility::CanCast(entt::entity caster) const {
    if (cooldown_remaining_ > 0.0f) {
        return "Ability is on cooldown";
    }
    return "";
}

bool ChanneledAbility::Activate(entt::entity caster, entt::entity target, const glm::vec3& position) {
    auto error = CanCast(caster);
    if (!error.empty()) {
        return false;
    }

    is_channeling_ = true;
    channel_time_ = 0.0f;
    channel_power_ = 0.0f;

    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Ability {} channeling started", config_.name);

    return true;
}

float ChanneledAbility::UpdateChannel(float dt) {
    if (!is_channeling_) return 0.0f;

    channel_time_ += dt;
    
    if (config_.channel_duration > 0.0f) {
        channel_power_ = glm::min(1.0f, channel_time_ / config_.channel_duration);
    } else {
        channel_power_ = glm::min(1.0f, channel_time_ / 1.0f);  // Default 1 second
    }

    OnChannelUpdate(channel_power_, dt);
    return channel_power_;
}

void ChanneledAbility::ReleaseChannel(float power) {
    if (!is_channeling_) return;

    is_channeling_ = false;
    channel_power_ = power;

    OnChannelRelease(power);

    // Start cooldown
    cooldown_remaining_ = config_.cooldown_duration;

    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Ability {} released with power {}", config_.name, power);
}

void ChanneledAbility::ApplyEffects(entt::entity target) {
    for (auto& effect : effects_) {
        if (effect) {
            effect->Apply(entt::null, target, channel_power_);
        }
    }
}

void ChanneledAbility::Update(float dt) {
    if (cooldown_remaining_ > 0.0f) {
        cooldown_remaining_ -= dt;
    }
}

void ChanneledAbility::Reset() {
    is_channeling_ = false;
    channel_time_ = 0.0f;
    channel_power_ = 0.0f;
    cooldown_remaining_ = 0.0f;
    effects_.clear();
}

} // namespace engine::ability
