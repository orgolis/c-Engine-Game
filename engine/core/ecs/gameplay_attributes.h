#pragma once

// ============================================================================
// G0 · Data-driven attributes  — the flexibility core of the gameplay framework.
// ============================================================================
//
// The engine bakes in NO fixed stats. A game defines its OWN attributes as DATA
// (Health, Stamina, Mana, Power, Sanity, Heat, Hunger, Boost, …) via an
// AttributeSet on an entity. Everything downstream — effects, damage, abilities,
// UI — operates on attributes BY NAME, so any genre's stats plug in without
// engine changes. This is what keeps user control + variety maximal.
//
// AttributeSet is a NON-POD ECS component (dynamic list), so it registers as an
// authorable component with CUSTOM serialization (below); the editor draws it
// with a custom inspector. Add attributes at author time or from a prefab.

#include "world.h"
#include "authorable_components.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace schizo::ecs {

struct Attribute {
    std::string name;              // e.g. "Health", "Stamina", "Sanity"
    float base    = 0.0f;          // authored base value
    float current = 0.0f;          // live value (base + effects, clamped to [min,max])
    float min     = 0.0f;
    float max     = 100.0f;
};

struct AttributeSet {
    std::vector<Attribute> attributes;

    Attribute*       find(const std::string& n)       { for (auto& a : attributes) if (a.name == n) return &a; return nullptr; }
    const Attribute* find(const std::string& n) const { for (auto& a : attributes) if (a.name == n) return &a; return nullptr; }

    float get(const std::string& n, float dflt = 0.0f) const { const Attribute* a = find(n); return a ? a->current : dflt; }
    void  set(const std::string& n, float v) {
        if (Attribute* a = find(n)) a->current = std::max(a->min, std::min(a->max, v));
    }
    // Add or adjust an attribute's current (clamped), returning the new value.
    float adjust(const std::string& n, float delta) {
        if (Attribute* a = find(n)) { set(n, a->current + delta); return a->current; }
        return 0.0f;
    }
    Attribute& define(const std::string& n, float base, float min = 0.0f, float max = 100.0f) {
        if (Attribute* a = find(n)) { a->base = base; a->current = base; a->min = min; a->max = max; return *a; }
        attributes.push_back(Attribute{n, base, base, min, max});
        return attributes.back();
    }
};

// ---- bonus stats: attributes the engine's systems read when an entity has them ----
//   MoveSpeed          % faster moving (the player; NPC agents)
//   Damage             % more damage dealt (guns, abilities)
//   CooldownReduction  % shorter ability cooldowns (at most 75)
//   ReloadSpeed        % faster reloading
//   HealthRegen        health a second (while alive)
//   ExtraJumps         jumps in the air
//   WeaponDamage       additional % firearm / melee damage
//   AbilityDamage      additional % spell damage
//   WeaponHandling     % less weapon spread and recoil (capped at 60)
//   ManaRegen          Mana a second while alive
// A skill's `grant` or an effect on one the entity does not have yet adds it
// (from 0, room up to +-1000): passives and buffs need no setup.
inline bool is_bonus_stat(const std::string& n) {
    return n == "MoveSpeed" || n == "Damage" || n == "CooldownReduction" || n == "ReloadSpeed" ||
           n == "HealthRegen" || n == "ExtraJumps" || n == "ManaRegen" ||
           n == "WeaponDamage" || n == "AbilityDamage" || n == "WeaponHandling" || n.rfind("resist.", 0) == 0;
}
inline Attribute* ensure_bonus_stat(AttributeSet& s, const std::string& n) {
    if (Attribute* a = s.find(n)) return a;
    if (!is_bonus_stat(n)) return nullptr;
    return &s.define(n, 0.0f, -1000.0f, 1000.0f);
}
/// A bonus stat's value (0 when the set has none).
inline float bonus_stat(const AttributeSet* s, const char* n) {
    if (!s) return 0.0f;
    const Attribute* a = s->find(n);
    return a ? a->current : 0.0f;
}

inline float damage_bonus(const AttributeSet* s, const char* specialization) {
    return std::max(0.0f, 1.0f + (bonus_stat(s, "Damage") + bonus_stat(s, specialization)) / 100.0f);
}

// ---- custom serialization: count, then per attribute (name, base, current, min, max) ----
// Byte helpers (detail::put_bytes/get_bytes) come from component_serialize.h.

inline void serialize_attribute_set(const void* obj, std::vector<uint8_t>& out) {
    const AttributeSet& s = *static_cast<const AttributeSet*>(obj);
    const uint32_t count = static_cast<uint32_t>(s.attributes.size());
    detail::put_bytes(out, &count, 4);
    for (const auto& a : s.attributes) {
        const uint32_t len = static_cast<uint32_t>(a.name.size());
        detail::put_bytes(out, &len, 4);
        detail::put_bytes(out, a.name.data(), len);
        detail::put_bytes(out, &a.base, 4);
        detail::put_bytes(out, &a.current, 4);
        detail::put_bytes(out, &a.min, 4);
        detail::put_bytes(out, &a.max, 4);
    }
}

inline void deserialize_attribute_set(void* obj, const uint8_t* data, size_t n) {
    AttributeSet& s = *static_cast<AttributeSet*>(obj);
    s.attributes.clear();
    const uint8_t* p = data; const uint8_t* e = data + n;
    uint32_t count = 0;
    if (!detail::get_bytes(p, e, &count, 4)) return;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t len = 0;
        if (!detail::get_bytes(p, e, &len, 4) || p + len > e) return;
        Attribute a;
        a.name.assign(reinterpret_cast<const char*>(p), len); p += len;
        if (!detail::get_bytes(p, e, &a.base, 4))    return;
        if (!detail::get_bytes(p, e, &a.current, 4)) return;
        if (!detail::get_bytes(p, e, &a.min, 4))     return;
        if (!detail::get_bytes(p, e, &a.max, 4))     return;
        s.attributes.push_back(std::move(a));
    }
}

// Make AttributeSet an authorable component (custom serialization; the editor
// supplies a custom inspector). Idempotent — call once at startup.
inline void register_attribute_component() {
    register_authorable(make_authorable_custom<AttributeSet>(
        "Attributes", &serialize_attribute_set, &deserialize_attribute_set));
}

}  // namespace schizo::ecs
