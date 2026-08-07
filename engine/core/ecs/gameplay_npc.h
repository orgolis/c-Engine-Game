#pragma once

// ============================================================================
// G8 · NPCs & encounters — factions, reputation, aggro/threat, spawners.
// ============================================================================
//
// Factions gate who is hostile to whom (a data-driven relationship table);
// reputation is a per-entity standing toward a faction (stored as G0 attributes
// "rep.<faction>", so it composes with the stat system). Aggro tracks a threat
// table per NPC and picks the highest-threat target (fed by combat events).
// Spawners fire "spawn.request" events on an interval up to a live cap — the
// integration layer instantiates (e.g. From-Prefab) and reports deaths back.

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

// ---------------- factions ----------------
enum class Standing : uint32_t { Hostile = 0, Neutral = 1, Friendly = 2 };
struct Faction { std::string faction; };

struct FactionRelation { std::string a, b; Standing standing; };
inline std::vector<FactionRelation>& faction_relations() { static std::vector<FactionRelation> r; return r; }
inline void set_faction_standing(const std::string& a, const std::string& b, Standing s) {
    for (auto& r : faction_relations()) if ((r.a == a && r.b == b) || (r.a == b && r.b == a)) { r.standing = s; return; }
    faction_relations().push_back({a, b, s});
}
inline Standing faction_standing(const std::string& a, const std::string& b) {
    if (a == b) return Standing::Friendly;               // same faction = friendly
    for (const auto& r : faction_relations())
        if ((r.a == a && r.b == b) || (r.a == b && r.b == a)) return r.standing;
    return Standing::Neutral;                             // default for unrelated factions
}
inline Standing standing_between(World& w, Entity e1, Entity e2) {
    const Faction* f1 = w.try_get<Faction>(e1);
    const Faction* f2 = w.try_get<Faction>(e2);
    return (f1 && f2) ? faction_standing(f1->faction, f2->faction) : Standing::Neutral;
}
inline bool are_hostile(World& w, Entity e1, Entity e2) { return standing_between(w, e1, e2) == Standing::Hostile; }

// ---------------- reputation (attribute "rep.<faction>") ----------------
inline float reputation(World& w, Entity e, const std::string& faction) {
    const AttributeSet* a = w.try_get<AttributeSet>(e);
    return a ? a->get("rep." + faction) : 0.0f;
}
inline void adjust_reputation(World& w, Entity e, const std::string& faction, float delta) {
    if (!w.has<AttributeSet>(e)) w.add<AttributeSet>(e, AttributeSet{});
    AttributeSet& a = w.get<AttributeSet>(e);
    const std::string key = "rep." + faction;
    if (!a.find(key)) a.define(key, 0.0f, -1e9f, 1e9f);
    a.adjust(key, delta);
}

// ---------------- aggro / threat ----------------
struct ThreatEntry { uint32_t source; float threat; };
struct Aggro {
    float leash = 20.0f;                   // authored
    std::vector<ThreatEntry> table;        // runtime
    uint32_t target = 0xFFFFFFFFu;         // runtime (0xFFFFFFFF = none)
};
inline void add_threat(World& w, Entity npc, Entity source, float amount) {
    Aggro* ag = w.try_get<Aggro>(npc);
    if (!ag) return;
    const uint32_t sid = static_cast<uint32_t>(source);
    ThreatEntry* te = nullptr;
    for (auto& t : ag->table) if (t.source == sid) { te = &t; break; }
    if (!te) { ag->table.push_back({sid, 0.0f}); te = &ag->table.back(); }
    te->threat += amount;
    float best = -1.0f; uint32_t bestid = 0xFFFFFFFFu;
    for (const auto& t : ag->table) if (t.threat > best) { best = t.threat; bestid = t.source; }
    ag->target = bestid;
}
inline Entity aggro_target(World& w, Entity npc) {
    const Aggro* ag = w.try_get<Aggro>(npc);
    return (ag && ag->target != 0xFFFFFFFFu) ? static_cast<Entity>(ag->target) : null_entity;
}
inline void clear_threat(World& w, Entity npc) {
    if (Aggro* ag = w.try_get<Aggro>(npc)) { ag->table.clear(); ag->target = 0xFFFFFFFFu; }
}
inline void tick_aggro(World& w, float dt, float decay_per_sec = 0.0f) {
    if (decay_per_sec <= 0.0f) return;
    w.each<Aggro>([&](Entity, Aggro& ag) {
        for (auto& t : ag.table) t.threat = std::max(0.0f, t.threat - decay_per_sec * dt);
        float best = -1.0f; uint32_t bestid = 0xFFFFFFFFu;
        for (const auto& t : ag.table) if (t.threat > best) { best = t.threat; bestid = t.source; }
        ag.target = best > 0.0f ? bestid : 0xFFFFFFFFu;
    });
}

