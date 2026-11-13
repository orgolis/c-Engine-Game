#pragma once

// ============================================================================
// G4 · Inventory + equipment (slots, stacking, weight, set bonuses).
// ============================================================================
//
// Inventory  — a list of ItemInstances with slot + weight limits and stacking.
// Equipment  — one item per named slot; equipping applies the item's modifiers to
//              the entity's AttributeSet (Add) + grants its tags, and set bonuses
//              apply when enough pieces of a set are worn. Everything reverts
//              EXACTLY on unequip (the applied aggregate is tracked), so it rides
//              the G0 attribute/tag stack without a bespoke stat pipeline.
// Consumables — use_item() applies the def's on-use GameplayEffects + decrements.
//
// NOTE: equipment modifiers touch current+max (a removable layer over base). If an
// attribute is ALSO driven by DerivedStats, define a recompute order for that stat.

#include "world.h"
#include "gameplay_attributes.h"
#include "gameplay_tags.h"
#include "gameplay_effects.h"
#include "gameplay_events.h"
#include "gameplay_items.h"
#include "authorable_components.h"
#include "component_serialize.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace schizo::ecs {

// ---------------- Inventory ----------------
struct Inventory {
    std::vector<ItemInstance> items;
    int   max_slots  = 20;      // 0 = unlimited
    float max_weight = 0.0f;    // 0 = unlimited
};

inline float inventory_weight(const Inventory& inv) {
    float w = 0.0f; for (const auto& i : inv.items) w += item_weight(i); return w;
}
inline int inventory_count(const Inventory& inv, const std::string& def_id) {
    int c = 0; for (const auto& i : inv.items) if (i.def_id == def_id) c += i.quantity; return c;
}

// Add an item, stacking onto existing stacks (stackable + no affixes) up to
// max_stack, then into new slots. Returns the quantity that did NOT fit.
inline int inventory_add(Inventory& inv, const ItemInstance& item) {
    const ItemDef* d = find_item(item.def_id);
    const int max_stack = d ? std::max(1, d->max_stack) : 1;
    int remaining = item.quantity;

    if (item.affixes.empty() && max_stack > 1)
        for (auto& s : inv.items) {
            if (remaining <= 0) break;
            if (s.def_id == item.def_id && s.affixes.empty() && s.quantity < max_stack) {
                const int add = std::min(max_stack - s.quantity, remaining);
                s.quantity += add; remaining -= add;
            }
        }

    while (remaining > 0) {
        if (inv.max_slots > 0 && static_cast<int>(inv.items.size()) >= inv.max_slots) break;
        const int put = std::min(remaining, max_stack);
        if (inv.max_weight > 0.0f && d && inventory_weight(inv) + d->weight * put > inv.max_weight) break;
        ItemInstance s = item;
        s.quantity = put;
        inv.items.push_back(std::move(s));
        remaining -= put;
    }
    return remaining;
}

// Remove up to `qty` of a def id across stacks. Returns how many were removed.
inline int inventory_remove(Inventory& inv, const std::string& def_id, int qty) {
    int removed = 0;
    for (size_t i = 0; i < inv.items.size() && removed < qty;) {
        if (inv.items[i].def_id == def_id) {
            const int take = std::min(qty - removed, inv.items[i].quantity);
            inv.items[i].quantity -= take; removed += take;
            if (inv.items[i].quantity <= 0) { inv.items.erase(inv.items.begin() + i); continue; }
        }
        ++i;
    }
    return removed;
}

// Use a consumable at `index`: apply its on-use effects + decrement/remove.
/// Abilities asked for by items (`casts`): the game fires them from the
/// user's view (Play: the player's camera) and empties this.
struct AbilityRequests { std::vector<std::string> ids; };

inline bool equip_from_inventory(World& w, Entity e, const std::string& def_id, GameplayEventBus* bus);

/// How a gun in the bag (an item with a weapon's id) is used: set by
/// gameplay_loadout.h -- it goes into a weapon slot.
using WeaponItemUse = bool (*)(World& w, Entity e, const std::string& weapon_id, GameplayEventBus* bus);
inline WeaponItemUse& weapon_item_use() { static WeaponItemUse f = nullptr; return f; }

