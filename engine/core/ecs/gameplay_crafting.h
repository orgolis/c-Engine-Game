#pragma once

// ============================================================================
// G5 · Crafting recipes + salvage — data-driven, over G4 inventory.
// ============================================================================
//
// A RecipeDef turns input item stacks into output stacks, optionally gated by a
// station tag (the crafter must have e.g. "station.forge") and/or a skill tag.
// Recipes live in a global registry (like the item catalog); craft() consumes the
// inputs from an entity's Inventory and adds the outputs. Salvage is the inverse:
// break one item into materials. Everything composes with G4 items + G0 tags.

#include "world.h"
#include "gameplay_items.h"       // ItemInstance, find_item
#include "gameplay_inventory.h"   // Inventory, inventory_add/remove/count
#include "gameplay_tags.h"
#include "gameplay_events.h"

#include <string>
#include <vector>

namespace schizo::ecs {

struct ItemStack { std::string item_id; int quantity = 1; };

struct RecipeDef {
    std::string id;
    std::string name;
    std::vector<ItemStack> inputs;
    std::vector<ItemStack> outputs;
    std::string station_tag;      // "" = craftable anywhere; else the crafter must have this tag
    std::string require_tag;      // optional skill/unlock tag (e.g. "skill.smithing")
};

inline std::vector<RecipeDef>& recipe_registry() { static std::vector<RecipeDef> r; return r; }
inline void register_recipe(const RecipeDef& d) {
    for (auto& e : recipe_registry()) if (e.id == d.id) { e = d; return; }
    recipe_registry().push_back(d);
}
inline const RecipeDef* find_recipe(const std::string& id) {
    for (const auto& e : recipe_registry()) if (e.id == id) return &e;
    return nullptr;
}

// Does the entity have all inputs + satisfy the station/skill tag gates?
inline bool can_craft(World& w, Entity e, const RecipeDef& r) {
    const Inventory* inv = w.try_get<Inventory>(e);
    if (!inv) return false;
    if (!r.station_tag.empty() || !r.require_tag.empty()) {
        const GameplayTags* tags = w.try_get<GameplayTags>(e);
        if (!r.station_tag.empty() && (!tags || !tags->has(r.station_tag))) return false;
        if (!r.require_tag.empty() && (!tags || !tags->has(r.require_tag))) return false;
    }
    for (const auto& in : r.inputs)
        if (inventory_count(*inv, in.item_id) < in.quantity) return false;
    return true;
}

// Consume inputs + produce outputs on the entity's inventory. Returns false if
// not craftable. (Assumes the outputs fit; leftover that doesn't fit is dropped.)
inline bool craft(World& w, Entity e, const std::string& recipe_id, GameplayEventBus* bus = nullptr) {
    const RecipeDef* r = find_recipe(recipe_id);
    if (!r || !can_craft(w, e, *r)) return false;
    Inventory& inv = w.get<Inventory>(e);
    for (const auto& in : r->inputs)  inventory_remove(inv, in.item_id, in.quantity);
    for (const auto& out : r->outputs) inventory_add(inv, ItemInstance{out.item_id, out.quantity, 0, {}});
    if (bus) { GameplayEvent ev; ev.name = "craft.done"; ev.target = e; ev.param = recipe_id; bus->publish(ev); }
    return true;
}

// ---- salvage: break one item into materials ----
struct SalvageDef { std::string item_id; std::vector<ItemStack> yields; };
inline std::vector<SalvageDef>& salvage_registry() { static std::vector<SalvageDef> r; return r; }
inline void register_salvage(const SalvageDef& d) {
    for (auto& e : salvage_registry()) if (e.item_id == d.item_id) { e = d; return; }
    salvage_registry().push_back(d);
}
inline const SalvageDef* find_salvage(const std::string& item_id) {
    for (const auto& e : salvage_registry()) if (e.item_id == item_id) return &e;
    return nullptr;
}
inline bool salvage(World& w, Entity e, const std::string& item_id, GameplayEventBus* bus = nullptr) {
    Inventory* inv = w.try_get<Inventory>(e);
    const SalvageDef* s = find_salvage(item_id);
    if (!inv || !s || inventory_count(*inv, item_id) < 1) return false;
    inventory_remove(*inv, item_id, 1);
    for (const auto& y : s->yields) inventory_add(*inv, ItemInstance{y.item_id, y.quantity, 0, {}});
    if (bus) { GameplayEvent ev; ev.name = "salvage.done"; ev.target = e; ev.param = item_id; bus->publish(ev); }
    return true;
}

}  // namespace schizo::ecs
