#include <cstdint>
#include "character_stats.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace engine::character {

CharacterStats::CharacterStats(entt::entity entity)
    : entity_(entity) {
    // Initialize resistances to 0 (no resistance)
    std::fill(resistances_.begin(), resistances_.end(), 0.0f);
}

void CharacterStats::Update(float dt) {
    // Regenerate health if passive regen is enabled
    if (health_regen_rate_ > 0.0f) {
        health_ = glm::min(health_ + health_regen_rate_ * dt, max_health_);
    }

    // Regenerate stamina
    if (stamina_regen_rate_ > 0.0f) {
        stamina_ = glm::min(stamina_ + stamina_regen_rate_ * dt, max_stamina_);
    }

    // Update temporary resistances
    for (auto& effects : temporary_resistances_) {
        auto it = effects.begin();
        while (it != effects.end()) {
            it->remaining_duration -= dt;
            if (it->remaining_duration <= 0.0f) {
                it = effects.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void CharacterStats::TakeDamage(float damage) {
    if (damage < 0.0f) return;
    
    health_ = glm::max(0.0f, health_ - damage);
    
    auto logger = spdlog::get("engine");
    if (logger) {
        logger->debug("Character took {} damage, health now: {}/{}", 
                     damage, health_, max_health_);
    }
    
    if (health_ <= 0.0f && logger) {
        logger->info("Character died!");
    }
}

void CharacterStats::Heal(float amount) {
    if (amount < 0.0f) return;
    
    health_ = glm::min(health_ + amount, max_health_);
    
    auto logger = spdlog::get("engine");
    if (logger) {
        logger->debug("Character healed {} HP, health now: {}/{}", 
                     amount, health_, max_health_);
    }
}

bool CharacterStats::ConsumeStamina(float amount) {
    if (stamina_ >= amount) {
        stamina_ -= amount;
        return true;
    }
    return false;
}

void CharacterStats::SetResistance(ElementType element, float value) {
    value = glm::clamp(value, 0.0f, 1.0f);
    resistances_[static_cast<size_t>(element)] = value;
}

void CharacterStats::AddTemporaryResistance(ElementType element, float bonus, float duration) {
    auto& effects = temporary_resistances_[static_cast<size_t>(element)];
    effects.push_back({bonus, duration});
}

bool CharacterStats::AddExperience(uint64_t amount) {
    experience_ += amount;
    uint64_t xp_needed = GetExperienceForNextLevel();
    
    if (experience_ >= xp_needed) {
        level_++;
        experience_ -= xp_needed;
        
        // Level up bonuses
        max_health_ += 10.0f;
        max_stamina_ += 5.0f;
        health_ = max_health_;  // Full heal on level
        stamina_ = max_stamina_;
        
        auto logger = spdlog::get("engine");
        if (logger) {
            logger->info("Character leveled up to {}", level_);
        }
        return true;
    }
    return false;
}

uint64_t CharacterStats::GetExperienceForNextLevel() const {
    return GetXPForLevel(level_);
}

float CharacterStats::CalculateFinalDamage(float damage, float armor_penetration,
                                          ElementType element) const {
    if (damage <= 0.0f) return 0.0f;
    
    // Calculate armor reduction
    float effective_armor = glm::max(0.0f, armor_ - armor_penetration);
    float armor_reduction = effective_armor / (effective_armor + 100.0f);  // Scaling formula
    float armor_mitigated_damage = damage * (1.0f - armor_reduction);
    
    // Calculate resistance reduction
    float resistance = resistances_[static_cast<size_t>(element)];
    
    // Add temporary resistances
    const auto& temp_resistances = temporary_resistances_[static_cast<size_t>(element)];
    for (const auto& effect : temp_resistances) {
        resistance = glm::min(1.0f, resistance + effect.bonus);
    }
    
    float final_damage = armor_mitigated_damage * (1.0f - resistance);
    return glm::max(1.0f, final_damage);  // Minimum 1 damage always
}

uint64_t CharacterStats::GetXPForLevel(uint8_t level) {
    // Exponential XP curve: each level requires more XP
    // Level 1->2: 100, Level 2->3: 200, etc.
    return static_cast<uint64_t>(100.0f * level * (level + 1) / 2.0f);
}

} // namespace engine::character
