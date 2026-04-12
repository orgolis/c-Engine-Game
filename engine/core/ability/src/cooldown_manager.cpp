#include "cooldown_manager.h"
#include <glm/common.hpp>

namespace engine::ability {

bool CooldownManager::IsReady(const std::string& ability_name) const {
    auto it = cooldowns_.find(ability_name);
    if (it == cooldowns_.end()) {
        return true;  // No cooldown entry = ready
    }
    return it->second.remaining_duration <= 0.0f;
}

void CooldownManager::StartCooldown(const std::string& ability_name, float duration) {
    CooldownInfo info;
    info.total_duration = duration;
    info.remaining_duration = duration;
    cooldowns_[ability_name] = info;
}

float CooldownManager::GetRemainingCooldown(const std::string& ability_name) const {
    auto it = cooldowns_.find(ability_name);
    if (it == cooldowns_.end()) {
        return 0.0f;
    }
    return glm::max(0.0f, it->second.remaining_duration);
}

CooldownInfo CooldownManager::GetCooldownInfo(const std::string& ability_name) const {
    auto it = cooldowns_.find(ability_name);
    if (it == cooldowns_.end()) {
        return CooldownInfo();
    }
    return it->second;
}

void CooldownManager::ResetCooldown(const std::string& ability_name) {
    auto it = cooldowns_.find(ability_name);
    if (it != cooldowns_.end()) {
        it->second.remaining_duration = 0.0f;
    }
}

void CooldownManager::Update(float dt) {
    for (auto& [ability_name, info] : cooldowns_) {
        if (info.remaining_duration > 0.0f) {
            info.remaining_duration -= dt;
        }
    }
}

void CooldownManager::Clear() {
    cooldowns_.clear();
}

} // namespace engine::ability
