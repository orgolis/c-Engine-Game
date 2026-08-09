#pragma once

#include <cstdint>
#include "../physics/rigidbody_physics.h"
#include <glm/glm.hpp>
#include <glm/common.hpp>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>

namespace schizo::gameplay {

class CharacterStats;

/**
 * @brief Hit detection system - determines if an attack connects
 * 
 * Hitboxes are collision shapes (usually capsules for melee, spheres for ranged).
 * When a hitbox overlaps a hurtbox, it generates a HitResult.
 * 
 * ⚠ DETERMINISTIC: Hit detection must be frame-perfect and deterministic.
 * Use collision shapes, not random checks.
 */
class Hitbox {
public:
    Hitbox(const std::string& name, const std::shared_ptr<schizo::physics::CollisionShape>& shape)
        : name_(name), shape_(shape), is_active_(false), damage_(0.0f),
          pierces_(1), knockback_force_(0.0f), hit_stop_frames_(0) {
    }
    
    // ======================
    // Properties
    // ======================
    
    const std::string& GetName() const { return name_; }
    
    void SetActive(bool active) { is_active_ = active; }
    bool IsActive() const { return is_active_; }
    
    void SetDamage(float damage) { damage_ = damage; }
    float GetDamage() const { return damage_; }
    
    void SetKnockback(float force) { knockback_force_ = force; }
    float GetKnockback() const { return knockback_force_; }
    
    void SetPierces(int count) { pierces_ = count; }
    int GetPierces() const { return pierces_; }
    
    void SetHitStopFrames(int frames) { hit_stop_frames_ = frames; }
    int GetHitStopFrames() const { return hit_stop_frames_; }
    
    // ======================
    // Transform
    // ======================
    
    void SetPosition(const glm::vec3& pos) { position_ = pos; }
    glm::vec3 GetPosition() const { return position_; }
    
    void SetVelocity(const glm::vec3& vel) { velocity_ = vel; }
    glm::vec3 GetVelocity() const { return velocity_; }
    
    const std::shared_ptr<schizo::physics::CollisionShape>& GetShape() const {
        return shape_;
    }
    
    // ======================
    // Hit tracking (prevent double-hits)
    // ======================
    
    void RecordHit(uint32_t target_id) {
        hit_targets_.push_back(target_id);
    }
    
    bool HasHitTarget(uint32_t target_id) const {
        return std::find(hit_targets_.begin(), hit_targets_.end(), target_id)
            != hit_targets_.end();
    }
    
    void ClearHits() {
        hit_targets_.clear();
    }
    
private:
    std::string name_;
    std::shared_ptr<schizo::physics::CollisionShape> shape_;
    bool is_active_;
    float damage_;
    int pierces_;  // How many targets can be hit
    float knockback_force_;
    int hit_stop_frames_;  // Freeze on impact
    
    glm::vec3 position_;
    glm::vec3 velocity_;
    std::vector<uint32_t> hit_targets_;  // Already hit these IDs this frame
};

/**
 * @brief Result of a single hit
 * 
 * Returned when hitbox connects with hurtbox.
 */
struct HitResult {
    bool hit = false;
    uint32_t attacker_id = 0;
    uint32_t target_id = 0;
    glm::vec3 hit_position;
    float damage = 0.0f;
    float knockback_force = 0.0f;
    float hit_stop_time = 0.0f;  // Time to freeze on impact
    std::string hitbox_name;
    std::string effect_name;  // Combat effect (crit, parry, etc)
};

/**
 * @brief Poise system - Elden Ring-style poise/interruption
 * 
 * Every character has poise. Poise is reduced byhits.
 * When poise breaks (reaches 0), character is staggered.
 * 
 * Supports:
 * - Poise damage scaling (different attacks do different poise damage)
 * - Poise regeneration over time
 * - Interrupted animations
 */
class PoiseSystem {
public:
    PoiseSystem(float max_poise = 100.0f)
        : max_poise_(max_poise), current_poise_(max_poise),
          poise_regeneration_rate_(5.0f),  // 5 poise/sec
          is_staggered_(false),
          stagger_recovery_time_(1.0f) {
    }
    
    // ======================
    // Poise state
    // ======================
    
    float GetCurrentPoise() const { return current_poise_; }
    float GetMaxPoise() const { return max_poise_; }
    float GetPoisePercent() const { return current_poise_ / max_poise_; }
    
    bool IsStaggered() const { return is_staggered_; }
    float GetStaggerRecoveryTime() const { return stagger_recovery_time_; }
    
    // ======================
    // Poise damage
    // ======================
    
    /**
     * @brief Apply poise damage
     * 
     * Reduces poise. If reaches 0, character is staggered.
     * ⚠ DETERMINISTIC: Poise damage is fixed per attack type
     */
    void TakePoiseDamage(float damage) {
        current_poise_ -= damage;
        
        if (current_poise_ <= 0.0f) {
            current_poise_ = 0.0f;
            Stagger();
        }
    }
    
    /**
     * @brief Stagger character (poise broken)
     * 
     * Triggers interrupt, forces animation interruption.
     */
    void Stagger() {
        is_staggered_ = true;
        stagger_recovery_time_ = 1.0f;  // 1 second stagger
        current_poise_ = max_poise_ * 0.5f;  // Restore some poise
    }
    
