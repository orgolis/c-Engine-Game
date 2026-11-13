#pragma once

// ============================================================================
// Weapon loadout -- the guns a player carries; one of them is in hand.
// ============================================================================
//
// WeaponState is the gun in hand (its magazine, cooldown, recoil). The
// WeaponLoadout lists the guns carried, each in its slot, with the rounds left
// in each magazine, so switching back finds a gun as it was left. Switching
// puts the gun in hand away and draws another: it fires once its draw time
// (`draw` in its weapon block) has passed.
//
// Weapon slots: `max_slots` limits how many guns are carried (0: any number).
// A slot keeps its place when it is emptied, so the keys for slots 1, 2, 3
// stay with them. Guns beyond the slots go into the bag (the Inventory) as
// items with the gun's id; using one there (Use Item, Equip Weapon) puts it
// into a slot -- the first empty one, or in place of the gun in hand -- and
// what was there goes into the bag. A gun's magazine goes into and comes out
// of the bag as its ammunition, so no rounds are made or lost on the way.
//
// An entity with a WeaponState and no loadout carries just that gun; the
// loadout is made the first time it is needed.
//
// Picking up a gun (a Pickup whose item is a weapon's id): receive_weapon.
// Checked by tools/gameplay_check.

#include "world.h"
#include "gameplay_events.h"
#include "gameplay_inventory.h"
#include "gameplay_weapons.h"
#include "authorable_components.h"
#include "component_serialize.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace schizo::ecs {

struct LoadoutSlot {
    std::string weapon_id;         // "": an empty slot
    int         ammo_in_mag = 0;   // while it is put away
};
struct WeaponLoadout {
    std::vector<LoadoutSlot> slots;
    int current = 0;               // the one in hand (-1: the hand is empty)
    int last = -1;                 // the one in hand before (Last Weapon)
    int max_slots = 0;             // how many guns can be carried (0: any number)
};

inline bool slot_filled(const WeaponLoadout& l, int i) {
    return i >= 0 && i < static_cast<int>(l.slots.size()) && !l.slots[static_cast<size_t>(i)].weapon_id.empty();
}
inline int loadout_index(const WeaponLoadout& l, const std::string& id) {
    if (id.empty()) return -1;
    for (size_t i = 0; i < l.slots.size(); ++i)
        if (l.slots[i].weapon_id == id) return static_cast<int>(i);
    return -1;
}
inline int loadout_count(const WeaponLoadout& l) {
    int n = 0;
    for (const auto& s : l.slots) n += s.weapon_id.empty() ? 0 : 1;
    return n;
}
/// Where another gun would go: the first empty slot, or a new one while there
/// is room. -1 when every slot is full.
inline int loadout_free_slot(const WeaponLoadout& l) {
    const int n = static_cast<int>(l.slots.size());
    for (int i = 0; i < n && (l.max_slots <= 0 || i < l.max_slots); ++i)
        if (l.slots[static_cast<size_t>(i)].weapon_id.empty()) return i;
    return l.max_slots <= 0 || n < l.max_slots ? n : -1;
}

/// What to call an item or a gun in the bag: its name, else its id.
inline std::string carried_name(const std::string& id) {
    if (const ItemDef* d = find_item(id)) return d->name;
    if (const WeaponDef* d = find_weapon(id)) return d->name;
    return id;
}
/// A gun in a line: "24 damage, 12 rounds, 400 rpm" (x pellets for a shotgun).
inline std::string weapon_summary(const WeaponDef& d) {
    return std::to_string(static_cast<int>(d.damage)) + " damage" +
           (d.pellets > 1 ? " x" + std::to_string(d.pellets) : std::string()) + ", " + std::to_string(d.mag_size) +
           " rounds, " + std::to_string(static_cast<int>(d.rpm)) + " rpm";
}

/// The entity's loadout, made from the gun in hand when there is none yet,
/// and kept pointing at the gun in hand. Null when it has neither.
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
    if (l.max_slots > 0 && static_cast<int>(l.slots.size()) < l.max_slots) l.slots.resize(static_cast<size_t>(l.max_slots));
    if (ws && !ws->weapon_id.empty()) {
        int i = loadout_index(l, ws->weapon_id);
        if (i < 0) {   // put in hand some other way (a script, an older save)
            i = loadout_free_slot(l);
            if (i < 0) i = static_cast<int>(l.slots.size());   // over the limit rather than lost
            if (i >= static_cast<int>(l.slots.size())) l.slots.resize(static_cast<size_t>(i) + 1);
            l.slots[static_cast<size_t>(i)] = {ws->weapon_id, ws->ammo_in_mag};
        }
        l.current = i;
    } else {
        l.current = -1;
    }
    if (!slot_filled(l, l.last) || l.last == l.current) l.last = -1;
    return &l;
}

