#pragma once

// ============================================================================
// G0 · Gameplay Effects — data-defined modifiers on attributes + tags.
// ============================================================================
//
// The reactive glue over Attributes + Tags. A GameplayEffect is DATA a designer,
// script, or ability builds — it modifies attributes BY NAME and/or grants tags,
// as instant / duration / periodic. So damage, healing, DoT/HoT, stuns, and
// temporary stat buffs are all the same authored primitive, for any game.
//
//   Instant   : apply the modifiers once (damage / heal / set).
//   Periodic  : (period > 0) apply the modifiers every `period` seconds (DoT/HoT).
//   Duration  : (period == 0) apply once on start + grant tags while active; on
//               expiry, revert Add modifiers (if revert_on_end) + remove tags.
//   Infinite  : like Duration but never expires (removed explicitly).
//
// ActiveEffects is RUNTIME state (not authored) tracked on the entity; tick it
// each frame with tick_effects().

#include "world.h"
#include "gameplay_attributes.h"
#include "gameplay_tags.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace schizo::ecs {

enum class EffectOp : uint32_t { Add = 0, Multiply = 1, Override = 2 };
enum class EffectDurationType : uint32_t { Instant = 0, Duration = 1, Infinite = 2 };

struct EffectModifier {
    std::string attribute;                 // by name — any attribute the game defined
    EffectOp    op = EffectOp::Add;
    float       magnitude = 0.0f;
};

struct GameplayEffect {
    std::string name;
    EffectDurationType duration_type = EffectDurationType::Instant;
    float duration = 0.0f;                  // seconds (Duration)
    float period   = 0.0f;                  // >0 -> apply modifiers every period (DoT/HoT)
    std::vector<EffectModifier> modifiers;
    std::vector<std::string>    granted_tags;   // granted while active
    bool revert_on_end = false;             // non-periodic Duration: undo Add modifiers on expiry
};

// Apply modifiers to an AttributeSet once (sign=-1 reverts Add/Multiply).
inline void apply_modifiers(AttributeSet& attrs, const std::vector<EffectModifier>& mods, float sign) {
    for (const auto& m : mods) {
        Attribute* a = attrs.find(m.attribute);
        if (!a) continue;
        switch (m.op) {
            case EffectOp::Add:      a->current += m.magnitude * sign; break;
            case EffectOp::Multiply: a->current  = (sign >= 0.0f)
                                                    ? a->current * m.magnitude
                                                    : a->current / (m.magnitude != 0.0f ? m.magnitude : 1.0f); break;
            case EffectOp::Override: if (sign >= 0.0f) a->current = m.magnitude; break;  // override can't revert
        }
        a->current = std::max(a->min, std::min(a->max, a->current));
    }
}

struct ActiveEffect {
    GameplayEffect def;
    float remaining    = 0.0f;   // seconds left (Duration)
    float period_accum = 0.0f;
};
struct ActiveEffects { std::vector<ActiveEffect> effects; };

inline void grant_tags(World& w, Entity e, const std::vector<std::string>& tags) {
    if (tags.empty()) return;
    if (!w.has<GameplayTags>(e)) w.add<GameplayTags>(e, GameplayTags{});
    auto& g = w.get<GameplayTags>(e);
    for (const auto& t : tags) g.add(t);
}
inline void ungrant_tags(World& w, Entity e, const std::vector<std::string>& tags) {
    if (!w.has<GameplayTags>(e)) return;
    auto& g = w.get<GameplayTags>(e);
    for (const auto& t : tags) g.remove_exact(t);
}

// Apply an effect to an entity (needs an AttributeSet for stat modifiers).
inline void apply_effect(World& w, Entity e, const GameplayEffect& fx) {
    if (fx.duration_type == EffectDurationType::Instant) {
        if (auto* a = w.try_get<AttributeSet>(e)) apply_modifiers(*a, fx.modifiers, +1.0f);
        return;
    }
    if (!w.has<ActiveEffects>(e)) w.add<ActiveEffects>(e, ActiveEffects{});
    ActiveEffect ae;
    ae.def = fx;
    ae.remaining = fx.duration;
    if (fx.period <= 0.0f) {   // non-periodic duration buff: apply once on start
        if (auto* a = w.try_get<AttributeSet>(e)) apply_modifiers(*a, fx.modifiers, +1.0f);
    }
    grant_tags(w, e, fx.granted_tags);
    w.get<ActiveEffects>(e).effects.push_back(std::move(ae));
}

// Advance every entity's active effects by dt: periodic ticks, then expiry
// (revert Add modifiers + remove granted tags).
inline void tick_effects(World& w, float dt) {
    w.each<ActiveEffects>([&](Entity e, ActiveEffects& ae) {
        AttributeSet* attrs = w.try_get<AttributeSet>(e);
        for (size_t i = 0; i < ae.effects.size();) {
            ActiveEffect& fx = ae.effects[i];
            if (fx.def.period > 0.0f && attrs) {
                fx.period_accum += dt;
                while (fx.period_accum >= fx.def.period) {
                    apply_modifiers(*attrs, fx.def.modifiers, +1.0f);
                    fx.period_accum -= fx.def.period;
                }
            }
            bool expired = false;
            if (fx.def.duration_type == EffectDurationType::Duration) {
                fx.remaining -= dt;
                if (fx.remaining <= 0.0f) expired = true;
            }
            if (expired) {
                if (fx.def.period <= 0.0f && fx.def.revert_on_end && attrs)
                    apply_modifiers(*attrs, fx.def.modifiers, -1.0f);
                ungrant_tags(w, e, fx.def.granted_tags);
                ae.effects.erase(ae.effects.begin() + i);
            } else {
                ++i;
            }
        }
    });
}

// Convenience builders (designers/scripts also build these directly as data).
inline GameplayEffect make_instant(std::string name, std::string attr, float delta, EffectOp op = EffectOp::Add) {
    GameplayEffect fx; fx.name = std::move(name); fx.duration_type = EffectDurationType::Instant;
    fx.modifiers.push_back({std::move(attr), op, delta}); return fx;
}
inline GameplayEffect make_dot(std::string name, std::string attr, float per_tick, float period, float duration) {
    GameplayEffect fx; fx.name = std::move(name); fx.duration_type = EffectDurationType::Duration;
    fx.duration = duration; fx.period = period;
    fx.modifiers.push_back({std::move(attr), EffectOp::Add, per_tick}); return fx;
}

}  // namespace schizo::ecs