    /**
     * @brief Update poise (regen when not staggered)
     */
    void Update(float delta_time) {
        if (is_staggered_) {
            stagger_recovery_time_ -= delta_time;
            if (stagger_recovery_time_ <= 0.0f) {
                is_staggered_ = false;
            }
        } else {
            // Regenerate poise over time
            current_poise_ = glm::min(
                current_poise_ + poise_regeneration_rate_ * delta_time,
                max_poise_
            );
        }
    }
    
    /**
     * @brief Set poise regeneration rate
     */
    void SetPoiseRegenRate(float rate) {
        poise_regeneration_rate_ = rate;
    }
    
private:
    float max_poise_;
    float current_poise_;
    float poise_regeneration_rate_;
    bool is_staggered_;
    float stagger_recovery_time_;
};

/**
 * @brief Knockback calculator
 * 
 * Determines character velocity after being hit.
 * Supports:
 * - Direction (away from attacker)
 * - Force scaling (based on attack and defender poise)
 * - Weight/mass resistance
 */
class KnockbackCalculator {
public:
    /**
     * @brief Calculate knockback velocity
     * 
     * @param hit_position Where the hit occurred
     * @param attacker_pos Where the attacker is
     * @param knockback_force Base knockback force
     * @param defender_weight Character weight (resists knockback)
     * @param poise_factor Poise break affects knockback (0->1 as poise breaks)
     * 
     * @return Velocity to apply to defender
     * 
     * ⚠ DETERMINISTIC: All parameters are deterministic
     */
    static glm::vec3 Calculate(
        const glm::vec3& hit_position,
        const glm::vec3& attacker_pos,
        float knockback_force,
        float defender_weight = 1.0f,
        float poise_factor = 0.0f  // 0 = full poise, 1 = staggered
    ) {
        // Direction away from attacker
        glm::vec3 direction = glm::normalize(hit_position - attacker_pos);
        
        // Force reduced by weight
        float effective_force = knockback_force / (1.0f + defender_weight * 0.5f);
        
        // Staggered targets take more knockback
        effective_force *= (1.0f + poise_factor);
        
        return direction * effective_force;
    }
};

/**
 * @brief Combat system manager
 * 
 * Tracks active hitboxes, detects collisions, generates hit results.
 * 
 * ⚠ Architecture note:
 * - Hitboxes are created per-animation frame (e.g., frame 5-10 of slash animation)
 * - Physics shapes used for collision testing
 * - Deterministic collision detection (no raycasts with RNG)
 */
class CombatSystem {
public:
    CombatSystem() = default;
    
    /**
     * @brief Register an active hitbox
     */
    void RegisterHitbox(const std::shared_ptr<Hitbox>& hitbox) {
        active_hitboxes_.push_back(hitbox);
    }
    
    /**
     * @brief Unregister hitbox (cleanup)
     */
    void UnregisterHitbox(const std::shared_ptr<Hitbox>& hitbox) {
        auto it = std::find(active_hitboxes_.begin(), active_hitboxes_.end(), hitbox);
        if (it != active_hitboxes_.end()) {
            active_hitboxes_.erase(it);
        }
    }
    
    /**
     * @brief Get all active hitboxes
     */
    const std::vector<std::shared_ptr<Hitbox>>& GetActiveHitboxes() const {
        return active_hitboxes_;
    }
    
    /**
     * @brief Test collision between hitbox and hurtbox (AABB)
     * 
     * @param hitbox Attacking hitbox
     * @param hurtbox_pos Center of hurtbox
     * @param hurtbox_size Half-extents of hurtbox AABB
     * 
     * @return HitResult with collision info, or hit=false if no collision
     */
    static HitResult TestCollision(
        const Hitbox& hitbox,
        const glm::vec3& hurtbox_pos,
        const glm::vec3& hurtbox_size
    ) {
        HitResult result;
        result.hit = false;
        
        if (!hitbox.IsActive()) {
            return result;
        }
        
        // Simple AABB collision test
        glm::vec3 hitbox_pos = hitbox.GetPosition();
        auto sphere_shape = std::dynamic_pointer_cast<schizo::physics::SphereShape>(
            hitbox.GetShape());
        
        if (sphere_shape) {
            float radius = sphere_shape->GetBoundingRadius();
            
            // AABB-sphere collision
            glm::vec3 closest(
                std::max(hurtbox_pos.x - hurtbox_size.x, std::min(hitbox_pos.x, hurtbox_pos.x + hurtbox_size.x)),
                std::max(hurtbox_pos.y - hurtbox_size.y, std::min(hitbox_pos.y, hurtbox_pos.y + hurtbox_size.y)),
                std::max(hurtbox_pos.z - hurtbox_size.z, std::min(hitbox_pos.z, hurtbox_pos.z + hurtbox_size.z))
            );
            float distance = glm::distance(hitbox_pos, closest);
            
            if (distance < radius) {
                result.hit = true;
                result.hit_position = closest;
                result.damage = hitbox.GetDamage();
                result.knockback_force = hitbox.GetKnockback();
                result.hitbox_name = hitbox.GetName();
            }
        }
        
        return result;
    }
    
    /**
     * @brief Update all hitboxes (decay lifetimes, etc)
     */
    void Update(float delta_time) {
        // Hitbox lifetime managed by animation/ability system
        // This just updates internal state
    }
    
private:
    std::vector<std::shared_ptr<Hitbox>> active_hitboxes_;
};

} // namespace schizo::gameplay
