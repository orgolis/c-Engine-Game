#pragma once

// ============================================================================
// G0 · Ability system — data-defined activatable abilities.
// ============================================================================
//
// An ability = a cost (an attribute) + cooldown + tag requirements/blocks + the
// GameplayEffects it applies to the caster and/or a target. So skills, spells,
// gun fire modes, dashes, and item uses are all the same authored primitive over
// Attributes/Tags/Effects — for any game.

#include "world.h"
#include "gameplay_attributes.h"
#include "gameplay_tags.h"
#include "gameplay_effects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace schizo::ecs {

struct Ability {
    std::string name;
    std::string cost_attribute;              // "" = free
    float       cost = 0.0f;
    float       cooldown = 0.0f;             // seconds
    std::vector<std::string>    require_tags;   // caster must have ALL (hierarchical)
    std::vector<std::string>    block_tags;     // caster must have NONE
    std::vector<GameplayEffect>  effects_self;   // applied to the caster on activate
    std::vector<GameplayEffect>  effects_target; // applied to the target on activate
};

struct AbilitySlot { Ability ability; float cooldown_remaining = 0.0f; };
struct AbilitySet  { std::vector<AbilitySlot> abilities; };

// Is this ability activatable by the caster right now? (cooldown, tags, cost)
inline bool can_activate(World& w, Entity caster, const AbilitySlot& slot) {
    if (slot.cooldown_remaining > 0.0f) return false;
    const GameplayTags* tags = w.try_get<GameplayTags>(caster);
    for (const auto& t : slot.ability.require_tags) if (!tags || !tags->has(t)) return false;
    if (tags) for (const auto& t : slot.ability.block_tags) if (tags->has(t)) return false;
    if (!slot.ability.cost_attribute.empty() && slot.ability.cost > 0.0f) {
        const AttributeSet* attrs = w.try_get<AttributeSet>(caster);
        if (!attrs || attrs->get(slot.ability.cost_attribute) < slot.ability.cost) return false;
    }
    return true;
}

// Activate abilities[index] (optional target). Pays cost, applies effects, starts
// the cooldown. Returns false if it wasn't activatable.
inline bool try_activate_ability(World& w, Entity caster, size_t index, Entity target = null_entity) {
    if (!w.has<AbilitySet>(caster)) return false;
    AbilitySet& set = w.get<AbilitySet>(caster);
    if (index >= set.abilities.size()) return false;
    AbilitySlot& slot = set.abilities[index];
    if (!can_activate(w, caster, slot)) return false;

    if (!slot.ability.cost_attribute.empty() && slot.ability.cost > 0.0f)
        if (AttributeSet* attrs = w.try_get<AttributeSet>(caster))
            attrs->set(slot.ability.cost_attribute, attrs->get(slot.ability.cost_attribute) - slot.ability.cost);

    for (const auto& fx : slot.ability.effects_self) apply_effect(w, caster, fx);
    if (target != null_entity)
        for (const auto& fx : slot.ability.effects_target) apply_effect(w, target, fx);

    slot.cooldown_remaining = slot.ability.cooldown;
    return true;
}

// Advance ability cooldowns.
inline void tick_abilities(World& w, float dt) {
    w.each<AbilitySet>([&](Entity, AbilitySet& set) {
        for (auto& s : set.abilities)
            if (s.cooldown_remaining > 0.0f) s.cooldown_remaining = std::max(0.0f, s.cooldown_remaining - dt);
    });
}

}  // namespace schizo::ecs
