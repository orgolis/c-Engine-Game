#pragma once

#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include <entt/entt.hpp>

namespace engine::ability {

// Forward declarations
class Effect;
class Modifier;

/**
 * @enum AbilityType
 * @brief Classification of ability behavior
 */
enum class AbilityType {
    Active,     // Instant cast or channeled, requires activation
    Passive,    // Always active, provides continuous bonuses
    Channeled   // Held down, accumulates power/duration
};

/**
 * @struct AbilityConfig
 * @brief Configuration parameters for an ability
 */
struct AbilityConfig {
    std::string name;
    std::string description;
    AbilityType type = AbilityType::Active;
    
    float cooldown_duration = 5.0f;
    float mana_cost = 0.0f;
    float stamina_cost = 0.0f;
    
    float cast_time = 0.0f;        // Duration needed to cast
    float channel_duration = 0.0f; // Max channel time (0 = instant)
    
    // Area of effect
    float aoe_radius = 0.0f;
    float range = 100.0f;
    
    // Level/Skill Tree info
    uint8_t min_level = 1;
    uint32_t skill_node_id = 0;    // Skill tree node that grants this
};

/**
 * @class Ability
 * @brief Base class for all abilities in the game
 *
 * Abilities can be:
 * - Active: Fireball, Dash, Shield - requires activation
 * - Passive: Fire Resistance +20% - always active
 * - Channeled: Lightning Storm - held for power accumulation
 */
class Ability {
public:
    virtual ~Ability() = default;

    /**
     * Get the ability's unique identifier
     */
    virtual const std::string& GetName() const = 0;

    /**
     * Get the ability type
     */
    virtual AbilityType GetType() const = 0;

    /**
     * Get the full configuration
     */
    virtual const AbilityConfig& GetConfig() const = 0;

    /**
     * Check if ability can be activated right now
     * @param caster The entity attempting to cast
     * @return Error message if cannot cast, empty string if can cast
     */
    virtual std::string CanCast(entt::entity caster) const = 0;

    /**
     * Activate the ability
     * @param caster Entity performing the ability
     * @param target Target entity (if applicable, can be null)
     * @param position Target position (for AOE abilities)
     * @return true if activation successful
     */
    virtual bool Activate(entt::entity caster, entt::entity target, const glm::vec3& position) = 0;

    /**
     * Update channeled ability
     * @param dt Delta time
     * @return Power level (0.0 - 1.0) for charging visualization
     */
    virtual float UpdateChannel(float dt) { return 0.0f; }

    /**
     * Release a channeled ability
     * @param power How fully charged (0.0 - 1.0)
     */
    virtual void ReleaseChannel(float power) {}

    /**
     * Get remaining cooldown in seconds
     */
    virtual float GetRemainingCooldown() const = 0;

    /**
     * Apply effects of the ability
     * @param target Entity to apply effect to
     */
    virtual void ApplyEffects(entt::entity target) = 0;

    /**
     * Get associated effects
     */
    virtual const std::vector<std::shared_ptr<Effect>>& GetEffects() const = 0;

    /**
     * Update ability state (cooldowns, etc)
     * @param dt Delta time in seconds
     */
    virtual void Update(float dt) = 0;

    /**
     * Reset ability to initial state
     */
    virtual void Reset() = 0;
};

/**
 * @class ActiveAbility
 * @brief Base implementation for instant-cast or channeled abilities
 */
class ActiveAbility : public Ability {
public:
    explicit ActiveAbility(const AbilityConfig& config);
    ~ActiveAbility() override = default;

    const std::string& GetName() const override { return config_.name; }
    AbilityType GetType() const override { return AbilityType::Active; }
    const AbilityConfig& GetConfig() const override { return config_; }

    std::string CanCast(entt::entity caster) const override;
    bool Activate(entt::entity caster, entt::entity target, const glm::vec3& position) override;
    float GetRemainingCooldown() const override { return cooldown_remaining_; }
    void ApplyEffects(entt::entity target) override;
    const std::vector<std::shared_ptr<Effect>>& GetEffects() const override { return effects_; }
    void Update(float dt) override;
    void Reset() override;

protected:
    AbilityConfig config_;
    std::vector<std::shared_ptr<Effect>> effects_;
    float cooldown_remaining_ = 0.0f;

    /**
     * Override to implement ability-specific activation logic
     */
    virtual void OnActivate(entt::entity caster, entt::entity target, const glm::vec3& position) {}
};

/**
 * @class PassiveAbility
 * @brief Base implementation for always-active abilities
 */
class PassiveAbility : public Ability {
public:
    explicit PassiveAbility(const AbilityConfig& config);
    ~PassiveAbility() override = default;

    const std::string& GetName() const override { return config_.name; }
    AbilityType GetType() const override { return AbilityType::Passive; }
    const AbilityConfig& GetConfig() const override { return config_; }

    std::string CanCast(entt::entity caster) const override { return ""; }  // Always active
    bool Activate(entt::entity caster, entt::entity target, const glm::vec3& position) override { return false; }
    float GetRemainingCooldown() const override { return 0.0f; }
    void ApplyEffects(entt::entity target) override;
    const std::vector<std::shared_ptr<Effect>>& GetEffects() const override { return modifiers_; }
    void Update(float dt) override;
    void Reset() override;

protected:
    AbilityConfig config_;
    std::vector<std::shared_ptr<Effect>> modifiers_;
};

/**
 * @class ChanneledAbility
 * @brief Base implementation for held abilities that build power
 */
class ChanneledAbility : public Ability {
public:
    explicit ChanneledAbility(const AbilityConfig& config);
    ~ChanneledAbility() override = default;

    const std::string& GetName() const override { return config_.name; }
    AbilityType GetType() const override { return AbilityType::Channeled; }
    const AbilityConfig& GetConfig() const override { return config_; }

    std::string CanCast(entt::entity caster) const override;
    bool Activate(entt::entity caster, entt::entity target, const glm::vec3& position) override;
    float UpdateChannel(float dt) override;
    void ReleaseChannel(float power) override;
    float GetRemainingCooldown() const override { return cooldown_remaining_; }
    void ApplyEffects(entt::entity target) override;
    const std::vector<std::shared_ptr<Effect>>& GetEffects() const override { return effects_; }
    void Update(float dt) override;
    void Reset() override;

    /**
     * Get current channel power (0.0 - 1.0)
     */
    float GetChannelPower() const { return channel_power_; }

    /**
     * Check if currently channeling
     */
    bool IsChanneling() const { return is_channeling_; }

protected:
    AbilityConfig config_;
    std::vector<std::shared_ptr<Effect>> effects_;
    float cooldown_remaining_ = 0.0f;
    float channel_time_ = 0.0f;
    float channel_power_ = 0.0f;
    bool is_channeling_ = false;

    virtual void OnChannelUpdate(float power, float dt) {}
    virtual void OnChannelRelease(float power) {}
};

} // namespace engine::ability
