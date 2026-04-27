#include "ability_system.h"
#include <spdlog/spdlog.h>

namespace engine::ability {

AbilitySystem::AbilitySystem(entt::entity entity)
    : entity_(entity) {
}

bool AbilitySystem::AddAbility(std::shared_ptr<Ability> ability) {
    if (!ability) return false;

    const auto& name = ability->GetName();
    if (abilities_.find(name) != abilities_.end()) {
        auto logger = spdlog::get("engine");
        if (logger) logger->warn("Ability {} already exists", name);
        return false;
    }

    abilities_[name] = ability;

    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Ability {} added to system", name);

    return true;
}

bool AbilitySystem::RemoveAbility(const std::string& ability_name) {
    auto it = abilities_.find(ability_name);
    if (it == abilities_.end()) {
        return false;
    }

    abilities_.erase(it);

    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Ability {} removed from system", ability_name);

    return true;
}

Ability* AbilitySystem::GetAbility(const std::string& ability_name) {
    auto it = abilities_.find(ability_name);
    if (it == abilities_.end()) {
        return nullptr;
    }
    return it->second.get();
}

const Ability* AbilitySystem::GetAbility(const std::string& ability_name) const {
    auto it = abilities_.find(ability_name);
    if (it == abilities_.end()) {
        return nullptr;
    }
    return it->second.get();
}

bool AbilitySystem::ActivateAbility(const std::string& ability_name, entt::entity target,
                                    const glm::vec3& position) {
    Ability* ability = GetAbility(ability_name);
    if (!ability) {
        auto logger = spdlog::get("engine");
        if (logger) logger->warn("Ability {} not found", ability_name);
        return false;
    }

    // Check if can activate
    std::string error = ability->CanCast(entity_);
    if (!error.empty()) {
        auto logger = spdlog::get("engine");
        if (logger) logger->debug("Cannot activate {}: {}", ability_name, error);
        return false;
    }

    // Activate the ability
    bool success = ability->Activate(entity_, target, position);
    if (success) {
        // Start cooldown
        cooldown_manager_.StartCooldown(ability_name, ability->GetConfig().cooldown_duration);
    }

    return success;
}

std::string AbilitySystem::CanActivateAbility(const std::string& ability_name) {
    Ability* ability = GetAbility(ability_name);
    if (!ability) {
        return "Ability not found";
    }

    // Check cooldown first
    if (!cooldown_manager_.IsReady(ability_name)) {
        float remaining = cooldown_manager_.GetRemainingCooldown(ability_name);
        return "Ability on cooldown for " + std::to_string(remaining) + "s";
    }

    // Check ability-specific requirements
    return ability->CanCast(entity_);
}

bool AbilitySystem::StartChannel(const std::string& ability_name) {
    Ability* ability = GetAbility(ability_name);
    if (!ability || ability->GetType() != AbilityType::Channeled) {
        return false;
    }

    if (!cooldown_manager_.IsReady(ability_name)) {
        return false;
    }

    channeling_ability_ = ability_name;
    ability->Activate(entity_, entt::null, glm::vec3(0.0f));
    channel_start_time_ = 0.0f;

    return true;
}

bool AbilitySystem::ReleaseChannel() {
    if (channeling_ability_.empty()) {
        return false;
    }

    Ability* ability = GetAbility(channeling_ability_);
    if (!ability) return false;

    ChanneledAbility* channeled = dynamic_cast<ChanneledAbility*>(ability);
    if (!channeled) return false;

    float power = channeled->GetChannelPower();
    channeled->ReleaseChannel(power);

    cooldown_manager_.StartCooldown(channeling_ability_, ability->GetConfig().cooldown_duration);

    channeling_ability_.clear();
    return true;
}

float AbilitySystem::GetChannelPower() const {
    if (channeling_ability_.empty()) {
        return 0.0f;
    }

    const Ability* ability = GetAbility(channeling_ability_);
    if (!ability) return 0.0f;

    const ChanneledAbility* channeled = dynamic_cast<const ChanneledAbility*>(ability);
    if (!channeled) return 0.0f;

    return channeled->GetChannelPower();
}

float AbilitySystem::GetAbilityCooldown(const std::string& ability_name) const {
    return cooldown_manager_.GetRemainingCooldown(ability_name);
}

void AbilitySystem::AddTemporaryModifier(const std::string& stat_name, const Modifier& modifier) {
    modifier_db_.AddModifier(stat_name, modifier);
}

void AbilitySystem::RemoveModifiersFromSource(const std::string& source) {
    modifier_db_.RemoveModifiersFromSource(source);
}

void AbilitySystem::Update(float dt) {
    // Update cooldowns
    cooldown_manager_.Update(dt);

    // Update modifiers
    modifier_db_.Update(dt);

    // Update abilities
    for (auto& [name, ability] : abilities_) {
        ability->Update(dt);
    }

    // Update channeled ability
    if (!channeling_ability_.empty()) {
        Ability* ability = GetAbility(channeling_ability_);
        if (ability && ability->GetType() == AbilityType::Channeled) {
            ChanneledAbility* channeled = dynamic_cast<ChanneledAbility*>(ability);
            if (channeled) {
                channeled->UpdateChannel(dt);
            }
        }
    }
}

} // namespace engine::ability