namespace loadout_detail {
/// The gun in hand's rounds into its slot (and what a reload took out of the bag).
inline void settle_hand(World& w, Entity e, WeaponLoadout& l) {
    WeaponState* ws = w.try_get<WeaponState>(e);
    if (!ws || !slot_filled(l, l.current) || l.slots[static_cast<size_t>(l.current)].weapon_id != ws->weapon_id) return;
    if (Inventory* inv = w.try_get<Inventory>(e)) sync_ammo_with_inventory(*ws, *inv);
    l.slots[static_cast<size_t>(l.current)].ammo_in_mag = ws->ammo_in_mag;
}
/// The gun at `index`, and the rounds in its magazine (as its ammunition),
/// into `inv`; the slot is emptied. Nothing changes when they do not fit.
inline bool into_bag(WeaponLoadout& l, int index, Inventory& inv) {
    if (!slot_filled(l, index)) return false;
    LoadoutSlot& s = l.slots[static_cast<size_t>(index)];
    Inventory trial = inv;
    if (inventory_add(trial, ItemInstance{s.weapon_id, 1, 0, {}}) > 0) return false;
    const WeaponDef* d = find_weapon(s.weapon_id);
    if (d && !d->ammo_item.empty() && s.ammo_in_mag > 0 &&
        inventory_add(trial, ItemInstance{d->ammo_item, s.ammo_in_mag, 0, {}}) > 0)
        return false;
    inv = std::move(trial);
    s = LoadoutSlot{};
    if (l.last == index) l.last = -1;
    return true;
}
inline Inventory& bag_of(World& w, Entity e) {
    if (!w.has<Inventory>(e)) w.add<Inventory>(e, Inventory{});
    return w.get<Inventory>(e);
}
}  // namespace loadout_detail

