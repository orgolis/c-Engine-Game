#pragma once

#include <cstdint>
#include "character_stats.h"
#include "ability_system.h"
#include "combat_system.h"
#include "../input/input_action.h"
#include <glm/glm.hpp>
#include <glm/common.hpp>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

namespace schizo::gameplay {

/**
 * @brief Base NPC/Creature system
 * 
 * Provides foundation for all creatures (enemies, NPCs, bosses).
 * Includes:
 * - Behavior tree for AI
 * - Ability execution
 * - Combat state management
 */

/**
 * @brief Creature perception - what the creature knows about the world
 */
struct CreaturePerception {
    std::vector<uint32_t> visible_enemies;   // Entity IDs currently visible
    std::vector<uint32_t> audible_events;    // Heard something
    uint32_t current_target = 0;             // Current attack target
    glm::vec3 last_known_enemy_pos;
    float detection_distance = 50.0f;
    float audio_detection_range = 30.0f;
};

/**
 * @brief Creature state machine states
 */
enum class CreatureState : uint8_t {
    Idle,           // Waiting, patrolling
    Alert,          // Saw something suspicious
    Combat,         // Active fight with target
    Evading,        // Running from player
    Stunned,        // Can't act (poise broken)
    Dead,           // Out of combat
};

/**
 * @brief Base creature behavior tree node
 * 
 * Hierarchical decision making for AI.
 * Simple behavior trees: Selector (OR) and Sequence (AND) nodes.
 */
class BehaviorNode {
public:
    enum class NodeType : uint8_t {
        Selector,      // Return success if any child succeeds
        Sequence,      // Return success if all children succeed
        Action,        // Leaf node - execute action
        Condition,     // Check condition
    };
    
    enum class Result : uint8_t {
        Success,
        Failure,
        Running,
    };
    
    virtual ~BehaviorNode() = default;
    virtual Result Tick(class Creature* creature, float delta_time) = 0;
    virtual NodeType GetType() const = 0;
};

/**
 * @brief Selector node - returns Success if any child succeeds (OR gate)
 */
class SelectorNode : public BehaviorNode {
public:
    void AddChild(std::shared_ptr<BehaviorNode> child) {
        children_.push_back(child);
    }
    
    Result Tick(Creature* creature, float delta_time) override;
    NodeType GetType() const override { return NodeType::Selector; }
    
private:
    std::vector<std::shared_ptr<BehaviorNode>> children_;
    size_t current_child_ = 0;
};

/**
 * @brief Sequence node - returns Success if all children succeed (AND gate)
 */
class SequenceNode : public BehaviorNode {
public:
    void AddChild(std::shared_ptr<BehaviorNode> child) {
        children_.push_back(child);
    }
    
    Result Tick(Creature* creature, float delta_time) override;
    NodeType GetType() const override { return NodeType::Sequence; }
    
private:
    std::vector<std::shared_ptr<BehaviorNode>> children_;
};

/**
 * @brief Action node - performs single action
 */
class ActionNode : public BehaviorNode {
public:
    using ActionFunc = std::function<BehaviorNode::Result(Creature*, float)>;
    
    explicit ActionNode(const std::string& name, ActionFunc action)
        : name_(name), action_(action) {
    }
    
    Result Tick(Creature* creature, float delta_time) override {
        return action_(creature, delta_time);
    }
    
    NodeType GetType() const override { return NodeType::Action; }
    const std::string& GetName() const { return name_; }
    
private:
    std::string name_;
    ActionFunc action_;
};

/**
 * @brief Condition node - returns Success if condition is true
 */
class ConditionNode : public BehaviorNode {
public:
    using ConditionFunc = std::function<bool(Creature*)>;
    
    explicit ConditionNode(const std::string& name, ConditionFunc condition)
        : name_(name), condition_(condition) {
    }
    
    Result Tick(Creature* creature, float delta_time) override {
        return condition_(creature) ? Result::Success : Result::Failure;
    }
    
    NodeType GetType() const override { return NodeType::Condition; }
    const std::string& GetName() const { return name_; }
    
private:
    std::string name_;
    ConditionFunc condition_;
};

/**
 * @brief Base Creature class
 * 
 * All combat entities (enemies, bosses, NPCs) derive from this.
 * Provides:
 * - Character stats
 * - Ability system
 * - Combat state
 * - Behavior tree AI
 * - Perception system
 */
class Creature {
public:
    Creature(const std::string& name, uint32_t entity_id = 0)
        : name_(name), entity_id_(entity_id), current_state_(CreatureState::Idle),
          position_(0.0f), facing_direction_(0.0f, 0.0f, 1.0f),
          stats_(std::make_shared<CharacterStats>()),
          poise_system_(100.0f) {
    }
    
    virtual ~Creature() = default;
    
    // ======================
    // Identification
    // ======================
    
    const std::string& GetName() const { return name_; }
    uint32_t GetEntityID() const { return entity_id_; }
    
    // ======================
    // Transform
    // ======================
    
    void SetPosition(const glm::vec3& pos) { position_ = pos; }
    glm::vec3 GetPosition() const { return position_; }
    
    void SetFacingDirection(const glm::vec3& dir) {
        facing_direction_ = glm::normalize(dir);
    }
    glm::vec3 GetFacingDirection() const { return facing_direction_; }
    
