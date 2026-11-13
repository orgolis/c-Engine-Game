#pragma once

// ============================================================================
// Weapon loadout -- the guns a player carries; one of them is in hand.
// ============================================================================
//
// WeaponState is the gun in hand (its magazine, cooldown, recoil). The
// WeaponLoadout lists every gun carried, in the order they came, with the
// rounds left in each magazine, so switching back finds a gun as it was left.
// Switching puts the gun in hand away and draws another: it fires once its
// draw time (`draw` in its weapon block) has passed.
//
// An entity with a WeaponState and no loadout carries just that gun; the
// loadout is made the first time it is needed.
//
// Picking up a gun (a Pickup whose item is a weapon's id) adds it here.
// Checked by tools/gameplay_check.

#include "world.h"
#include "gameplay_events.h"
#include "gameplay_weapons.h"
#include "authorable_components.h"
#include "component_serialize.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace schizo::ecs {

struct LoadoutSlot {
    std::string weapon_id;
    int         ammo_in_mag = 0;   // while it is put away
};
struct WeaponLoadout {
    std::vector<LoadoutSlot> slots;
    int current = 0;               // the one in hand
    int last = -1;                 // the one in hand before (Last Weapon)
};

inline int loadout_index(const WeaponLoadout& l, const std::string& id) {
    for (size_t i = 0; i < l.slots.size(); ++i)
        if (l.slots[i].weapon_id == id) return static_cast<int>(i);
    return -1;
}

/// The entity's loadout, made from the gun in hand when there is none yet,
/// and kept pointing at the gun in hand. Null when it has no gun at all.
inline WeaponLoadout* ensure_loadout(World& w, Entity e) {
    const WeaponState* ws = w.try_get<WeaponState>(e);
    if (!w.has<WeaponLoadout>(e)) {
        if (!ws || ws->weapon_id.empty()) return nullptr;
        WeaponLoadout l;
        l.slots.push_back({ws->weapon_id, ws->ammo_in_mag});
        w.add<WeaponLoadout>(e, l);
        ws = w.try_get<WeaponState>(e);
    }
    WeaponLoadout& l = w.get<WeaponLoadout>(e);
    if (ws && !ws->weapon_id.empty()) {
        int i = loadout_index(l, ws->weapon_id);
        if (i < 0) {
            l.slots.push_back({ws->weapon_id, ws->ammo_in_mag});
            i = static_cast<int>(l.slots.size()) - 1;
        }
        l.current = i;
    }
    if (l.last >= static_cast<int>(l.slots.size()) || l.last == l.current) l.last = -1;
    return l.slots.empty() ? nullptr : &l;
}

/// Put the gun in hand away and draw the one at `index`. False when there is
/// no such gun or it is already in hand.
inline bool select_weapon(World& w, Entity e, int index, GameplayEventBus* bus = nullptr) {
    WeaponLoadout* l = ensure_loadout(w, e);
    if (!l || index < 0 || index >= static_cast<int>(l->slots.size())) return false;
    WeaponState* ws = w.try_get<WeaponState>(e);
    if (ws && index == l->current && ws->weapon_id == l->slots[static_cast<size_t>(index)].weapon_id) return false;
    if (ws)   // rounds loaded since the last frame leave the bag before the gun is put away
        if (Inventory* inv = w.try_get<Inventory>(e)) sync_ammo_with_inventory(*ws, *inv);
    if (ws && l->current >= 0 && l->current < static_cast<int>(l->slots.size()) &&
        l->slots[static_cast<size_t>(l->current)].weapon_id == ws->weapon_id)
        l->slots[static_cast<size_t>(l->current)].ammo_in_mag = ws->ammo_in_mag;
    l->last = l->current;
    l->current = index;
    const LoadoutSlot& s = l->slots[static_cast<size_t>(index)];
    WeaponState drawn;
    drawn.weapon_id = s.weapon_id;
    drawn.ammo_in_mag = s.ammo_in_mag;
    const WeaponDef* d = find_weapon(s.weapon_id);
    drawn.cooldown = d ? std::max(0.0f, d->draw_time) : 0.0f;   // drawing it
    if (ws) *ws = drawn;
    else w.add<WeaponState>(e, drawn);
    if (bus) {
        GameplayEvent ev; ev.name = "weapon.switched"; ev.target = e; ev.param = s.weapon_id;
        ev.value = static_cast<float>(index);
        bus->publish(ev);
    }
    return true;
}

/// The next (+1) or previous (-1) gun carried, round the list.
inline bool cycle_weapon(World& w, Entity e, int step, GameplayEventBus* bus = nullptr) {
    WeaponLoadout* l = ensure_loadout(w, e);
    if (!l || l->slots.size() < 2 || step == 0) return false;
    const int n = static_cast<int>(l->slots.size());
    return select_weapon(w, e, ((l->current + step) % n + n) % n, bus);
}

/// Back to the gun in hand before this one.
inline bool last_weapon(World& w, Entity e, GameplayEventBus* bus = nullptr) {
    WeaponLoadout* l = ensure_loadout(w, e);
    return l && l->last >= 0 && select_weapon(w, e, l->last, bus);
}

/// Carry a gun from now on (with a full magazine). The first gun goes into
/// the hand. False when it is unknown or already carried.
inline bool give_weapon(World& w, Entity e, const std::string& id, GameplayEventBus* bus = nullptr) {
    const WeaponDef* d = find_weapon(id);
    if (!d) return false;
    WeaponLoadout* l = ensure_loadout(w, e);
    if (l && loadout_index(*l, id) >= 0) return false;
    if (!l) {
        WeaponState ws;
        ws.weapon_id = id;
        ws.ammo_in_mag = d->mag_size;
        if (w.has<WeaponState>(e)) w.get<WeaponState>(e) = ws;
        else w.add<WeaponState>(e, ws);
        ensure_loadout(w, e);
    } else {
        l->slots.push_back({id, d->mag_size});
    }
    if (bus) { GameplayEvent ev; ev.name = "weapon.given"; ev.target = e; ev.param = id; bus->publish(ev); }
    return true;
}

// ---- the component ------------------------------------------------------------------------
inline void serialize_loadout(const void* obj, std::vector<uint8_t>& out) {
    const auto& l = *static_cast<const WeaponLoadout*>(obj);
    detail::put_i32(out, 1);
    detail::put_i32(out, static_cast<int32_t>(l.slots.size()));
    for (const auto& s : l.slots) { detail::put_str(out, s.weapon_id); detail::put_i32(out, s.ammo_in_mag); }
    detail::put_i32(out, l.current);
    detail::put_i32(out, l.last);
}
inline void deserialize_loadout(void* obj, const uint8_t* data, size_t n) {
    auto& l = *static_cast<WeaponLoadout*>(obj);
    l = WeaponLoadout{};
    const uint8_t* p = data;
    const uint8_t* e = data + n;
    int32_t version = 0, count = 0;
    if (!detail::get_i32(p, e, version) || !detail::get_i32(p, e, count)) return;
    for (int32_t i = 0; i < count; ++i) {
        LoadoutSlot s;
        if (!detail::get_str(p, e, s.weapon_id) || !detail::get_i32(p, e, s.ammo_in_mag)) return;
        l.slots.push_back(std::move(s));
    }
    int32_t cur = 0, last = -1;
    if (detail::get_i32(p, e, cur)) l.current = cur;
    if (detail::get_i32(p, e, last)) l.last = last;
}
inline void register_loadout_components() {
    register_authorable(make_authorable_custom<WeaponLoadout>("Weapon Loadout", &serialize_loadout, &deserialize_loadout));
}

}  // namespace schizo::ecs