/// Put the gun in hand away and draw the one at `index`. False when there is
/// no gun there or it is already in hand.
inline bool select_weapon(World& w, Entity e, int index, GameplayEventBus* bus = nullptr) {
    WeaponLoadout* l = ensure_loadout(w, e);
    if (!l || !slot_filled(*l, index)) return false;
    WeaponState* ws = w.try_get<WeaponState>(e);
    if (ws && index == l->current && ws->weapon_id == l->slots[static_cast<size_t>(index)].weapon_id) return false;
    loadout_detail::settle_hand(w, e, *l);   // rounds loaded since the last frame leave the bag first
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

/// The next (+1) or previous (-1) gun carried, round the slots (empty ones skipped).
inline bool cycle_weapon(World& w, Entity e, int step, GameplayEventBus* bus = nullptr) {
    WeaponLoadout* l = ensure_loadout(w, e);
    if (!l || step == 0 || l->slots.empty()) return false;
    const int n = static_cast<int>(l->slots.size()), dir = step > 0 ? 1 : -1;
    int i = l->current >= 0 ? l->current : (dir > 0 ? -1 : n);
    for (int k = 0; k < n; ++k) {
        i = ((i + dir) % n + n) % n;
        if (i != l->current && slot_filled(*l, i)) return select_weapon(w, e, i, bus);
    }
    return false;
}

/// Back to the gun in hand before this one.
inline bool last_weapon(World& w, Entity e, GameplayEventBus* bus = nullptr) {
    WeaponLoadout* l = ensure_loadout(w, e);
    return l && slot_filled(*l, l->last) && select_weapon(w, e, l->last, bus);
}

/// Carry a gun from now on (with a full magazine), in the first empty slot.
/// An empty hand takes it. False when it is unknown, already carried, or
/// every slot is full.
inline bool give_weapon(World& w, Entity e, const std::string& id, GameplayEventBus* bus = nullptr) {
    const WeaponDef* d = find_weapon(id);
    if (!d) return false;
    WeaponLoadout* l = ensure_loadout(w, e);
    if (!l) {
        WeaponState ws;
        ws.weapon_id = id;
        ws.ammo_in_mag = d->mag_size;
        if (w.has<WeaponState>(e)) w.get<WeaponState>(e) = ws;
        else w.add<WeaponState>(e, ws);
        ensure_loadout(w, e);
    } else {
        if (loadout_index(*l, id) >= 0) return false;
        const int at = loadout_free_slot(*l);
        if (at < 0) return false;
        if (at >= static_cast<int>(l->slots.size())) l->slots.resize(static_cast<size_t>(at) + 1);
        l->slots[static_cast<size_t>(at)] = {id, d->mag_size};
        if (l->current < 0) select_weapon(w, e, at, bus);
    }
    if (bus) { GameplayEvent ev; ev.name = "weapon.given"; ev.target = e; ev.param = id; bus->publish(ev); }
    return true;
}

/// Put the gun at `index` into the bag (the rounds in its magazine go back
/// to its ammunition). When it was in hand, the gun in hand before it is
/// drawn, or another one, or the hand stays empty. False when there is no gun
/// there or the bag has no room.
inline bool stow_weapon(World& w, Entity e, int index, GameplayEventBus* bus = nullptr) {
    WeaponLoadout* l = ensure_loadout(w, e);
    if (!l || !slot_filled(*l, index)) return false;
    loadout_detail::settle_hand(w, e, *l);
    const std::string id = l->slots[static_cast<size_t>(index)].weapon_id;
    const bool was_in_hand = index == l->current;
    const int before = l->last;
    if (!loadout_detail::into_bag(*l, index, loadout_detail::bag_of(w, e))) return false;
    if (was_in_hand) {
        if (WeaponState* ws = w.try_get<WeaponState>(e)) *ws = WeaponState{};
        l->current = -1;
        l->last = -1;
        int next = slot_filled(*l, before) ? before : -1;
        for (int i = 0; next < 0 && i < static_cast<int>(l->slots.size()); ++i)
            if (slot_filled(*l, i)) next = i;
        if (next >= 0) select_weapon(w, e, next, bus);
    }
    if (bus) {
        GameplayEvent ev; ev.name = "weapon.stowed"; ev.target = e; ev.param = id; ev.value = static_cast<float>(index);
        bus->publish(ev);
    }
    return true;
}

/// Take a gun out of the bag into a weapon slot: `slot` (from 0), or with -1
/// the first empty one -- and with every slot full, the gun in hand's. What
/// was in that slot goes into the bag. Its magazine is loaded from the bag's
/// rounds (a gun without an ammunition item comes full). It is drawn when it
/// took the place of the gun in hand or the hand was empty. False when it is
/// not in the bag, not a gun, already carried, or there is no room in the bag
/// for what it replaces.
inline bool equip_weapon_from_inventory(World& w, Entity e, const std::string& id, int slot = -1,
                                        GameplayEventBus* bus = nullptr) {
    const WeaponDef* d = find_weapon(id);
    Inventory* inv = w.try_get<Inventory>(e);
    if (!d || !inv || inventory_count(*inv, id) <= 0) return false;
    WeaponLoadout* l = ensure_loadout(w, e);
    if (!l) {
        w.add<WeaponLoadout>(e, WeaponLoadout{});
        l = ensure_loadout(w, e);
        inv = w.try_get<Inventory>(e);
    }
    if (loadout_index(*l, id) >= 0) return false;
    if (slot >= 0 && l->max_slots > 0 && slot >= l->max_slots) return false;
    int at = slot >= 0 ? slot : loadout_free_slot(*l);
    if (at < 0) at = l->current >= 0 ? l->current : 0;
    const bool to_hand = at == l->current || l->current < 0;
    loadout_detail::settle_hand(w, e, *l);
    Inventory trial = *inv;
    inventory_remove(trial, id, 1);   // out of the bag first: it frees a cell for what it replaces
    if (slot_filled(*l, at) && !loadout_detail::into_bag(*l, at, trial)) return false;
    const int mag = d->ammo_item.empty() ? d->mag_size : inventory_remove(trial, d->ammo_item, d->mag_size);
    *inv = std::move(trial);
    if (at >= static_cast<int>(l->slots.size())) l->slots.resize(static_cast<size_t>(at) + 1);
    l->slots[static_cast<size_t>(at)] = {id, mag};
    if (to_hand) {
        if (WeaponState* ws = w.try_get<WeaponState>(e))
            if (at == l->current) *ws = WeaponState{};   // the gun it replaced has left the hand
        select_weapon(w, e, at, bus);
    }
    if (bus) {
        GameplayEvent ev; ev.name = "weapon.equipped"; ev.target = e; ev.param = id; ev.value = static_cast<float>(at);
        bus->publish(ev);
    }
    return true;
}

/// A gun found or given. Carried when a slot is free; with every slot full it
/// goes into the bag, with a magazine of its rounds. One already carried or in
/// the bag gives a magazine of its rounds instead (when `ammo_if_had`).
enum class GunReceived { None, Carried, Bagged, Ammo };
inline GunReceived receive_weapon(World& w, Entity e, const std::string& id, GameplayEventBus* bus = nullptr,
                                  bool ammo_if_had = true) {
    const WeaponDef* d = find_weapon(id);
    if (!d) return GunReceived::None;
    const WeaponLoadout* l = ensure_loadout(w, e);
    const bool carried = l && loadout_index(*l, id) >= 0;
    Inventory& inv = loadout_detail::bag_of(w, e);
    const ItemInstance rounds{d->ammo_item, std::max(1, d->mag_size), 0, {}};
    if (carried || inventory_count(inv, id) > 0) {
        if (!ammo_if_had || d->ammo_item.empty()) return GunReceived::None;
        inventory_add(inv, rounds);
        return GunReceived::Ammo;
    }
    if (give_weapon(w, e, id, bus)) return GunReceived::Carried;
    Inventory& bag = loadout_detail::bag_of(w, e);
    Inventory trial = bag;
    if (inventory_add(trial, ItemInstance{id, 1, 0, {}}) > 0) return GunReceived::None;
    if (!d->ammo_item.empty()) inventory_add(trial, rounds);
    bag = std::move(trial);
    if (bus) { GameplayEvent ev; ev.name = "weapon.bagged"; ev.target = e; ev.param = id; bus->publish(ev); }
    return GunReceived::Bagged;
}

/// How many guns can be carried (0: any number). Guns beyond the new number
/// go into the bag, the last ones first (never the one in hand), and those
/// left beyond the slots move into empty ones.
inline void set_weapon_slots(World& w, Entity e, int count) {
    count = std::max(0, count);
    WeaponLoadout* l = ensure_loadout(w, e);
    if (!l) {
        w.add<WeaponLoadout>(e, WeaponLoadout{});
        l = &w.get<WeaponLoadout>(e);
        l->current = -1;
    }
    l->max_slots = count;
    if (count > 0) {
        loadout_detail::settle_hand(w, e, *l);
        Inventory& inv = loadout_detail::bag_of(w, e);
        for (int i = static_cast<int>(l->slots.size()) - 1; i >= 0 && loadout_count(*l) > count; --i)
            if (i != l->current) loadout_detail::into_bag(*l, i, inv);
        for (int i = count; i < static_cast<int>(l->slots.size()); ++i) {
            if (!slot_filled(*l, i)) continue;
            for (int j = 0; j < count; ++j)
                if (!slot_filled(*l, j)) {
                    l->slots[static_cast<size_t>(j)] = l->slots[static_cast<size_t>(i)];
                    l->slots[static_cast<size_t>(i)] = LoadoutSlot{};
                    if (l->current == i) l->current = j;
                    if (l->last == i) l->last = j;
                    break;
                }
        }
        while (static_cast<int>(l->slots.size()) > count && l->slots.back().weapon_id.empty()) l->slots.pop_back();
    }
    ensure_loadout(w, e);
}

// ---- the component ------------------------------------------------------------------------
inline void serialize_loadout(const void* obj, std::vector<uint8_t>& out) {
    const auto& l = *static_cast<const WeaponLoadout*>(obj);
    detail::put_i32(out, 2);
    detail::put_i32(out, static_cast<int32_t>(l.slots.size()));
    for (const auto& s : l.slots) { detail::put_str(out, s.weapon_id); detail::put_i32(out, s.ammo_in_mag); }
    detail::put_i32(out, l.current);
    detail::put_i32(out, l.last);
    detail::put_i32(out, l.max_slots);
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
    int32_t cur = 0, last = -1, max_slots = 0;
    if (detail::get_i32(p, e, cur)) l.current = cur;
    if (detail::get_i32(p, e, last)) l.last = last;
    if (version >= 2 && detail::get_i32(p, e, max_slots)) l.max_slots = std::max(0, max_slots);
}

// Using a gun in the bag (Use Item) puts it into a weapon slot.
inline const bool kWeaponItemUse = [] {
    weapon_item_use() = [](World& w, Entity e, const std::string& id, GameplayEventBus* bus) {
        return equip_weapon_from_inventory(w, e, id, -1, bus);
    };
    return true;
}();

inline void register_loadout_components() {
    (void)kWeaponItemUse;
    register_authorable(make_authorable_custom<WeaponLoadout>("Weapon Loadout", &serialize_loadout, &deserialize_loadout));
}

}  // namespace schizo::ecs
