// ============================================================================
// combat.cpp — frame-data attack driver + hit resolution. See combat.h.
// ============================================================================

#include "combat/combat.h"
#include <algorithm>

namespace schizo::combat {

static constexpr int kStaggerFrames = 18;   // hitstun on a poise break

bool CombatActor::start_attack(const AttackFrameData& atk) {
    if (state_ != CombatState::Idle) return false;   // can't cancel a move
    attack_ = atk;
    state_  = CombatState::Startup;
    frame_  = 0;
    hit_targets_.clear();
    return true;
}

void CombatActor::advance_frame() {
    if (iframe_left_ > 0) --iframe_left_;
    if (parry_left_  > 0) --parry_left_;

    if (state_ == CombatState::Hitstun) {
        if (--hitstun_left_ <= 0) { state_ = CombatState::Idle; frame_ = 0; }
        return;
    }
    if (state_ == CombatState::Idle) return;

    ++frame_;   // 1-based frame within the attack
    const int s = attack_.startup, a = attack_.active, tot = attack_.total();
    if (frame_ <= s)          state_ = CombatState::Startup;
    else if (frame_ <= s + a) state_ = CombatState::Active;
    else if (frame_ <= tot)   state_ = CombatState::Recovery;
    else { state_ = CombatState::Idle; frame_ = 0; }
}

bool CombatActor::hitbox_active() const {
    if (state_ != CombatState::Active) return false;
    if (attack_.multi_hit) {
        const int active_frame = frame_ - attack_.startup;   // 1..active
        const int interval = std::max(1, attack_.multi_hit_interval);
        return ((active_frame - 1) % interval) == 0;
    }
    return true;   // single-hit: live across the window; dedup lands it once
}

HitSphere CombatActor::world_hitbox() const {
    const glm::quat rot = glm::angleAxis(yaw_, glm::vec3(0, 1, 0));
    return { pos_ + rot * attack_.hitbox_offset, attack_.hitbox_radius };
}

void CombatActor::apply_hit(float dmg, float poise_dmg) {
    health_ -= dmg;
    poise_  -= poise_dmg;
    if (poise_ <= 0.0f) {          // poise break → stagger, attack interrupted
        poise_ = max_poise_;
        state_ = CombatState::Hitstun;
        hitstun_left_ = kStaggerFrames;
        frame_ = 0;
    }
}

void CombatActor::stagger(int frames) {
    state_ = CombatState::Hitstun;
    hitstun_left_ = frames;
    frame_ = 0;
}

std::vector<CombatHit> resolve_combat(const std::vector<CombatActor*>& actors) {
    std::vector<CombatHit> hits;
    for (CombatActor* atk : actors) {
        if (!atk || !atk->hitbox_active()) continue;
        const HitSphere hb = atk->world_hitbox();
        const AttackFrameData& def = atk->current_attack();

        for (CombatActor* tgt : actors) {
            if (!tgt || tgt == atk || !tgt->alive()) continue;
            if (!def.multi_hit && atk->already_hit(tgt->id())) continue;

            const HitSphere hurt = tgt->hurtbox();
            const float reach = hb.radius + hurt.radius;
            if (glm::distance(hb.center, hurt.center) > reach) continue;  // miss

            // Dodge i-frames: the hit whiffs. Still dedup so it can't re-land
            // when the i-frames lapse inside the same active window.
            if (tgt->invulnerable()) { atk->record_hit(tgt->id()); continue; }

            CombatHit h;
            h.attacker = atk->id();
            h.target   = tgt->id();
            h.position = hurt.center;

            if (tgt->parrying()) {
                h.parried = true;                 // deflected: no damage…
                atk->stagger(24);                 // …and the attacker is punished
                atk->record_hit(tgt->id());
            } else {
                h.damage = def.damage;
                h.poise_damage = def.poise_damage;
                tgt->apply_hit(def.damage, def.poise_damage);
                if (!def.multi_hit) atk->record_hit(tgt->id());
            }
            hits.push_back(h);
        }
    }
    return hits;
}

} // namespace schizo::combat