    // ======================
    // Stats & Resources
    // ======================
    
    std::shared_ptr<CharacterStats> GetStats() { return stats_; }
    const std::shared_ptr<CharacterStats>& GetStats() const { return stats_; }
    
    // ======================
    // Combat State
    // ======================
    
    void SetState(CreatureState state) { current_state_ = state; }
    CreatureState GetState() const { return current_state_; }
    
    PoiseSystem& GetPoiseSystem() { return poise_system_; }
    const PoiseSystem& GetPoiseSystem() const { return poise_system_; }
    
    bool IsAlive() const { return stats_->IsAlive(); }
    bool IsStaggered() const { return poise_system_.IsStaggered(); }
    
    // ======================
    // Ability System
    // ======================
    
    void SetAbilityLoadout(const AbilityLoadout& loadout) {
        ability_loadout_ = loadout;
    }
    
    const AbilityLoadout& GetAbilityLoadout() const {
        return ability_loadout_;
    }
    
    // ======================
    // AI & Behavior
    // ======================
    
    /**
     * @brief Set behavior tree root
     */
    void SetBehaviorTree(std::shared_ptr<BehaviorNode> root) {
        behavior_tree_ = root;
    }
    
    /**
     * @brief Update AI decision making
     */
    virtual void Update(float delta_time) {
        // Update stats (regen, modifiers)
        stats_->Update(delta_time);
        
        // Update poise system
        poise_system_.Update(delta_time);
        
        // Tick behavior tree
        if (behavior_tree_) {
            behavior_tree_->Tick(this, delta_time);
        }
        
        // Update abilities
        for (auto& ability : ability_loadout_.GetAllAbilities()) {
            if (ability) {
                ability->Update(delta_time);
            }
        }
    }
    
    // ======================
    // Perception
    // ======================
    
    CreaturePerception& GetPerception() { return perception_; }
    const CreaturePerception& GetPerception() const { return perception_; }
    
    /**
     * @brief Update perception (what can creature see/hear)
     * 
     * Called before behavior tree tick to update perceived enemies.
     */
    virtual void UpdatePerception(const std::vector<std::shared_ptr<Creature>>& world_creatures) {
        perception_.visible_enemies.clear();
        
        for (const auto& other : world_creatures) {
            if (!other || other.get() == this) continue;
            
            float distance = glm::distance(position_, other->GetPosition());
            if (distance < perception_.detection_distance) {
                perception_.visible_enemies.push_back(other->GetEntityID());
                
                // Can attack if in range
                if (!perception_.current_target) {
                    perception_.current_target = other->GetEntityID();
                    perception_.last_known_enemy_pos = other->GetPosition();
                }
            }
        }
    }
    
    // ======================
    // Combat Actions
    // ======================
    
    /**
     * @brief Execute ability
     * 
     * Called by behavior tree or game code.
     * ⚠ DETERMINISTIC: No RNG, all inputs deterministic
     */
    virtual void ExecuteAbility(const std::shared_ptr<ActiveAbility>& ability) {
        if (!ability || !ability->IsReady()) {
            return;
        }
        
        // Check cost
        if (!ability->ability->CanActivate(stats_)) {
            return;
        }
        
        // Pay cost
        stats_->SetBaseStat(StatType::Mana,
            stats_->GetFinalStat(StatType::Mana) - ability->ability->GetManaCost());
        stats_->SetBaseStat(StatType::Stamina,
            stats_->GetFinalStat(StatType::Stamina) - ability->ability->GetStaminaCost());
        
        // Activate ability
        float cooldown_mult = stats_->GetFinalStat(StatType::AbilityCooldown);
        ability->Activate(cooldown_mult);
    }
    
    /**
     * @brief Take hit and process damage/poise
     */
    virtual void TakeHit(const HitResult& hit, const std::shared_ptr<Creature>& attacker) {
        if (!IsAlive()) return;
        
        // Take damage
        float damage = hit.damage;
        if (attacker) {
            damage *= attacker->GetStats()->GetFinalStat(StatType::Damage);
        }
        stats_->TakeDamage(damage);
        
        // Take poise damage (const value per attack)
        poise_system_.TakePoiseDamage(5.0f);  // Default 5 poise per hit
        
        // Apply knockback
        glm::vec3 knockback_vel = KnockbackCalculator::Calculate(
            hit.hit_position,
            attacker ? attacker->GetPosition() : position_,
            hit.knockback_force,
            1.0f,  // Weight
            poise_system_.IsStaggered() ? 1.0f : 0.0f
        );
        
        // Apply to velocity (simplified - would integrate with physics system)
        velocity_ = knockback_vel;
    }
    
    glm::vec3 GetVelocity() const { return velocity_; }
    void SetVelocity(const glm::vec3& vel) { velocity_ = vel; }
    
protected:
    std::string name_;
    uint32_t entity_id_;
    CreatureState current_state_;
    
    glm::vec3 position_;
    glm::vec3 velocity_;
    glm::vec3 facing_direction_;
    
    std::shared_ptr<CharacterStats> stats_;
    PoiseSystem poise_system_;
    CreaturePerception perception_;
    
    AbilityLoadout ability_loadout_;
    std::shared_ptr<BehaviorNode> behavior_tree_;
};

} // namespace schizo::gameplay
