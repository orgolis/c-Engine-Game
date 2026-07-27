#pragma once

// ============================================================================
// Combat frame-data substrate — the action-combat driver that gates hitboxes by
// animation frames (startup / active / recovery), dedups hits per attack, and
// models the defensive windows (dodge i-frames, parry, poise/stagger). This is
// the layer that turns "an attack animation" into "a hit that lands on frame 8".
//
// Complements engine/core/gameplay/combat_system.h (schizo::gameplay::Hitbox /
// PoiseSystem — the collision-shape + time-based poise primitives). This module
// is the FRAME-DATA driver on top: fixed-tick, deterministic, glm-only.
//
// CAMERA / LARGE-WORLD contract: actors are world-space and camera-agnostic —
// combat plays out the same under any player camera (first/third-person, etc.).
// On a floating-origin rebase the caller shifts every actor via translate(shift)
// so hit/hurt volumes stay consistent with the rebased world + camera.
// ============================================================================

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <set>
#include <vector>

namespace schizo::combat {

/// A sphere volume — used for both hit (offensive) and hurt (defensive) shapes.
struct HitSphere {
    glm::vec3 center{0.0f};
    float     radius = 0.5f;
};

/// Frame-data description of one attack. Frames are fixed-timestep ticks.
struct AttackFrameData {
    int   startup  = 6;    // wind-up frames before the hitbox goes live
    int   active   = 4;    // frames the hitbox can deal damage
    int   recovery = 10;   // locked frames after the active window
    glm::vec3 hitbox_offset{0.0f, 1.0f, 1.3f};  // local frame (forward = +Z)
    float hitbox_radius = 0.7f;
    float damage = 25.0f;
    float poise_damage = 30.0f;
    bool  multi_hit = false;        // hit repeatedly across the active window
    int   multi_hit_interval = 2;   // …once every N active frames
    int   total() const { return startup + active + recovery; }
};

enum class CombatState { Idle, Startup, Active, Recovery, Hitstun };

// ----------------------------------------------------------------------------
// One combatant: transform + health + frame-based poise + the running attack.
// ----------------------------------------------------------------------------
class CombatActor {
public:
    CombatActor() = default;
    explicit CombatActor(int id, float health = 100.0f, float max_poise = 100.0f)
        : id_(id), health_(health), poise_(max_poise), max_poise_(max_poise) {}

    int   id() const { return id_; }
    void  set_position(const glm::vec3& p) { pos_ = p; }
    void  set_facing_yaw(float yaw) { yaw_ = yaw; }   // radians, about +Y
    glm::vec3 position() const { return pos_; }
    float health() const { return health_; }
    float poise() const { return poise_; }
    bool  alive() const { return health_ > 0.0f; }

    /// Floating-origin rebase: shift this actor along with the world + camera.
    void translate(const glm::vec3& d) { pos_ += d; }

    // ---- attack lifecycle -------------------------------------------------
    /// Begin an attack. Rejected if not Idle (no cancelling mid-move / hitstun).
    bool start_attack(const AttackFrameData& atk);
    /// Advance one fixed tick: decays timers, steps the frame state machine.
    void advance_frame();

    CombatState state() const { return state_; }
    int  frame() const { return frame_; }
    bool attacking() const {
        return state_ == CombatState::Startup || state_ == CombatState::Active ||
               state_ == CombatState::Recovery;
    }
    bool staggered() const { return state_ == CombatState::Hitstun; }
    const AttackFrameData& current_attack() const { return attack_; }

    /// True only while the hitbox deals damage (active window; for multi-hit,
    /// only on the interval ticks).
    bool hitbox_active() const;
    /// The offensive sphere in world space (offset rotated by yaw + translated).
    HitSphere world_hitbox() const;
    /// The defensive sphere (roughly the body) in world space.
    HitSphere hurtbox() const { return { pos_ + glm::vec3(0, 1.0f, 0), 0.6f }; }

    // ---- defensive windows ------------------------------------------------
    void start_dodge(int iframes) { iframe_left_ = iframes; }  // invulnerable
    void start_parry(int window)  { parry_left_ = window; }    // deflect window
    bool invulnerable() const { return iframe_left_ > 0; }
    bool parrying() const { return parry_left_ > 0; }

    // ---- per-attack hit dedup (a single-hit attack lands once per target) --
    bool already_hit(int target) const { return hit_targets_.count(target) != 0; }
    void record_hit(int target) { hit_targets_.insert(target); }

    // ---- damage / stagger -------------------------------------------------
    void apply_hit(float dmg, float poise_dmg);
    void stagger(int frames);   // forced hitstun (e.g. a parry punish)

private:
    int   id_ = 0;
    glm::vec3 pos_{0.0f};
    float yaw_ = 0.0f;
    float health_ = 100.0f, poise_ = 100.0f, max_poise_ = 100.0f;
    CombatState state_ = CombatState::Idle;
    AttackFrameData attack_{};
    int   frame_ = 0;            // 1-based frame within the current attack
    int   iframe_left_ = 0, parry_left_ = 0, hitstun_left_ = 0;
    std::set<int> hit_targets_;  // dedup set for the running attack
};

/// One resolved hit this frame.
struct CombatHit {
    int   attacker = 0, target = 0;
    float damage = 0.0f, poise_damage = 0.0f;
    bool  parried = false;
    glm::vec3 position{0.0f};
};

/// Resolve every active hitbox against every hurtbox for this frame. Applies
/// per-attack dedup, dodge i-frames (whiff), parry (deflect → attacker punished),
/// and poise damage (may stagger the target). Call once per tick AFTER advancing
/// each actor. Camera-agnostic; operates purely in world space.
std::vector<CombatHit> resolve_combat(const std::vector<CombatActor*>& actors);

} // namespace schizo::combat
