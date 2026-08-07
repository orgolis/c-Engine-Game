#pragma once

// ============================================================================
// G14 · Building & automation — placement, extractors, machines, logistics, power.
// ============================================================================
//
// A factory loop over the G4 Inventory (each building's item buffer): Extractors
// mine an item over time; Conveyors move items between building buffers; Machines
// run a recipe (consume inputs -> progress -> outputs) while the factory is
// powered (Generators vs machine draw). BuildableDefs carry a build cost paid from
// the builder's inventory. All deterministic + testable; meshes/grid-snapping are
// the integration layer.

#include "world.h"
#include "components.h"           // Transform
#include "gameplay_events.h"
#include "gameplay_items.h"
#include "gameplay_inventory.h"   // Inventory as the building buffer + inventory_* ops
#include "gameplay_economy.h"     // transfer_item (conveyors)
#include "gameplay_crafting.h"    // ItemStack
#include "authorable_components.h"
#include "component_serialize.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace schizo::ecs {

// ---------------- buildable definitions ----------------
struct BuildableDef { std::string id, name; std::vector<ItemStack> cost; };
inline std::vector<BuildableDef>& buildable_registry() { static std::vector<BuildableDef> r; return r; }
inline void register_buildable(const BuildableDef& d) {
    for (auto& e : buildable_registry()) if (e.id == d.id) { e = d; return; }
    buildable_registry().push_back(d);
}
inline const BuildableDef* find_buildable(const std::string& id) {
    for (const auto& e : buildable_registry()) if (e.id == id) return &e;
    return nullptr;
}

// ---------------- machine recipes (timed) ----------------
struct MachineRecipe { std::string id; std::vector<ItemStack> inputs, outputs; float time = 2.0f; };
inline std::vector<MachineRecipe>& machine_recipe_registry() { static std::vector<MachineRecipe> r; return r; }
inline void register_machine_recipe(const MachineRecipe& d) {
    for (auto& e : machine_recipe_registry()) if (e.id == d.id) { e = d; return; }
    machine_recipe_registry().push_back(d);
}
inline const MachineRecipe* find_machine_recipe(const std::string& id) {
    for (const auto& e : machine_recipe_registry()) if (e.id == id) return &e;
    return nullptr;
}

// ---------------- building components ----------------
struct Building  { std::string def_id; };                                   // what def placed it
struct Extractor { std::string item_id; float rate = 1.0f; float accum = 0.0f; };   // items/sec into its Inventory
struct Machine   { std::string recipe_id; float progress = 0.0f; bool crafting = false; int power_use = 10; };
struct Generator { int power = 20; };
struct Conveyor  { uint32_t from = 0xFFFFFFFFu, to = 0xFFFFFFFFu; std::string item_id; float rate = 2.0f; float accum = 0.0f; };

// ---------------- placement ----------------
inline bool can_build(World& w, Entity builder, const std::string& def_id) {
    const BuildableDef* d = find_buildable(def_id);
    const Inventory* inv = w.try_get<Inventory>(builder);
    if (!d) return false;
    for (const auto& c : d->cost) if (!inv || inventory_count(*inv, c.item_id) < c.quantity) return false;
    return true;
}
// Pay the cost from the builder + create a base building (Transform + Inventory
// buffer + Building marker). The caller adds the Extractor/Machine/Generator.
inline Entity place_building(World& w, Entity builder, const std::string& def_id, const glm::vec3& pos) {
    if (!can_build(w, builder, def_id)) return null_entity;
    const BuildableDef* d = find_buildable(def_id);
    if (Inventory* inv = w.try_get<Inventory>(builder))
        for (const auto& c : d->cost) inventory_remove(*inv, c.item_id, c.quantity);
    const Entity e = w.create();
    Transform t; t.position = pos; w.add<Transform>(e, t);
    w.add<Inventory>(e, Inventory{});
    w.add<Building>(e, Building{def_id});
    return e;
}

// ---------------- power ----------------
inline int factory_power(World& w) {
    int gen = 0, use = 0;
    w.each<Generator>([&](Entity, Generator& g) { gen += g.power; });
    w.each<Machine>([&](Entity, Machine& m) { use += m.power_use; });
    return gen - use;
}
inline bool factory_powered(World& w) { return factory_power(w) >= 0; }