inline bool use_item(World& w, Entity e, Inventory& inv, size_t index, GameplayEventBus* bus = nullptr) {
    if (index >= inv.items.size()) return false;
    const ItemDef* d = find_item(inv.items[index].def_id);
    if ((!d || (d->kind == ItemKind::Weapon && d->equip_slot.empty())) && weapon_item_use())
        if (weapon_item_use()(w, e, inv.items[index].def_id, bus)) return true;
    if (d && !d->equip_slot.empty() && d->kind != ItemKind::Consumable)
        return equip_from_inventory(w, e, d->id, bus);   // gear: using it puts it on
    if (!d || d->kind != ItemKind::Consumable) return false;
    for (const auto& fx : d->on_use) apply_effect(w, e, fx);
    if (!d->cast_ability.empty()) {
        if (!w.has<AbilityRequests>(e)) w.add<AbilityRequests>(e, AbilityRequests{});
        w.get<AbilityRequests>(e).ids.push_back(d->cast_ability);
    }
    if (bus) { GameplayEvent ev; ev.name = "item.used"; ev.target = e; ev.param = d->id; bus->publish(ev); }
    if (--inv.items[index].quantity <= 0) inv.items.erase(inv.items.begin() + index);
    return true;
}

// ---------------- Equipment + set bonuses ----------------
struct SetBonus {
    std::string set_id;
    int         required = 2;
    std::vector<ItemModifier> modifiers;
    std::vector<std::string>  tags;
};
inline std::vector<SetBonus>& set_bonus_registry() { static std::vector<SetBonus> r; return r; }
inline void register_set_bonus(const SetBonus& s) { set_bonus_registry().push_back(s); }

struct EquippedItem { std::string slot; ItemInstance item; };
struct Equipment {
    std::vector<EquippedItem> slots;
    // runtime bookkeeping (persisted so unequip reverts exactly): the aggregate
    // currently applied to the AttributeSet / Tags.
    std::vector<ItemModifier> applied;
    std::vector<std::string>  applied_tags;
};

inline const ItemInstance* equipped_in(const Equipment& eq, const std::string& slot) {
    for (const auto& s : eq.slots) if (s.slot == slot) return &s.item;
    return nullptr;
}

// Revert what's applied, then recompute + apply all equipped modifiers/tags plus
// any met set bonuses. Called on every equip/unequip.
inline void recompute_equipment(World& w, Entity e) {
    Equipment* eq = w.try_get<Equipment>(e);
    if (!eq) return;
    AttributeSet* attrs = w.try_get<AttributeSet>(e);
    GameplayTags* tags  = w.try_get<GameplayTags>(e);

    if (attrs) for (const auto& m : eq->applied)
        if (Attribute* a = attrs->find(m.attribute)) {
            a->max -= m.amount; a->current -= m.amount;
            a->current = std::max(a->min, std::min(a->max, a->current));
        }
    if (tags) for (const auto& t : eq->applied_tags) tags->remove_exact(t);
    eq->applied.clear(); eq->applied_tags.clear();

    std::vector<ItemModifier> agg;
    std::vector<std::string>  agg_tags;
    std::vector<std::pair<std::string, int>> set_counts;
    auto bump_set = [&](const std::string& sid) {
        for (auto& c : set_counts) if (c.first == sid) { ++c.second; return; }
        set_counts.push_back({sid, 1});
    };
    for (const auto& s : eq->slots) {
        for (const auto& m : item_modifiers(s.item)) agg.push_back(m);
        if (const ItemDef* d = find_item(s.item.def_id)) {
            for (const auto& t : d->granted_tags) agg_tags.push_back(t);
            if (!d->set_id.empty()) bump_set(d->set_id);
        }
    }
    for (const auto& c : set_counts)
        for (const auto& sb : set_bonus_registry())
            if (sb.set_id == c.first && c.second >= sb.required) {
                for (const auto& m : sb.modifiers) agg.push_back(m);
                for (const auto& t : sb.tags)      agg_tags.push_back(t);
            }

    if (attrs) for (const auto& m : agg) {
        // An attribute the wearer lacks starts at 0 (armour's resist.<type> on anyone).
        Attribute* a = attrs->find(m.attribute);
        if (!a) a = &attrs->define(m.attribute, 0.0f, 0.0f, 0.0f);
        a->max += m.amount; a->current += m.amount;
        a->current = std::max(a->min, std::min(a->max, a->current));
    }
    if (!agg_tags.empty()) {
        if (!w.has<GameplayTags>(e)) w.add<GameplayTags>(e, GameplayTags{});
        auto& g = w.get<GameplayTags>(e);
        for (const auto& t : agg_tags) g.add(t);
    }
    eq->applied = std::move(agg);
    eq->applied_tags = std::move(agg_tags);
}

