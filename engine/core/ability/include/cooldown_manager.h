#pragma once

#include <string>
#include <unordered_map>

namespace engine::ability {

/**
 * @struct CooldownInfo
 * @brief Information about an ability's cooldown
 */
struct CooldownInfo {
    float remaining_duration = 0.0f;
    float total_duration = 0.0f;

    /**
     * Get progress (0.0 = just started, 1.0 = ready)
     */
    float GetProgress() const {
        if (total_duration <= 0.0f) return 1.0f;
        return 1.0f - (remaining_duration / total_duration);
    }

    /**
     * Check if ready to use
     */
    bool IsReady() const { return remaining_duration <= 0.0f; }
};

/**
 * @class CooldownManager
 * @brief Manages ability cooldowns
 *
 * Tracks which abilities are on cooldown and for how long.
 * This prevents abilities from being cast too frequently.
 */
class CooldownManager {
public:
    CooldownManager() = default;
    ~CooldownManager() = default;

    /**
     * Check if ability is ready to cast
     * @param ability_name Name of the ability
     * @return true if not on cooldown
     */
    bool IsReady(const std::string& ability_name) const;

    /**
     * Start a cooldown for an ability
     * @param ability_name Name of the ability
     * @param duration Cooldown duration in seconds
     */
    void StartCooldown(const std::string& ability_name, float duration);

    /**
     * Get remaining cooldown
     * @param ability_name Name of the ability
     * @return Remaining cooldown in seconds (0 if ready)
     */
    float GetRemainingCooldown(const std::string& ability_name) const;

    /**
     * Get cooldown info
     */
    CooldownInfo GetCooldownInfo(const std::string& ability_name) const;

    /**
     * Reset cooldown to zero
     */
    void ResetCooldown(const std::string& ability_name);

    /**
     * Update all cooldowns
     * @param dt Delta time in seconds
     */
    void Update(float dt);

    /**
     * Get all active cooldowns (for UI display)
     */
    const std::unordered_map<std::string, CooldownInfo>& GetAllCooldowns() const { return cooldowns_; }

    /**
     * Clear all cooldowns
     */
    void Clear();

private:
    std::unordered_map<std::string, CooldownInfo> cooldowns_;
};

} // namespace engine::ability
