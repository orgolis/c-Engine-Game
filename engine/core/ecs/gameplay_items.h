#pragma once

// ============================================================================
// G4 · Item definitions, instances, procedural rolls, loot tables.
// ============================================================================
//
// Items are DATA. A game registers ItemDefs (id, kind, rarity, stack, weight,
// equip slot, equipped modifiers, granted tags, consumable on-use effects, and a
// rollable affix pool) in a global registry — no engine changes for new items. An
// ItemInstance is a concrete stack with procedurally rolled affixes. DropTables
// resolve into instances via a deterministic, seedable RNG (repeatable + net-safe).
//
// Everything composes with G0: equipped modifiers/tags feed the AttributeSet/Tags
// (see gameplay_inventory.h), and consumable on-use effects are GameplayEffects.

#include "gameplay_effects.h"   // GameplayEffect (consumable on-use)

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace schizo::ecs {

enum class ItemKind : uint32_t { Misc = 0, Weapon = 1, Armor = 2, Consumable = 3, Material = 4, Currency = 5 };

struct ItemModifier { std::string attribute; float amount = 0.0f; };   // applied (Add) while equipped
struct ItemAffix {
    std::string name;
    std::string attribute;
    float min_roll = 0.0f, max_roll = 0.0f;
};

struct ItemDef {
    std::string id;                          // stable key
    std::string name;
    ItemKind    kind = ItemKind::Misc;
    int         rarity = 0;                  // 0 common … N (game-defined tiers)
    int         max_stack = 1;
    float       weight = 0.0f;
    std::string equip_slot;                  // "" if not equippable ("weapon","head",…)
    std::vector<ItemModifier>   modifiers;   // base equipped bonuses
    std::vector<std::string>    granted_tags;// granted while equipped
    std::vector<GameplayEffect> on_use;      // consumable use effects
    std::vector<ItemAffix>      affix_pool;  // rollable affixes (procedural gen)
    std::string set_id;                      // "" = not part of a set (set bonuses)
};

// ---- global item catalog (seeded by the game/scripts) ----
inline std::vector<ItemDef>& item_registry() { static std::vector<ItemDef> r; return r; }
inline void register_item(const ItemDef& d) {
    for (auto& e : item_registry()) if (e.id == d.id) { e = d; return; }
    item_registry().push_back(d);
}
inline const ItemDef* find_item(const std::string& id) {
    for (const auto& e : item_registry()) if (e.id == id) return &e;
    return nullptr;
}

// ---- concrete item ----
struct RolledAffix { std::string attribute; float amount = 0.0f; std::string name; };
struct ItemInstance {
    std::string def_id;
    int         quantity = 1;
    int         rarity = 0;                   // may exceed the def's via rolls
    std::vector<RolledAffix> affixes;
};

// Deterministic, seedable RNG (xorshift64) — repeatable rolls + network-safe.
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed = 0x2545F4914F6CDD1DULL) : s(seed ? seed : 1) {}
    uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    float unit()                 { return static_cast<float>(next() >> 40) * (1.0f / 16777216.0f); } // [0,1)
    float range(float a, float b){ return a + (b - a) * unit(); }
    int   range_i(int a, int b)  { return (b < a) ? a : a + static_cast<int>(unit() * (b - a + 1)); } // [a,b]
};

// Roll a concrete item from a def, picking `num_affixes` from its affix pool.
inline ItemInstance roll_item(const std::string& def_id, int num_affixes, Rng& rng) {
    ItemInstance it; it.def_id = def_id;
    const ItemDef* d = find_item(def_id);
    if (!d) return it;
    it.rarity = d->rarity;
    if (num_affixes > 0 && !d->affix_pool.empty()) {
        for (int i = 0; i < num_affixes; ++i) {
            const ItemAffix& af = d->affix_pool[rng.range_i(0, static_cast<int>(d->affix_pool.size()) - 1)];
            it.affixes.push_back({af.attribute, rng.range(af.min_roll, af.max_roll), af.name});
        }
        it.rarity = d->rarity + 1;            // rolled items bump a tier (simple model)
    }
    return it;
}

// The effective modifiers of an instance = base def modifiers + rolled affixes.
inline std::vector<ItemModifier> item_modifiers(const ItemInstance& it) {
    std::vector<ItemModifier> m;
    if (const ItemDef* d = find_item(it.def_id)) m = d->modifiers;
    for (const auto& a : it.affixes) m.push_back({a.attribute, a.amount});
    return m;
}
inline float item_weight(const ItemInstance& it) {
    const ItemDef* d = find_item(it.def_id);
    return d ? d->weight * static_cast<float>(it.quantity) : 0.0f;
}

// ---- loot / drop tables ----
struct DropEntry {
    std::string item_id;
    float weight = 1.0f;      // relative selection weight
    float chance = 1.0f;      // 0..1 chance the selected entry actually drops
    int   min_qty = 1, max_qty = 1;
    int   affixes = 0;        // >0 = procedurally roll this many affixes
};
struct DropTable { std::vector<DropEntry> entries; int rolls = 1; };

inline std::vector<ItemInstance> roll_loot(const DropTable& table, Rng& rng) {
    std::vector<ItemInstance> out;
    float total = 0.0f;
    for (const auto& e : table.entries) total += e.weight;
    if (total <= 0.0f) return out;
    for (int r = 0; r < table.rolls; ++r) {
        float pick = rng.range(0.0f, total);
        const DropEntry* chosen = nullptr;
        for (const auto& e : table.entries) { if (pick < e.weight) { chosen = &e; break; } pick -= e.weight; }
        if (!chosen) chosen = &table.entries.back();
        if (rng.unit() > chosen->chance) continue;   // failed the drop chance
        ItemInstance it = (chosen->affixes > 0) ? roll_item(chosen->item_id, chosen->affixes, rng)
                                                : ItemInstance{chosen->item_id, 1,
                                                    (find_item(chosen->item_id) ? find_item(chosen->item_id)->rarity : 0), {}};
        it.quantity = rng.range_i(chosen->min_qty, chosen->max_qty);
        out.push_back(std::move(it));
    }
    return out;
}

}  // namespace schizo::ecs
