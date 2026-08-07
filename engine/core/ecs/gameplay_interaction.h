#pragma once

// ============================================================================
// G7 · World interaction — interactables, pickups, containers.
// ============================================================================
//
// An Interactable marks an entity the player can use (a lever, NPC, chest, item
// on the ground) with a prompt + the event it fires when used and a proximity
// range. A Pickup grants an item to the interactor. Containers are just an entity
// with an Inventory + an Interactable (open it, then transfer_item between the
// player and the container). Everything is proximity + events over G0/G4/G5 — no
// genre-specific interaction baked in.

#include "world.h"
#include "components.h"           // Transform
#include "gameplay_items.h"
#include "gameplay_inventory.h"
#include "gameplay_events.h"
#include "authorable_components.h"
#include "component_serialize.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace schizo::ecs {

struct Interactable {
    std::string prompt = "Interact";     // "Open", "Talk", "Pick up", …
    std::string event  = "interact";     // fired on the bus when used
    float range   = 2.5f;
    bool  enabled = true;
    bool  consume_on_use = false;        // disable after one use (e.g. ground pickups)
};
struct Pickup { std::string item_id; int quantity = 1; };

// The nearest ENABLED interactable within its own range of `from` (or null_entity).
inline Entity nearest_interactable(World& w, const glm::vec3& from) {
    Entity best = null_entity;
    float  best_d2 = 0.0f;
    w.each<Interactable, Transform>([&](Entity e, Interactable& it, Transform& t) {
        if (!it.enabled) return;
        const glm::vec3 d = t.position - from;
        const float d2 = glm::dot(d, d);
        if (d2 <= it.range * it.range && (best == null_entity || d2 < best_d2)) { best = e; best_d2 = d2; }
    });
    return best;
}

// Use `target`: fire its event, grant its Pickup (if any) to the interactor, and
// disable it if consume_on_use. Returns false if it isn't an enabled interactable.
inline bool interact(World& w, Entity interactor, Entity target, GameplayEventBus* bus = nullptr) {
    Interactable* it = w.try_get<Interactable>(target);
    if (!it || !it->enabled) return false;
    if (bus) { GameplayEvent ev; ev.name = it->event; ev.instigator = interactor; ev.target = target;
               ev.param = it->prompt; bus->publish(ev); }
    if (Pickup* p = w.try_get<Pickup>(target)) {
        if (!w.has<Inventory>(interactor)) w.add<Inventory>(interactor, Inventory{});
        inventory_add(w.get<Inventory>(interactor), ItemInstance{p->item_id, p->quantity, 0, {}});
        if (bus) { GameplayEvent ev; ev.name = "pickup"; ev.instigator = interactor; ev.target = target;
                   ev.value = static_cast<float>(p->quantity); ev.param = p->item_id; bus->publish(ev); }
    }
    if (it->consume_on_use) it->enabled = false;
    return true;
}

// Interact with the nearest interactable in range of the interactor's Transform.
inline bool try_interact(World& w, Entity interactor, GameplayEventBus* bus = nullptr) {
    Transform* t = w.try_get<Transform>(interactor);
    if (!t) return false;
    const Entity tgt = nearest_interactable(w, t->position);
    return tgt != null_entity && interact(w, interactor, tgt, bus);
}

// ---------------- custom serialization ----------------
inline void serialize_interactable(const void* obj, std::vector<uint8_t>& out) {
    const auto& it = *static_cast<const Interactable*>(obj);
    detail::put_str(out, it.prompt);
    detail::put_str(out, it.event);
    detail::put_f32(out, it.range);
    const uint8_t en = it.enabled ? 1 : 0;        detail::put_bytes(out, &en, 1);
    const uint8_t co = it.consume_on_use ? 1 : 0; detail::put_bytes(out, &co, 1);
}
inline void deserialize_interactable(void* obj, const uint8_t* data, size_t n) {
    auto& it = *static_cast<Interactable*>(obj);
    const uint8_t* p = data; const uint8_t* e = data + n;
    if (!detail::get_str(p, e, it.prompt)) return;
    if (!detail::get_str(p, e, it.event))  return;
    detail::get_f32(p, e, it.range);
    uint8_t en = 1, co = 0;
    if (detail::get_bytes(p, e, &en, 1)) it.enabled = en != 0;
    if (detail::get_bytes(p, e, &co, 1)) it.consume_on_use = co != 0;
}
inline void serialize_pickup(const void* obj, std::vector<uint8_t>& out) {
    const auto& p = *static_cast<const Pickup*>(obj);
    detail::put_str(out, p.item_id);
    detail::put_i32(out, p.quantity);
}
inline void deserialize_pickup(void* obj, const uint8_t* data, size_t n) {
    auto& pk = *static_cast<Pickup*>(obj);
    const uint8_t* p = data; const uint8_t* e = data + n;
    if (!detail::get_str(p, e, pk.item_id)) return;
    detail::get_i32(p, e, pk.quantity);
}

inline void register_interaction_components() {
    register_authorable(make_authorable_custom<Interactable>("Interactable", &serialize_interactable, &deserialize_interactable));
    register_authorable(make_authorable_custom<Pickup>("Pickup", &serialize_pickup, &deserialize_pickup));
}

}  // namespace schizo::ecs
