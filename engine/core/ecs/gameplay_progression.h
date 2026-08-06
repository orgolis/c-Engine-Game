#pragma once

// ============================================================================
// G3 · Stats & progression — derived stats, resource regen, XP / leveling.
// ============================================================================
//
// Built entirely on G0's data-driven AttributeSet (attributes by name), so a game
// defines its own stat model with no engine changes:
//
//   DerivedStats  — a target attribute computed as base + Σ(coeff * source), e.g.
//                   MaxHealth = 50 + 10*Vitality, AttackPower = 2*Strength. Set
//                   set_max to drive an attribute's MAX (a resource cap) instead
//                   of its current value.
//   Regeneration  — per-attribute regen rate (HP/mana/stamina refill over time).
//   Progression   — level + XP with a data-driven curve, skill points per level,
//                   and per-level attribute growth; grant_xp() levels up + fires
//                   a "level.up" event.

#include "world.h"
#include "gameplay_attributes.h"
#include "gameplay_events.h"
#include "authorable_components.h"
#include "component_serialize.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace schizo::ecs {

// ---------------- Derived stats ----------------
struct DerivedTerm { std::string source; float coeff = 1.0f; };
struct DerivedStat {
    std::string target;                 // attribute to write (defined if missing)
    float       base = 0.0f;
    std::vector<DerivedTerm> terms;     // linear combination of source attributes
    bool        set_max = false;        // drive the target's MAX (resource cap) not current
};
struct DerivedStats { std::vector<DerivedStat> stats; };

inline void recompute_derived(World& w, Entity e) {
    AttributeSet* attrs = w.try_get<AttributeSet>(e);
    DerivedStats* ds    = w.try_get<DerivedStats>(e);
    if (!attrs || !ds) return;
    for (const auto& d : ds->stats) {
        float v = d.base;
        for (const auto& t : d.terms) v += t.coeff * attrs->get(t.source, 0.0f);
        Attribute* a = attrs->find(d.target);
        if (!a) a = &attrs->define(d.target, v, 0.0f, d.set_max ? v : 1e9f);
        if (d.set_max) {
            a->max = v;
            if (a->current > a->max) a->current = a->max;
        } else {
            a->base = v;
            a->current = std::max(a->min, std::min(a->max, v));
        }
    }
}

// ---------------- Resource regeneration ----------------
struct RegenEntry { std::string attribute; float per_second = 0.0f; };
struct Regeneration { std::vector<RegenEntry> entries; };

inline void tick_regen(World& w, float dt) {
    w.each<Regeneration, AttributeSet>([&](Entity, Regeneration& r, AttributeSet& a) {
        for (const auto& e : r.entries)
            if (e.per_second != 0.0f) a.adjust(e.attribute, e.per_second * dt);
    });
}

// ---------------- Leveling / XP ----------------
struct XpCurve {
    float base = 100.0f, linear = 50.0f, quadratic = 0.0f;
    // XP required to advance INTO `level` (from level-1).
    float threshold(int level) const {
        const float n = static_cast<float>(level - 1);
        return base + linear * n + quadratic * n * n;
    }
};
struct StatGrowth { std::string attribute; float per_level = 0.0f; };
struct Progression {
    int     level = 1;
    float   xp = 0.0f;                   // XP banked toward the next level
    int     skill_points = 0;
    int     points_per_level = 1;
    XpCurve curve;
    std::vector<StatGrowth> per_level_growth;   // attributes raised each level-up
};

// Grant XP; returns the number of levels gained. Fires "level.up" per level.
inline int grant_xp(World& w, Entity e, float amount, GameplayEventBus* bus = nullptr) {
    Progression* p = w.try_get<Progression>(e);
    if (!p || amount <= 0.0f) return 0;
    p->xp += amount;
    int gained = 0;
    while (p->xp >= p->curve.threshold(p->level + 1)) {
        p->xp -= p->curve.threshold(p->level + 1);
        ++p->level;
        ++gained;
        p->skill_points += p->points_per_level;
        if (AttributeSet* attrs = w.try_get<AttributeSet>(e))
            for (const auto& g : p->per_level_growth)
                if (Attribute* a = attrs->find(g.attribute)) {
                    a->base += g.per_level; a->max += g.per_level; a->current += g.per_level;
                }
        if (bus) { GameplayEvent ev; ev.name = "level.up"; ev.target = e; ev.value = static_cast<float>(p->level); bus->publish(ev); }
    }
    if (gained && w.has<DerivedStats>(e)) recompute_derived(w, e);
    return gained;
}

