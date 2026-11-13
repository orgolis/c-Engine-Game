#pragma once

// ============================================================================
// D0 · Life and death -- what happens when health runs out.
// ============================================================================
//
// Damage already lowered an attribute; nothing ever acted on it reaching zero:
// an enemy with no health kept chasing, the player kept playing. A Mortal
// entity dies when its health attribute reaches zero:
//
//   - it is tagged state.dead and a "died" gameplay event goes out (scripts
//     count kills, the AI stops: npc agents skip the dead),
//   - its loot table is rolled -- what drops is handed back for the host to
//     put in the world as pickups,
//   - after its corpse time it is "removed" (the host hides it) -- unless it
//     respawns (the player), in which case the host brings it back.
//
// `tick_life` is pure; tools/gameplay_check runs it.

#include "world.h"
#include "authorable_components.h"
#include "component_serialize.h"
#include "gameplay_attributes.h"
#include "gameplay_events.h"
#include "gameplay_items.h"
#include "gameplay_tags.h"

#include <cstdint>
#include <string>
#include <vector>

namespace schizo::ecs {

struct Mortal {
    std::string health = "Health";      // the attribute that is life
    float corpse_time = 3.0f;           // seconds a body stays before it is removed
    bool  respawns = false;             // the player: comes back instead of being removed
    std::vector<DropEntry> loot;        // what it may drop
    int   loot_rolls = 1;
    // Runtime (not saved)
    bool  dead = false;
    float dead_time = 0.0f;
    bool  removed = false;
};

struct LifeEvent {
    enum class Kind : uint8_t { Died, Removed, Respawn } kind = Kind::Died;
    Entity entity = null_entity;
    std::vector<ItemInstance> loot;     // Died: what dropped
};

/// Back to life with full health (a respawn, or a loaded save).
inline void revive(World& w, Entity e, Mortal& m, AttributeSet& a) {
    m.dead = false;
    m.dead_time = 0.0f;
    m.removed = false;
    if (Attribute* h = a.find(m.health)) h->current = h->max;
    if (GameplayTags* t = w.try_get<GameplayTags>(e)) t->remove_exact("state.dead");
}

inline bool is_dead(World& w, Entity e) {
    const GameplayTags* t = w.try_get<GameplayTags>(e);
    return t && t->has("state.dead");
}

/// One frame. `respawn_after` is how long the dead who respawn wait.
inline void tick_life(World& w, float dt, GameplayEventBus* bus, Rng& rng, std::vector<LifeEvent>& out,
                      float respawn_after = 2.5f) {
    w.each<Mortal, AttributeSet>([&](Entity e, Mortal& m, AttributeSet& a) {
        if (!m.dead) {
            const Attribute* h = a.find(m.health);
            if (!h || h->current > 0.0f) return;
            m.dead = true;
            m.dead_time = 0.0f;
            m.removed = false;
            if (!w.has<GameplayTags>(e)) w.add<GameplayTags>(e, GameplayTags{});
            w.get<GameplayTags>(e).add("state.dead");
            LifeEvent ev;
            ev.kind = LifeEvent::Kind::Died;
            ev.entity = e;
            if (!m.loot.empty()) {
                DropTable t;
                t.entries = m.loot;
                t.rolls = m.loot_rolls;
                ev.loot = roll_loot(t, rng);
            }
            if (bus) { GameplayEvent ge; ge.name = "died"; ge.target = e; ge.value = static_cast<float>(ev.loot.size()); bus->publish(ge); }
            out.push_back(std::move(ev));
            return;
        }
        m.dead_time += dt;
        if (m.respawns) {
            if (m.dead_time >= respawn_after) {
                LifeEvent ev;
                ev.kind = LifeEvent::Kind::Respawn;
                ev.entity = e;
                out.push_back(ev);
                revive(w, e, m, a);
                if (bus) { GameplayEvent ge; ge.name = "respawned"; ge.target = e; bus->publish(ge); }
            }
        } else if (!m.removed && m.dead_time >= m.corpse_time) {
            m.removed = true;
            LifeEvent ev;
            ev.kind = LifeEvent::Kind::Removed;
            ev.entity = e;
            out.push_back(ev);
        }
    });
}

// ---- saving: the settings ------------------------------------------------------
inline void serialize_mortal(const void* obj, std::vector<uint8_t>& out) {
    const auto& m = *static_cast<const Mortal*>(obj);
    detail::put_i32(out, 1);
    detail::put_str(out, m.health);
    detail::put_f32(out, m.corpse_time);
    detail::put_i32(out, m.respawns ? 1 : 0);
    detail::put_i32(out, m.loot_rolls);
    detail::put_i32(out, static_cast<int32_t>(m.loot.size()));
    for (const auto& d : m.loot) {
        detail::put_str(out, d.item_id); detail::put_f32(out, d.weight); detail::put_f32(out, d.chance);
        detail::put_i32(out, d.min_qty); detail::put_i32(out, d.max_qty);
    }
}
inline void deserialize_mortal(void* obj, const uint8_t* data, size_t n) {
    auto& m = *static_cast<Mortal*>(obj);
    m = Mortal{};
    const uint8_t* p = data; const uint8_t* e = data + n;
    int32_t ver = 0, rs = 0, cnt = 0;
    if (!detail::get_i32(p, e, ver) || ver < 1) return;
    detail::get_str(p, e, m.health);
    detail::get_f32(p, e, m.corpse_time);
    if (detail::get_i32(p, e, rs)) m.respawns = rs != 0;
    detail::get_i32(p, e, m.loot_rolls);
    if (!detail::get_i32(p, e, cnt)) return;
    for (int32_t i = 0; i < cnt; ++i) {
        DropEntry d;
        if (!detail::get_str(p, e, d.item_id) || !detail::get_f32(p, e, d.weight) || !detail::get_f32(p, e, d.chance) ||
            !detail::get_i32(p, e, d.min_qty) || !detail::get_i32(p, e, d.max_qty)) return;
        m.loot.push_back(d);
    }
}
inline void register_life_components() {
    register_authorable(make_authorable_custom<Mortal>("Mortal", &serialize_mortal, &deserialize_mortal));
}

}  // namespace schizo::ecs
