#pragma once

// ============================================================================
// G5 · Economy — currency wallet, vendors (buy/sell), transfer, harvest nodes.
// ============================================================================
//
// Currency is a named attribute (default "Gold") on the G0 AttributeSet, so a
// wallet composes with the rest of the stat system (a game may instead use an
// ItemKind::Currency stack). A Vendor lists items with per-unit prices; players
// buy (pay currency, receive item) and sell (give item, receive value*ratio).
// transfer_item moves stacks between inventories (the basis for containers/trade).
// A HarvestNode yields items on harvest, then respawns after a cooldown.

#include "world.h"
#include "gameplay_attributes.h"
#include "gameplay_items.h"
#include "gameplay_inventory.h"
#include "gameplay_events.h"
#include "authorable_components.h"
#include "component_serialize.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace schizo::ecs {

// ---------------- currency wallet (an attribute) ----------------
inline float wallet_balance(World& w, Entity e, const std::string& cur = "Gold") {
    const AttributeSet* a = w.try_get<AttributeSet>(e);
    return a ? a->get(cur) : 0.0f;
}
inline void wallet_add(World& w, Entity e, float amount, const std::string& cur = "Gold") {
    if (!w.has<AttributeSet>(e)) w.add<AttributeSet>(e, AttributeSet{});
    AttributeSet& a = w.get<AttributeSet>(e);
    if (!a.find(cur)) a.define(cur, 0.0f, 0.0f, 1e9f);
    a.adjust(cur, amount);
}
inline bool wallet_spend(World& w, Entity e, float amount, const std::string& cur = "Gold") {
    AttributeSet* a = w.try_get<AttributeSet>(e);
    if (!a || a->get(cur) < amount) return false;
    a->adjust(cur, -amount);
    return true;
}

// ---------------- vendor / shop ----------------
struct VendorEntry { std::string item_id; float price = 1.0f; int stock = -1; };  // stock < 0 = unlimited
struct Vendor {
    std::vector<VendorEntry> entries;
    float       sell_ratio = 0.5f;   // fraction of an item's value the vendor pays when buying FROM the player
    std::string currency   = "Gold";
};

// Buyer buys `qty` of item from the vendor: pay currency, decrement stock, add item.
inline bool vendor_buy(World& w, Entity vendor, Entity buyer, const std::string& item_id,
                       int qty = 1, GameplayEventBus* bus = nullptr) {
    Vendor* v = w.try_get<Vendor>(vendor);
    if (!v || qty <= 0) return false;
    VendorEntry* ve = nullptr;
    for (auto& en : v->entries) if (en.item_id == item_id) { ve = &en; break; }
    if (!ve) return false;
    if (ve->stock >= 0 && ve->stock < qty) return false;
    const float cost = ve->price * static_cast<float>(qty);
    if (!wallet_spend(w, buyer, cost, v->currency)) return false;
    if (!w.has<Inventory>(buyer)) w.add<Inventory>(buyer, Inventory{});
    inventory_add(w.get<Inventory>(buyer), ItemInstance{item_id, qty, 0, {}});
    if (ve->stock >= 0) ve->stock -= qty;
    if (bus) { GameplayEvent ev; ev.name = "vendor.buy"; ev.instigator = buyer; ev.target = vendor;
               ev.value = cost; ev.param = item_id; bus->publish(ev); }
    return true;
}

// Seller sells `qty` of item TO the vendor: remove from inventory, pay value*ratio.
// Payout uses the item's intrinsic ItemDef::value (0 if unknown).
inline bool vendor_sell(World& w, Entity vendor, Entity seller, const std::string& item_id,
                        int qty = 1, GameplayEventBus* bus = nullptr) {
    Vendor* v = w.try_get<Vendor>(vendor);
    Inventory* inv = w.try_get<Inventory>(seller);
    if (!v || !inv || qty <= 0 || inventory_count(*inv, item_id) < qty) return false;
    const ItemDef* d = find_item(item_id);
    const float unit = (d ? d->value : 0.0f) * v->sell_ratio;
    const float payout = unit * static_cast<float>(qty);
    inventory_remove(*inv, item_id, qty);
    wallet_add(w, seller, payout, v->currency);
    if (bus) { GameplayEvent ev; ev.name = "vendor.sell"; ev.instigator = seller; ev.target = vendor;
               ev.value = payout; ev.param = item_id; bus->publish(ev); }
    return true;
}