// ---------------- spawner / encounter ----------------
struct Spawner {
    std::string spawn_id;      // prefab/marker the integration layer instantiates
    int   max_alive = 3;
    float interval  = 5.0f;
    float radius    = 5.0f;
    float timer = 0.0f;        // runtime
    int   alive = 0;           // runtime
};
// Fire "spawn.request" (param = spawn_id, target = spawner) each interval while alive < max.
inline void tick_spawners(World& w, float dt, GameplayEventBus* bus = nullptr) {
    w.each<Spawner>([&](Entity e, Spawner& s) {
        if (s.alive >= s.max_alive) return;
        s.timer += dt;
        if (s.timer >= s.interval) {
            s.timer = 0.0f;
            ++s.alive;
            if (bus) { GameplayEvent ev; ev.name = "spawn.request"; ev.target = e; ev.param = s.spawn_id;
                       ev.value = static_cast<float>(s.alive); bus->publish(ev); }
        }
    });
}
inline void spawner_report_death(World& w, Entity spawner) {
    if (Spawner* s = w.try_get<Spawner>(spawner)) s->alive = std::max(0, s->alive - 1);
}

// ---------------- custom serialization ----------------
inline void serialize_faction(const void* obj, std::vector<uint8_t>& out) {
    detail::put_str(out, static_cast<const Faction*>(obj)->faction);
}
inline void deserialize_faction(void* obj, const uint8_t* data, size_t n) {
    const uint8_t* p = data; const uint8_t* e = data + n;
    detail::get_str(p, e, static_cast<Faction*>(obj)->faction);
}
inline void serialize_aggro(const void* obj, std::vector<uint8_t>& out) {
    detail::put_f32(out, static_cast<const Aggro*>(obj)->leash);   // runtime table/target skipped
}
inline void deserialize_aggro(void* obj, const uint8_t* data, size_t n) {
    const uint8_t* p = data; const uint8_t* e = data + n;
    auto& ag = *static_cast<Aggro*>(obj);
    detail::get_f32(p, e, ag.leash); ag.table.clear(); ag.target = 0xFFFFFFFFu;
}
inline void serialize_spawner(const void* obj, std::vector<uint8_t>& out) {
    const auto& s = *static_cast<const Spawner*>(obj);
    detail::put_str(out, s.spawn_id);
    detail::put_i32(out, s.max_alive);
    detail::put_f32(out, s.interval);
    detail::put_f32(out, s.radius);
}
inline void deserialize_spawner(void* obj, const uint8_t* data, size_t n) {
    auto& s = *static_cast<Spawner*>(obj);
    const uint8_t* p = data; const uint8_t* e = data + n;
    if (!detail::get_str(p, e, s.spawn_id)) return;
    detail::get_i32(p, e, s.max_alive);
    detail::get_f32(p, e, s.interval);
    detail::get_f32(p, e, s.radius);
    s.timer = 0.0f; s.alive = 0;
}

inline void register_npc_components() {
    register_authorable(make_authorable_custom<Faction>("Faction", &serialize_faction, &deserialize_faction));
    register_authorable(make_authorable_custom<Aggro>("Aggro", &serialize_aggro, &deserialize_aggro));
    register_authorable(make_authorable_custom<Spawner>("Spawner", &serialize_spawner, &deserialize_spawner));
}

}  // namespace schizo::ecs
