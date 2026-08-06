#pragma once

// ============================================================================
// G2 · ECS melee combat — frame-data hitboxes resolved through G0.
// ============================================================================
//
// Wires the combat frame-data substrate (engine/core/combat/combat.h) onto ECS
// entities and routes every landed hit through G0 so it composes with the whole
// gameplay stack:
//
//   * attacks are AttackFrameData (startup/active/recovery, hitbox, damage,
//     poise) — reused from combat.h;
//   * a hit's damage goes through apply_damage() -> the target's Health attribute
//     with G0 resist/immunity applied (a damage_type per attacker);
//   * on-hit GameplayEffects (bleed, poison, stun) are applied to the target;
//   * poise break, i-frames (dodge), and parry (attacker punished) are modelled;
//   * every hit/parry/whiff/stagger is published on the GameplayEventBus, and the
//     target's StateMachine is sent "hit" / "staggered" so its FSM reacts.
//
// Camera-agnostic: actors are world-space (position from Transform, facing_yaw
// set by the controller). Advance at a FIXED tick (the bridge steps it at 60 Hz).

#include "world.h"
#include "components.h"                // Transform
#include "gameplay_events.h"           // GameplayEventBus
#include "gameplay_damage.h"           // apply_damage (G0)
#include "gameplay_effects.h"          // on-hit GameplayEffects (G0)
#include "gameplay_state_machine.h"    // drive the target's FSM + detail::put_str
#include "authorable_components.h"
#include "component_serialize.h"
#include "combat/combat.h"             // AttackFrameData / HitSphere / CombatState (POD)

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace schizo::ecs {

using combat::AttackFrameData;
using combat::HitSphere;
using combat::CombatState;

struct CombatActor {
    // ---- authored config ----
    float facing_yaw  = 0.0f;          // radians about +Y (runtime, set by controller)
    float poise       = 100.0f;
    float max_poise   = 100.0f;
    float hurt_radius = 0.6f;
    float hurt_height = 1.0f;
    std::string damage_type = "physical";                 // routed through G0 resist/immunity
    std::vector<GameplayEffect> on_hit_effects;           // applied to the target on a landed hit
    // ---- runtime attack state ----
    CombatState     state = CombatState::Idle;
    AttackFrameData attack;
    int frame = 0;
    int iframe_left = 0, parry_left = 0, hitstun_left = 0;
    std::vector<uint32_t> hit_targets;                    // per-attack dedup
};

inline bool combat_attacking(const CombatActor& a) {
    return a.state == CombatState::Startup || a.state == CombatState::Active ||
           a.state == CombatState::Recovery;
}

// Begin an attack (rejected unless Idle — no cancelling mid-move / hitstun).
inline bool combat_start_attack(CombatActor& a, const AttackFrameData& atk) {
    if (a.state != CombatState::Idle) return false;
    a.attack = atk;
    a.frame = 1;
    a.state = (atk.startup == 0) ? CombatState::Active : CombatState::Startup;
    a.hit_targets.clear();
    return true;
}
inline void combat_start_dodge(CombatActor& a, int iframes) { a.iframe_left = iframes; }
inline void combat_start_parry(CombatActor& a, int window)  { a.parry_left = window; }
inline void combat_stagger(CombatActor& a, int frames) {
    a.state = CombatState::Hitstun; a.hitstun_left = frames; a.frame = 0;
}

// Advance one fixed tick: decay defensive windows + step the frame state machine.
inline void combat_advance(CombatActor& a) {
    if (a.iframe_left > 0) --a.iframe_left;
    if (a.parry_left  > 0) --a.parry_left;
    if (a.state == CombatState::Hitstun) {
        if (--a.hitstun_left <= 0) { a.state = CombatState::Idle; a.hitstun_left = 0; }
        return;
    }
    if (!combat_attacking(a)) return;
    ++a.frame;
    const AttackFrameData& atk = a.attack;
    if (a.frame > atk.total()) { a.state = CombatState::Idle; a.frame = 0; a.hit_targets.clear(); return; }
    if      (a.frame <= atk.startup)                a.state = CombatState::Startup;
    else if (a.frame <= atk.startup + atk.active)   a.state = CombatState::Active;
    else                                            a.state = CombatState::Recovery;
}

// Is the hitbox dealing damage this tick? (Active window; multi-hit only on the
// interval frames.)
inline bool combat_hitbox_live(const CombatActor& a) {
    if (a.state != CombatState::Active) return false;
    if (!a.attack.multi_hit) return true;
    const int iv = a.attack.multi_hit_interval > 0 ? a.attack.multi_hit_interval : 1;
    const int active_frame = a.frame - a.attack.startup;   // 1-based within the active window
    return ((active_frame - 1) % iv) == 0;
}

inline glm::vec3 yaw_rotate(const glm::vec3& v, float yaw) {   // rotate about +Y (forward = +Z)
    const float c = std::cos(yaw), s = std::sin(yaw);
    return glm::vec3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}
inline HitSphere combat_world_hitbox(const CombatActor& a, const glm::vec3& pos) {
    return { pos + yaw_rotate(a.attack.hitbox_offset, a.facing_yaw), a.attack.hitbox_radius };
}
inline HitSphere combat_hurtbox(const CombatActor& a, const glm::vec3& pos) {
    return { pos + glm::vec3(0.0f, a.hurt_height, 0.0f), a.hurt_radius };
}
inline bool spheres_overlap(const HitSphere& x, const HitSphere& y) {
    const glm::vec3 d = x.center - y.center;
    const float r = x.radius + y.radius;
    return glm::dot(d, d) <= r * r;
}

// One fixed combat tick: advance every actor, then resolve active hitboxes vs
// hurtboxes — applying dedup, dodge i-frames, parry, poise/stagger, G0 damage +
// on-hit effects, events, and FSM drive. Call at a fixed rate.
inline void tick_combat(World& w, GameplayEventBus& bus) {
    struct Row { Entity e; CombatActor* a; glm::vec3 pos; };
    std::vector<Row> rows;
    w.each<CombatActor, Transform>([&](Entity e, CombatActor& a, Transform& t) {
        rows.push_back({e, &a, t.position});
    });

    for (auto& r : rows) combat_advance(*r.a);

    auto contains = [](const std::vector<uint32_t>& s, uint32_t x) {
        for (uint32_t v : s) if (v == x) return true;
        return false;
    };

    for (auto& atk : rows) {
        if (!combat_hitbox_live(*atk.a)) continue;
        const HitSphere hb = combat_world_hitbox(*atk.a, atk.pos);
        for (auto& tgt : rows) {
            if (tgt.e == atk.e) continue;
            if (!spheres_overlap(hb, combat_hurtbox(*tgt.a, tgt.pos))) continue;

            const uint32_t tid = static_cast<uint32_t>(tgt.e);
            if (!atk.a->attack.multi_hit) {
                if (contains(atk.a->hit_targets, tid)) continue;   // single-hit: once per target
                atk.a->hit_targets.push_back(tid);
            }

            if (tgt.a->parry_left > 0) {                            // parried -> attacker punished
                combat_stagger(*atk.a, 30);
                GameplayEvent ev; ev.name = "combat.parry"; ev.instigator = tgt.e; ev.target = atk.e;
                bus.publish(ev);
                if (w.has<StateMachine>(atk.e))
                    send_state_event(w, atk.e, w.get<StateMachine>(atk.e), "staggered", &bus);
                continue;
            }
            if (tgt.a->iframe_left > 0) {                           // i-frames -> whiff
                GameplayEvent ev; ev.name = "combat.whiff"; ev.instigator = atk.e; ev.target = tgt.e;
                bus.publish(ev);
                continue;
            }

            const float dealt = apply_damage(w, tgt.e,
                DamageInfo{atk.a->attack.damage, atk.a->damage_type});   // G0 damage
            for (const auto& fx : atk.a->on_hit_effects) apply_effect(w, tgt.e, fx);  // G0 status

            tgt.a->poise -= atk.a->attack.poise_damage;
            bool staggered = false;
            if (tgt.a->poise <= 0.0f) { tgt.a->poise = tgt.a->max_poise; combat_stagger(*tgt.a, 24); staggered = true; }

            GameplayEvent hit; hit.name = "combat.hit"; hit.instigator = atk.e; hit.target = tgt.e;
            hit.value = dealt; hit.param = atk.a->damage_type;
            bus.publish(hit);
            if (w.has<StateMachine>(tgt.e))
                send_state_event(w, tgt.e, w.get<StateMachine>(tgt.e), staggered ? "staggered" : "hit", &bus);
        }
    }
}

// ---- custom serialization (authored config only; runtime + effects skipped) ----
inline void serialize_combat_actor(const void* obj, std::vector<uint8_t>& out) {
    const auto& a = *static_cast<const CombatActor*>(obj);
    detail::put_bytes(out, &a.max_poise, 4);
    detail::put_bytes(out, &a.hurt_radius, 4);
    detail::put_bytes(out, &a.hurt_height, 4);
    detail::put_str(out, a.damage_type);
}
inline void deserialize_combat_actor(void* obj, const uint8_t* data, size_t n) {
    auto& a = *static_cast<CombatActor*>(obj);
    const uint8_t* p = data; const uint8_t* e = data + n;
    if (!detail::get_bytes(p, e, &a.max_poise, 4))  return;
    if (!detail::get_bytes(p, e, &a.hurt_radius, 4)) return;
    if (!detail::get_bytes(p, e, &a.hurt_height, 4)) return;
    detail::get_str(p, e, a.damage_type);
    a.poise = a.max_poise;
    a.state = CombatState::Idle; a.frame = 0;
    a.iframe_left = a.parry_left = a.hitstun_left = 0;
    a.hit_targets.clear();
}
inline void register_combat_component() {
    register_authorable(make_authorable_custom<CombatActor>(
        "Combat Actor", &serialize_combat_actor, &deserialize_combat_actor));
}

}  // namespace schizo::ecs
