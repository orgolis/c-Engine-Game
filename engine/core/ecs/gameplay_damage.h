#pragma once

// ============================================================================
// G0 · Damage pipeline — data-driven types, resistances, immunity.
// ============================================================================
//
// Damage is resolved by NAME, so a game defines its own damage types
// ("physical", "fire", "toxic", "kinetic", "void") with no engine changes:
//   - immunity: target tag "immune.<type>"  -> 0 damage
//   - resistance: target attribute "resist.<type>" in [0,1] -> reduces damage
//   - the result is applied as an instant GameplayEffect on the health attribute
//   - DamageTaken (a bonus stat, %): a weakened target takes more (statuses)
//   - Shield (an attribute): takes the damage first, until it is gone
// Everything composes over Attributes/Tags/Effects. Every hit is written to the
// target's DamageLog: who hit it last and with what (a kill's credit), and the
// frame's hits (what a status's `hurt` moment hears; gameplay_status.h).

#include "world.h"
#include "gameplay_attributes.h"
#include "gameplay_tags.h"
#include "gameplay_effects.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace schizo::ecs {

struct DamageInfo {
    float       amount = 0.0f;
    std::string type;                        // "" = untyped (no resist/immunity)
    std::string health_attribute = "Health"; // which attribute the damage subtracts from
    Entity instigator = null_entity;
    std::string source;                      // what it came from: an ability, a weapon, "status:<id>"
};

/// One hit an entity took (DamageLog).
struct DamageRecord {
    float       amount = 0.0f;   // after resistances, before a shield took its part
    std::string type, source;
    Entity      by = null_entity;
};
/// The hits an entity took: this frame's (cleared by tick_statuses), and the
/// last one's maker and source -- who killed it, with what.
struct DamageLog {
    std::vector<DamageRecord> hits;
    Entity      last_by = null_entity;
    std::string last_source, last_type;
    std::vector<std::string> died_with;   // the statuses it had when it died (tick_statuses)
};
struct DamageAuthority { bool authoritative=true; bool friendly_fire=true; };

// Runtime multipart hurtboxes share one health pool. Each pellet resolves once;
// areas resolve the owner once, rather than multiplying by overlapping regions.
struct DamageReceiver { Entity owner = null_entity; float multiplier = 1.0f; };
inline Entity damage_owner(World& w, Entity e) {
    if (const auto* r = w.try_get<DamageReceiver>(e))
        return w.valid(r->owner) ? r->owner : null_entity;
    return e;
}
inline void unique_damage_owners(World& w, std::vector<Entity>& hits) {
    std::vector<Entity> owners;
    for (Entity e : hits) {
        e = damage_owner(w, e);
        if (e != null_entity && std::find(owners.begin(), owners.end(), e) == owners.end()) owners.push_back(e);
    }
    hits = std::move(owners);
}

// Resolve + apply damage to a target. Returns the final damage dealt.
inline float apply_damage(World& w, Entity target, const DamageInfo& dmg) {
    const auto* policy=w.registry().ctx().find<DamageAuthority>();
    if(policy&&!policy->authoritative) return 0.0f;
    if (!w.valid(target) || !std::isfinite(dmg.amount) || dmg.amount <= 0.0f) return 0.0f;
    float multiplier = 1.0f;
    if (const auto* receiver = w.try_get<DamageReceiver>(target)) {
        multiplier = std::max(0.0f, receiver->multiplier);
        target = damage_owner(w, target);
        if (target == null_entity) return 0.0f;
    }
    if(policy&&!policy->friendly_fire&&w.valid(dmg.instigator)&&target!=dmg.instigator) {
        const auto* source_tags=w.try_get<GameplayTags>(dmg.instigator);
        const auto* target_tags=w.try_get<GameplayTags>(target);
        if(source_tags&&target_tags&&source_tags->has("team.player")&&target_tags->has("team.player"))return 0;
    }
    if (!dmg.type.empty())
        if (const GameplayTags* tags = w.try_get<GameplayTags>(target))
            if (tags->has("immune." + dmg.type)) return 0.0f;

    float amount = dmg.amount * multiplier;
    if (!dmg.type.empty())
        if (const AttributeSet* attrs = w.try_get<AttributeSet>(target)) {
            float resist = attrs->get("resist." + dmg.type, 0.0f);   // 0..1 fraction
            // Shredded armor retains one fifth of physical protection.
            if (dmg.type == "physical" || dmg.type == "kinetic")
                if (const auto* tags = w.try_get<GameplayTags>(target); tags && tags->has("status.shredded"))
                    resist *= 0.2f;
            // Above 1: immune; below 0: a weakness (more damage) -- as before 0.8.53.
            amount *= std::max(0.0f, 1.0f - resist);
        }

    auto* attrs = w.try_get<AttributeSet>(target);
    const auto* health = attrs ? attrs->find(dmg.health_attribute) : nullptr;
    if (!health || health->current <= health->min) return 0.0f;
    // Weakened (a status): DamageTaken % more (or less).
    if (const Attribute* taken = attrs->find("DamageTaken"); taken && taken->current != 0.0f)
        amount *= std::max(0.0f, 1.0f + taken->current / 100.0f);
    // Written down before a shield takes its part (a hit is as hard as it was).
    {
        if (!w.has<DamageLog>(target)) w.add<DamageLog>(target, DamageLog{});
        DamageLog& log = w.get<DamageLog>(target);
        if (log.hits.size() < 32) log.hits.push_back({amount, dmg.type, dmg.source, dmg.instigator});
        log.last_by = dmg.instigator;
        log.last_source = dmg.source;
        log.last_type = dmg.type;
    }
    if (Attribute* shield = attrs->find("Shield"); shield && shield->current > 0.0f && dmg.health_attribute == "Health") {
        const float taken_by_shield = std::min(shield->current, amount);
        shield->current -= taken_by_shield;
        amount -= taken_by_shield;
        if (amount <= 0.0f) return 0.0f;
    }
    attrs = w.try_get<AttributeSet>(target);   // (a component was added above)
    health = attrs->find(dmg.health_attribute);
    const float before = health->current;
    attrs->adjust(dmg.health_attribute, -amount);
    return before - attrs->get(dmg.health_attribute);
}

}  // namespace schizo::ecs