// ---------------- the factory tick ----------------
inline void tick_factory(World& w, float dt, GameplayEventBus* bus = nullptr) {
    const bool powered = factory_powered(w);

    // extractors: mine into their own buffer
    w.each<Extractor, Inventory>([&](Entity, Extractor& ex, Inventory& inv) {
        ex.accum += ex.rate * dt;
        while (ex.accum >= 1.0f) { ex.accum -= 1.0f; inventory_add(inv, ItemInstance{ex.item_id, 1, 0, {}}); }
    });

    // machines: consume inputs -> progress -> outputs (only while powered)
    if (powered)
        w.each<Machine, Inventory>([&](Entity e, Machine& m, Inventory& inv) {
            const MachineRecipe* r = find_machine_recipe(m.recipe_id);
            if (!r) return;
            if (!m.crafting) {
                bool ok = true;
                for (const auto& in : r->inputs) if (inventory_count(inv, in.item_id) < in.quantity) { ok = false; break; }
                if (ok) { for (const auto& in : r->inputs) inventory_remove(inv, in.item_id, in.quantity); m.crafting = true; m.progress = 0.0f; }
            }
            if (m.crafting) {
                m.progress += dt;
                if (m.progress >= r->time) {
                    for (const auto& out : r->outputs) inventory_add(inv, ItemInstance{out.item_id, out.quantity, 0, {}});
                    m.crafting = false; m.progress = 0.0f;
                    if (bus) { GameplayEvent ev; ev.name = "machine.produce"; ev.target = e; ev.param = m.recipe_id; bus->publish(ev); }
                }
            }
        });

    // conveyors: rate-limited transfer between building buffers
    w.each<Conveyor>([&](Entity, Conveyor& c) {
        c.accum += c.rate * dt;
        const int want = static_cast<int>(c.accum);
        if (want > 0) {
            const int moved = transfer_item(w, static_cast<Entity>(c.from), static_cast<Entity>(c.to), c.item_id, want);
            c.accum -= static_cast<float>(moved);
        }
    });
}

// ---------------- custom serialization ----------------
inline void serialize_building(const void* o, std::vector<uint8_t>& out) { detail::put_str(out, static_cast<const Building*>(o)->def_id); }
inline void deserialize_building(void* o, const uint8_t* d, size_t n) { const uint8_t* p = d; detail::get_str(p, d + n, static_cast<Building*>(o)->def_id); }
inline void serialize_extractor(const void* o, std::vector<uint8_t>& out) {
    const auto& e = *static_cast<const Extractor*>(o); detail::put_str(out, e.item_id); detail::put_f32(out, e.rate);
}
inline void deserialize_extractor(void* o, const uint8_t* d, size_t n) {
    auto& e = *static_cast<Extractor*>(o); const uint8_t* p = d; const uint8_t* end = d + n;
    if (!detail::get_str(p, end, e.item_id)) return; detail::get_f32(p, end, e.rate); e.accum = 0.0f;
}
inline void serialize_machine(const void* o, std::vector<uint8_t>& out) {
    const auto& m = *static_cast<const Machine*>(o); detail::put_str(out, m.recipe_id); detail::put_i32(out, m.power_use);
}
inline void deserialize_machine(void* o, const uint8_t* d, size_t n) {
    auto& m = *static_cast<Machine*>(o); const uint8_t* p = d; const uint8_t* end = d + n;
    if (!detail::get_str(p, end, m.recipe_id)) return; detail::get_i32(p, end, m.power_use); m.progress = 0.0f; m.crafting = false;
}
inline void serialize_generator(const void* o, std::vector<uint8_t>& out) { detail::put_i32(out, static_cast<const Generator*>(o)->power); }
inline void deserialize_generator(void* o, const uint8_t* d, size_t n) { const uint8_t* p = d; detail::get_i32(p, d + n, static_cast<Generator*>(o)->power); }
inline void serialize_conveyor(const void* o, std::vector<uint8_t>& out) {
    const auto& c = *static_cast<const Conveyor*>(o); detail::put_str(out, c.item_id); detail::put_f32(out, c.rate);   // from/to are runtime links
}
inline void deserialize_conveyor(void* o, const uint8_t* d, size_t n) {
    auto& c = *static_cast<Conveyor*>(o); const uint8_t* p = d; const uint8_t* end = d + n;
    if (!detail::get_str(p, end, c.item_id)) return; detail::get_f32(p, end, c.rate); c.accum = 0.0f;
}
inline void register_building_components() {
    register_authorable(make_authorable_custom<Building>("Building", &serialize_building, &deserialize_building));
    register_authorable(make_authorable_custom<Extractor>("Extractor", &serialize_extractor, &deserialize_extractor));
    register_authorable(make_authorable_custom<Machine>("Machine", &serialize_machine, &deserialize_machine));
    register_authorable(make_authorable_custom<Generator>("Generator", &serialize_generator, &deserialize_generator));
    register_authorable(make_authorable_custom<Conveyor>("Conveyor", &serialize_conveyor, &deserialize_conveyor));
}

}  // namespace schizo::ecs