// Equip an item into its def's slot (replacing whatever is there).
inline bool equip_item(World& w, Entity e, const ItemInstance& item, GameplayEventBus* bus = nullptr) {
    const ItemDef* d = find_item(item.def_id);
    if (!d || d->equip_slot.empty()) return false;
    if (!w.has<Equipment>(e)) w.add<Equipment>(e, Equipment{});
    Equipment& eq = w.get<Equipment>(e);
    for (size_t i = 0; i < eq.slots.size(); ++i)
        if (eq.slots[i].slot == d->equip_slot) { eq.slots.erase(eq.slots.begin() + i); break; }
    eq.slots.push_back({d->equip_slot, item});
    recompute_equipment(w, e);
    if (bus) { GameplayEvent ev; ev.name = "item.equipped"; ev.target = e; ev.param = d->id; bus->publish(ev); }
    return true;
}
inline bool unequip_slot(World& w, Entity e, const std::string& slot, GameplayEventBus* bus = nullptr) {
    Equipment* eq = w.try_get<Equipment>(e);
    if (!eq) return false;
    for (size_t i = 0; i < eq->slots.size(); ++i)
        if (eq->slots[i].slot == slot) {
            eq->slots.erase(eq->slots.begin() + i);
            recompute_equipment(w, e);
            if (bus) { GameplayEvent ev; ev.name = "item.unequipped"; ev.target = e; ev.param = slot; bus->publish(ev); }
            return true;
        }
    return false;
}

/// Put on an item from the bag: it leaves the bag, and what was in its slot
/// goes back into the bag. False if it is not in the bag or is not gear.
inline bool equip_from_inventory(World& w, Entity e, const std::string& def_id, GameplayEventBus* bus) {
    const ItemDef* d = find_item(def_id);
    Inventory* inv = w.try_get<Inventory>(e);
    if (!d || d->equip_slot.empty() || !inv) return false;
    size_t at = inv->items.size();
    for (size_t i = 0; i < inv->items.size(); ++i)
        if (inv->items[i].def_id == def_id) { at = i; break; }
    if (at == inv->items.size()) return false;
    ItemInstance item = inv->items[at];
    item.quantity = 1;
    if (--inv->items[at].quantity <= 0) inv->items.erase(inv->items.begin() + static_cast<std::ptrdiff_t>(at));
    if (const Equipment* eq = w.try_get<Equipment>(e))
        if (const ItemInstance* old = equipped_in(*eq, d->equip_slot)) {
            const ItemInstance back = *old;
            inventory_add(*inv, back);
        }
    return equip_item(w, e, item, bus);
}

/// Take off what is in a slot, into the bag. False when nothing is there or
/// the bag has no room (it stays on).
inline bool unequip_to_inventory(World& w, Entity e, const std::string& slot, GameplayEventBus* bus = nullptr) {
    const Equipment* eq = w.try_get<Equipment>(e);
    const ItemInstance* on = eq ? equipped_in(*eq, slot) : nullptr;
    if (!on) return false;
    const ItemInstance item = *on;
    if (!w.has<Inventory>(e)) w.add<Inventory>(e, Inventory{});
    Inventory& inv = w.get<Inventory>(e);
    Inventory trial = inv;
    if (inventory_add(trial, item) > 0) return false;
    inv = std::move(trial);
    return unequip_slot(w, e, slot, bus);
}