// ---------------- custom serialization ----------------
inline void serialize_derived_stats(const void* obj, std::vector<uint8_t>& out) {
    const auto& ds = *static_cast<const DerivedStats*>(obj);
    uint32_t n = static_cast<uint32_t>(ds.stats.size()); detail::put_bytes(out, &n, 4);
    for (const auto& d : ds.stats) {
        detail::put_str(out, d.target);
        detail::put_f32(out, d.base);
        const uint8_t sm = d.set_max ? 1 : 0; detail::put_bytes(out, &sm, 1);
        uint32_t nt = static_cast<uint32_t>(d.terms.size()); detail::put_bytes(out, &nt, 4);
        for (const auto& t : d.terms) { detail::put_str(out, t.source); detail::put_f32(out, t.coeff); }
    }
}
inline void deserialize_derived_stats(void* obj, const uint8_t* data, size_t n) {
    auto& ds = *static_cast<DerivedStats*>(obj); ds.stats.clear();
    const uint8_t* p = data; const uint8_t* e = data + n;
    uint32_t ns = 0; if (!detail::get_bytes(p, e, &ns, 4)) return;
    for (uint32_t i = 0; i < ns; ++i) {
        DerivedStat d;
        if (!detail::get_str(p, e, d.target)) return;
        if (!detail::get_f32(p, e, d.base))   return;
        uint8_t sm = 0; if (!detail::get_bytes(p, e, &sm, 1)) return; d.set_max = sm != 0;
        uint32_t nt = 0; if (!detail::get_bytes(p, e, &nt, 4)) return;
        for (uint32_t j = 0; j < nt; ++j) {
            DerivedTerm t;
            if (!detail::get_str(p, e, t.source)) return;
            if (!detail::get_f32(p, e, t.coeff))  return;
            d.terms.push_back(std::move(t));
        }
        ds.stats.push_back(std::move(d));
    }
}

inline void serialize_regen(const void* obj, std::vector<uint8_t>& out) {
    const auto& r = *static_cast<const Regeneration*>(obj);
    uint32_t n = static_cast<uint32_t>(r.entries.size()); detail::put_bytes(out, &n, 4);
    for (const auto& en : r.entries) { detail::put_str(out, en.attribute); detail::put_f32(out, en.per_second); }
}
inline void deserialize_regen(void* obj, const uint8_t* data, size_t n) {
    auto& r = *static_cast<Regeneration*>(obj); r.entries.clear();
    const uint8_t* p = data; const uint8_t* e = data + n;
    uint32_t ne = 0; if (!detail::get_bytes(p, e, &ne, 4)) return;
    for (uint32_t i = 0; i < ne; ++i) {
        RegenEntry en;
        if (!detail::get_str(p, e, en.attribute)) return;
        if (!detail::get_f32(p, e, en.per_second)) return;
        r.entries.push_back(std::move(en));
    }
}

inline void serialize_progression(const void* obj, std::vector<uint8_t>& out) {
    const auto& g = *static_cast<const Progression*>(obj);
    detail::put_i32(out, g.level);
    detail::put_f32(out, g.xp);
    detail::put_i32(out, g.skill_points);
    detail::put_i32(out, g.points_per_level);
    detail::put_f32(out, g.curve.base);
    detail::put_f32(out, g.curve.linear);
    detail::put_f32(out, g.curve.quadratic);
    uint32_t n = static_cast<uint32_t>(g.per_level_growth.size()); detail::put_bytes(out, &n, 4);
    for (const auto& s : g.per_level_growth) { detail::put_str(out, s.attribute); detail::put_f32(out, s.per_level); }
}
inline void deserialize_progression(void* obj, const uint8_t* data, size_t n) {
    auto& g = *static_cast<Progression*>(obj);
    const uint8_t* p = data; const uint8_t* e = data + n;
    if (!detail::get_i32(p, e, g.level))            return;
    if (!detail::get_f32(p, e, g.xp))               return;
    if (!detail::get_i32(p, e, g.skill_points))     return;
    if (!detail::get_i32(p, e, g.points_per_level)) return;
    if (!detail::get_f32(p, e, g.curve.base))       return;
    if (!detail::get_f32(p, e, g.curve.linear))     return;
    if (!detail::get_f32(p, e, g.curve.quadratic))  return;
    g.per_level_growth.clear();
    uint32_t ng = 0; if (!detail::get_bytes(p, e, &ng, 4)) return;
    for (uint32_t i = 0; i < ng; ++i) {
        StatGrowth s;
        if (!detail::get_str(p, e, s.attribute)) return;
        if (!detail::get_f32(p, e, s.per_level)) return;
        g.per_level_growth.push_back(std::move(s));
    }
}

inline void register_progression_components() {
    register_authorable(make_authorable_custom<DerivedStats>("Derived Stats", &serialize_derived_stats, &deserialize_derived_stats));
    register_authorable(make_authorable_custom<Regeneration>("Regeneration", &serialize_regen, &deserialize_regen));
    register_authorable(make_authorable_custom<Progression>("Progression", &serialize_progression, &deserialize_progression));
}

}  // namespace schizo::ecs
