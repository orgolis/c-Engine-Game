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
// Everything composes over Attributes/Tags/Effects.

#include "world.h"
#include "gameplay_attributes.h"
#include "gameplay_tags.h"
#include "gameplay_effects.h"

#include <algorithm>
#include <string>

namespace schizo::ecs {

struct DamageInfo {
    float       amount = 0.0f;
    std::string type;                        // "" = untyped (no resist/immunity)
    std::string health_attribute = "Health"; // which attribute the damage subtracts from
};

// Resolve + apply damage to a target. Returns the final damage dealt.
inline float apply_damage(World& w, Entity target, const DamageInfo& dmg) {
    if (!dmg.type.empty())
        if (const GameplayTags* tags = w.try_get<GameplayTags>(target))
            if (tags->has("immune." + dmg.type)) return 0.0f;

    float amount = dmg.amount;
    if (!dmg.type.empty())
        if (const AttributeSet* attrs = w.try_get<AttributeSet>(target)) {
            const float resist = attrs->get("resist." + dmg.type, 0.0f);   // 0..1 fraction
            amount *= std::max(0.0f, 1.0f - resist);
        }

    apply_effect(w, target, make_instant("Damage", dmg.health_attribute, -amount));
    return amount;
}

}  // namespace schizo::ecs