// ---------------- custom serialization ----------------
inline void put_item_instance(std::vector<uint8_t>& out, const ItemInstance& it) {
    detail::put_str(out, it.def_id);
    detail::put_i32(out, it.quantity);
    detail::put_i32(out, it.rarity);
    uint32_t n = static_cast<uint32_t>(it.affixes.size()); detail::put_bytes(out, &n, 4);
    for (const auto& a : it.affixes) { detail::put_str(out, a.attribute); detail::put_f32(out, a.amount); detail::put_str(out, a.name); }
}
inline bool get_item_instance(const uint8_t*& p, const uint8_t* e, ItemInstance& it) {
    if (!detail::get_str(p, e, it.def_id))  return false;
    if (!detail::get_i32(p, e, it.quantity)) return false;
    if (!detail::get_i32(p, e, it.rarity))   return false;
    uint32_t n = 0; if (!detail::get_bytes(p, e, &n, 4)) return false;
    it.affixes.clear();
    for (uint32_t i = 0; i < n; ++i) {
        RolledAffix a;
        if (!detail::get_str(p, e, a.attribute) || !detail::get_f32(p, e, a.amount) || !detail::get_str(p, e, a.name)) return false;
        it.affixes.push_back(std::move(a));
    }
    return true;
}

inline void serialize_inventory(const void* obj, std::vector<uint8_t>& out) {
    const auto& inv = *static_cast<const Inventory*>(obj);
    detail::put_i32(out, inv.max_slots);
    detail::put_f32(out, inv.max_weight);
    uint32_t n = static_cast<uint32_t>(inv.items.size()); detail::put_bytes(out, &n, 4);
    for (const auto& it : inv.items) put_item_instance(out, it);
}
inline void deserialize_inventory(void* obj, const uint8_t* data, size_t n) {
    auto& inv = *static_cast<Inventory*>(obj); inv.items.clear();
    const uint8_t* p = data; const uint8_t* e = data + n;
    if (!detail::get_i32(p, e, inv.max_slots))  return;
    if (!detail::get_f32(p, e, inv.max_weight)) return;
    uint32_t ni = 0; if (!detail::get_bytes(p, e, &ni, 4)) return;
    for (uint32_t i = 0; i < ni; ++i) { ItemInstance it; if (!get_item_instance(p, e, it)) return; inv.items.push_back(std::move(it)); }
}

inline void serialize_equipment(const void* obj, std::vector<uint8_t>& out) {
    const auto& eq = *static_cast<const Equipment*>(obj);
    uint32_t ns = static_cast<uint32_t>(eq.slots.size()); detail::put_bytes(out, &ns, 4);
    for (const auto& s : eq.slots) { detail::put_str(out, s.slot); put_item_instance(out, s.item); }
    uint32_t na = static_cast<uint32_t>(eq.applied.size()); detail::put_bytes(out, &na, 4);
    for (const auto& m : eq.applied) { detail::put_str(out, m.attribute); detail::put_f32(out, m.amount); }
    detail::put_str_vec(out, eq.applied_tags);
}
inline void deserialize_equipment(void* obj, const uint8_t* data, size_t n) {
    auto& eq = *static_cast<Equipment*>(obj); eq.slots.clear(); eq.applied.clear(); eq.applied_tags.clear();
    const uint8_t* p = data; const uint8_t* e = data + n;
    uint32_t ns = 0; if (!detail::get_bytes(p, e, &ns, 4)) return;
    for (uint32_t i = 0; i < ns; ++i) { EquippedItem s; if (!detail::get_str(p, e, s.slot) || !get_item_instance(p, e, s.item)) return; eq.slots.push_back(std::move(s)); }
    uint32_t na = 0; if (!detail::get_bytes(p, e, &na, 4)) return;
    for (uint32_t i = 0; i < na; ++i) { ItemModifier m; if (!detail::get_str(p, e, m.attribute) || !detail::get_f32(p, e, m.amount)) return; eq.applied.push_back(std::move(m)); }
    detail::get_str_vec(p, e, eq.applied_tags);
}

inline void register_inventory_components() {
    register_authorable(make_authorable_custom<Inventory>("Inventory", &serialize_inventory, &deserialize_inventory));
    register_authorable(make_authorable_custom<Equipment>("Equipment", &serialize_equipment, &deserialize_equipment));
}

}  // namespace schizo::ecs