// ---------------- item transfer (containers / trade basis) ----------------
// Move up to `qty` of item from one inventory to another. Returns how many moved.
inline int transfer_item(World& w, Entity from, Entity to, const std::string& item_id, int qty) {
    Inventory* fi = w.try_get<Inventory>(from);
    if (!fi || qty <= 0) return 0;
    const int move = std::min(qty, inventory_count(*fi, item_id));
    if (move <= 0) return 0;
    if (!w.has<Inventory>(to)) w.add<Inventory>(to, Inventory{});
    const int leftover = inventory_add(w.get<Inventory>(to), ItemInstance{item_id, move, 0, {}});
    const int moved = move - leftover;
    inventory_remove(*fi, item_id, moved);
    return moved;
}

// ---------------- harvest / gathering node ----------------
struct HarvestNode {
    std::string item_id;
    int   min_qty = 1, max_qty = 1;
    float respawn = 5.0f;    // seconds until it can be harvested again
    float cooldown = 0.0f;   // runtime: >0 = depleted
};

// Harvest into the harvester's inventory (once per respawn cycle). `seed` makes the
// yield deterministic for tests; a game can vary it per harvest.
inline bool harvest(World& w, Entity node, Entity harvester, GameplayEventBus* bus = nullptr, uint64_t seed = 1) {
    HarvestNode* n = w.try_get<HarvestNode>(node);
    if (!n || n->cooldown > 0.0f) return false;
    Rng rng(seed);
    const int qty = rng.range_i(n->min_qty, n->max_qty);
    if (!w.has<Inventory>(harvester)) w.add<Inventory>(harvester, Inventory{});
    inventory_add(w.get<Inventory>(harvester), ItemInstance{n->item_id, qty, 0, {}});
    n->cooldown = n->respawn;
    if (bus) { GameplayEvent ev; ev.name = "harvest.done"; ev.instigator = harvester; ev.target = node;
               ev.value = static_cast<float>(qty); ev.param = n->item_id; bus->publish(ev); }
    return true;
}
inline void tick_harvest(World& w, float dt) {
    w.each<HarvestNode>([&](Entity, HarvestNode& n) {
        if (n.cooldown > 0.0f) n.cooldown = std::max(0.0f, n.cooldown - dt);
    });
}

// ---------------- custom serialization (authorable components) ----------------
inline void serialize_vendor(const void* obj, std::vector<uint8_t>& out) {
    const auto& v = *static_cast<const Vendor*>(obj);
    uint32_t n = static_cast<uint32_t>(v.entries.size()); detail::put_bytes(out, &n, 4);
    for (const auto& en : v.entries) { detail::put_str(out, en.item_id); detail::put_f32(out, en.price); detail::put_i32(out, en.stock); }
    detail::put_f32(out, v.sell_ratio);
    detail::put_str(out, v.currency);
}
inline void deserialize_vendor(void* obj, const uint8_t* data, size_t n) {
    auto& v = *static_cast<Vendor*>(obj); v.entries.clear();
    const uint8_t* p = data; const uint8_t* e = data + n;
    uint32_t ne = 0; if (!detail::get_bytes(p, e, &ne, 4)) return;
    for (uint32_t i = 0; i < ne; ++i) {
        VendorEntry en;
        if (!detail::get_str(p, e, en.item_id)) return;
        if (!detail::get_f32(p, e, en.price))   return;
        if (!detail::get_i32(p, e, en.stock))   return;
        v.entries.push_back(std::move(en));
    }
    detail::get_f32(p, e, v.sell_ratio);
    detail::get_str(p, e, v.currency);
}
inline void serialize_harvest(const void* obj, std::vector<uint8_t>& out) {
    const auto& h = *static_cast<const HarvestNode*>(obj);
    detail::put_str(out, h.item_id);
    detail::put_i32(out, h.min_qty);
    detail::put_i32(out, h.max_qty);
    detail::put_f32(out, h.respawn);
}
inline void deserialize_harvest(void* obj, const uint8_t* data, size_t n) {
    auto& h = *static_cast<HarvestNode*>(obj);
    const uint8_t* p = data; const uint8_t* e = data + n;
    if (!detail::get_str(p, e, h.item_id)) return;
    detail::get_i32(p, e, h.min_qty);
    detail::get_i32(p, e, h.max_qty);
    detail::get_f32(p, e, h.respawn);
    h.cooldown = 0.0f;
}

inline void register_economy_components() {
    register_authorable(make_authorable_custom<Vendor>("Vendor", &serialize_vendor, &deserialize_vendor));
    register_authorable(make_authorable_custom<HarvestNode>("Harvest Node", &serialize_harvest, &deserialize_harvest));
}

}  // namespace schizo::ecs
