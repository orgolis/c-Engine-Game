#include "ecs_bridge.h"
#include "ecs/register_all.h"
#include "ecs/gameplay_tick.h"

#include "ecs/world.h"
#include "ecs/components.h"
#include "ecs/authorable_components.h"   // authorable registry (gameplay persistence)
#include "ecs/component_serialize.h"     // generic reflection-driven (de)serialization
#include "ecs/gameplay_attributes.h"     // G0 data-driven attributes
#include "ecs/gameplay_tags.h"           // G0 data-driven tags
#include "ecs/gameplay_effects.h"        // G0 effects engine (tick)
#include "ecs/gameplay_abilities.h"      // G0 ability cooldown tick
#include "ecs/gameplay_events.h"         // G0 event bus + timer manager
#include "ecs/gameplay_triggers.h"       // G0 trigger-volume overlap system
#include "ecs/gameplay_state_machine.h"  // G1 gameplay state machine
#include "ecs/gameplay_combat.h"         // G2 ECS melee combat (fixed-tick)
#include "ecs/gameplay_progression.h"    // G3 derived stats / regen / leveling
#include "ecs/gameplay_skills.h"         // G3 skill trees
#include "ecs/gameplay_items.h"          // G4 item defs / instances / loot
#include "ecs/gameplay_inventory.h"      // G4 inventory + equipment
#include "ecs/gameplay_item_file.h"      // F5 .items data-file loader
#include "ecs/gameplay_crafting.h"       // G5 recipes + salvage
#include "ecs/gameplay_economy.h"        // G5 vendors / wallet / transfer / harvest
#include "ecs/gameplay_interaction.h"    // G7 interactables / pickups
#include "ecs/gameplay_quests.h"         // G6 quests (event-driven objectives)
#include "ecs/gameplay_npc.h"            // G8 factions / aggro / spawners
#include "ecs/gameplay_savegame.h"       // G9 save/load + world flags
#include "ecs/gameplay_social.h"         // G10 parties / guilds / chat / trade
#include "ecs/gameplay_logic.h"          // scene logic graph (events -> actions)
#include "ecs/gameplay_weapons.h"        // G12 weapons / ammo / projectiles
#include "ecs/gameplay_vehicles.h"       // G13 vehicles / racing
#include "ecs/gameplay_building.h"       // G14 building / automation
#include "ecs/gameplay_survival.h"       // G15 needs / sanity / light / fear
#include "ecs/gameplay_stealth.h"        // G16 vision cones / awareness / hearing
#include "ecs/prefab.h"                  // prefab capture / instantiate (F4)
#include "ecs/parallel.h"
#include "ecs/snapshot.h"
#include "ecs/render_collect.h"
#include "reflection/reflection.h"       // generic field (de)serialization

#include "scene.h"
#include "entity.h"
#include "transform.h"
#include "mesh_renderer_component.h"
#include "mesh_component.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace schizo::editor {

// Shadow-only component: which shadow entity is this one's parent (or
// null_entity for a root). Kept editor-local so it doesn't touch core
// components.h until the migration goes authoritative.
struct ShadowParent {
    ecs::Entity value = ecs::null_entity;
};

// Pimpl: owns the shadow world. Constructed once, cleared+refilled per frame.
struct EcsSceneBridge::Impl {
    ecs::World world;
    // Parallel arrays in creation order. Keeping the OOP Transform* and a
    // resolved parent index alongside each shadow entity lets us build the
    // hierarchy and verify against the OOP world matrix without relying on
    // EnTT's internal storage iteration order.
    // Persistent: OOP Transform* -> its stable shadow entity, kept ACROSS
    // frames. Reconciled each sync (create new / reuse existing / destroy gone)
    // so an entity keeps its identity and components frame to frame.
    std::unordered_map<scene::Transform*, ecs::Entity> tf_to_entity;

    // Per-frame working arrays (active entities this frame, in iteration order).
    std::vector<ecs::Entity>       shadow;
    std::vector<scene::Transform*> oop_tf;
    std::vector<scene::Entity*>    oop_ent;     // parallel to oop_tf (for components)
    std::vector<int>               parent_idx;  // -1 == root
    // Authoritative read path: OOP Transform* -> ECS-computed world matrix,
    // refreshed every sync. Rendering queries this instead of GetWorldMatrix().
    std::unordered_map<scene::Transform*, glm::mat4> result;

    // G0 gameplay services, one per world. Ticked/flushed by tick_gameplay().
    ecs::GameplayEventBus event_bus;
    ecs::TimerManager     timer_mgr;
    ecs::SocialState      social_state;          // G10 parties/guilds/trades/leaderboard
    ecs::LogicGraph       logic_graph;           // scene logic graph (authored by the node panel)
    ecs::LogicRuntime     logic_runtime;         // executes the graph during Play
    ecs::Entity           world_flags_entity = ecs::null_entity;   // holds WorldFlags for Set Flag actions
    float                 combat_accum = 0.0f;   // fixed 60 Hz accumulator for frame-data combat
};

// TRS compose (defined below; forward-declared for draw_benchmark above the def).
static glm::mat4 compose_trs(const ecs::Transform& t);

const glm::mat4* EcsSceneBridge::world_matrix(
    const schizo::scene::Transform* tf) const {
    auto it = impl_->result.find(const_cast<scene::Transform*>(tf));
    return it == impl_->result.end() ? nullptr : &it->second;
}

ecs::World& EcsSceneBridge::world() { return impl_->world; }

void EcsSceneBridge::tick_gameplay(float dt) {
    // Delegates to the shared frame so the headless runner (gws run) ticks
    // exactly what play mode ticks. See engine/core/ecs/gameplay_tick.h.
    ecs::tick_all_gameplay(impl_->world, impl_->event_bus, impl_->timer_mgr,
                           impl_->combat_accum, dt);
}

ecs::GameplayEventBus& EcsSceneBridge::events() { return impl_->event_bus; }
ecs::TimerManager&     EcsSceneBridge::timers() { return impl_->timer_mgr; }
ecs::SocialState&      EcsSceneBridge::social() { return impl_->social_state; }

ecs::LogicGraph& EcsSceneBridge::logic_graph() { return impl_->logic_graph; }

void EcsSceneBridge::start_logic() {
    if (impl_->world_flags_entity == ecs::null_entity) {   // a persistent, sync-safe flags holder
        impl_->world_flags_entity = impl_->world.create();
        impl_->world.add<ecs::WorldFlags>(impl_->world_flags_entity, ecs::WorldFlags{});
    }
    impl_->logic_runtime.start(impl_->logic_graph, impl_->world, impl_->event_bus, impl_->world_flags_entity);
}
int EcsSceneBridge::world_flag(const std::string& key) {
    if (impl_->world_flags_entity == ecs::null_entity ||
        !impl_->world.has<ecs::WorldFlags>(impl_->world_flags_entity))
        return 0;
    return ecs::world_flag(impl_->world.get<ecs::WorldFlags>(impl_->world_flags_entity), key);
}
void EcsSceneBridge::set_world_flag(const std::string& key, int value) {
    if (impl_->world_flags_entity == ecs::null_entity) {   // ensure the persistent holder
        impl_->world_flags_entity = impl_->world.create();
        impl_->world.add<ecs::WorldFlags>(impl_->world_flags_entity, ecs::WorldFlags{});
    } else if (!impl_->world.has<ecs::WorldFlags>(impl_->world_flags_entity)) {
        impl_->world.add<ecs::WorldFlags>(impl_->world_flags_entity, ecs::WorldFlags{});
    }
    ecs::set_world_flag(impl_->world.get<ecs::WorldFlags>(impl_->world_flags_entity), key, value);
}

void EcsSceneBridge::stop_logic()          { impl_->logic_runtime.stop(); }
void EcsSceneBridge::logic_tick(float dt)  { if (impl_->logic_runtime.running()) impl_->logic_runtime.tick(dt); }
void EcsSceneBridge::logic_on_key(int key) { if (impl_->logic_runtime.running()) impl_->logic_runtime.on_key(key); }
void EcsSceneBridge::logic_on_key_up(int key) { if (impl_->logic_runtime.running()) impl_->logic_runtime.on_key_up(key); }

bool EcsSceneBridge::save_logic(const std::string& path) {
    if (impl_->logic_graph.empty()) { std::error_code ec; std::filesystem::remove(path, ec); return true; }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << ecs::logic_to_text(impl_->logic_graph);
    return static_cast<bool>(f);
}
bool EcsSceneBridge::load_logic(const std::string& path) {
    impl_->logic_runtime.stop();
    std::ifstream f(path, std::ios::binary);
    if (!f) { impl_->logic_graph = ecs::LogicGraph{}; return false; }
    std::stringstream ss; ss << f.rdbuf();
    impl_->logic_graph = ecs::logic_from_text(ss.str());
    return true;
}

void EcsSceneBridge::seed_demo_content() {
    static bool seeded = false;
    if (seeded) return;
    seeded = true;
    using namespace ecs;
    { ItemDef d; d.id = "potion_hp"; d.name = "Health Potion"; d.kind = ItemKind::Consumable;
      d.max_stack = 10; d.weight = 0.1f; d.value = 15.0f; d.on_use.push_back(make_instant("Heal", "Health", +40.0f)); register_item(d); }
    { ItemDef d; d.id = "sword_iron"; d.name = "Iron Sword"; d.kind = ItemKind::Weapon;
      d.equip_slot = "weapon"; d.weight = 3.0f; d.value = 60.0f; d.modifiers.push_back({"AttackPower", 12.0f});
      d.affix_pool.push_back({"of Might", "AttackPower", 3.0f, 5.0f}); register_item(d); }
    { ItemDef d; d.id = "helm_set"; d.name = "Guardian Helm"; d.kind = ItemKind::Armor;
      d.equip_slot = "head"; d.weight = 2.0f; d.value = 40.0f; d.modifiers.push_back({"Armor", 5.0f}); d.set_id = "guardian"; register_item(d); }
    { ItemDef d; d.id = "chest_set"; d.name = "Guardian Chest"; d.kind = ItemKind::Armor;
      d.equip_slot = "chest"; d.weight = 6.0f; d.value = 80.0f; d.modifiers.push_back({"Armor", 10.0f}); d.set_id = "guardian"; register_item(d); }
    register_set_bonus({"guardian", 2, {{"Armor", 20.0f}}, {"set.guardian"}});

    // G5 demo materials + a recipe + a salvage rule so crafting/economy are testable.
    { ItemDef d; d.id = "iron_ore";  d.name = "Iron Ore";  d.kind = ItemKind::Material; d.max_stack = 50; d.weight = 0.5f; d.value = 3.0f;  register_item(d); }
    { ItemDef d; d.id = "iron_ingot"; d.name = "Iron Ingot"; d.kind = ItemKind::Material; d.max_stack = 50; d.weight = 0.6f; d.value = 8.0f; register_item(d); }
    register_recipe({"smelt_iron", "Smelt Iron Ingot", {{"iron_ore", 2}}, {{"iron_ingot", 1}}, "", ""});
    register_recipe({"forge_sword", "Forge Iron Sword", {{"iron_ingot", 3}}, {{"sword_iron", 1}}, "station.forge", ""});
    register_salvage({"sword_iron", {{"iron_ingot", 1}}});

    // G12 demo weapons (a hitscan rifle + a projectile launcher).
    { WeaponDef d; d.id = "rifle"; d.name = "Assault Rifle"; d.fire_mode = FireMode::Auto;
      d.damage = 18.0f; d.damage_type = "kinetic"; d.rpm = 600.0f; d.mag_size = 30; d.spread_deg = 1.5f; d.range = 120.0f; register_weapon(d); }
    { WeaponDef d; d.id = "launcher"; d.name = "Rocket Launcher"; d.fire_mode = FireMode::Semi;
      d.damage = 90.0f; d.damage_type = "explosive"; d.rpm = 40.0f; d.mag_size = 1; d.projectile_speed = 30.0f; d.range = 150.0f; d.reload_time = 2.5f; register_weapon(d); }

    // G13 demo vehicle + a simple square track.
    { VehicleDef d; d.id = "hatchback"; d.name = "Hatchback"; d.max_speed = 45.0f; d.accel = 16.0f;
      d.turn_rate = 100.0f; d.boost_mult = 1.6f; register_vehicle(d); }
    { RaceTrack t; t.id = "loop"; t.laps = 3;
      t.checkpoints = { {{50, 0, 0}, 8}, {{50, 0, 50}, 8}, {{0, 0, 50}, 8}, {{0, 0, 0}, 8} };
      register_track(t); }

    // G14 demo buildables + a smelting recipe (ore -> ingot).
    register_buildable({"miner",   "Miner",   {}});
    register_buildable({"smelter", "Smelter", {{"iron_ingot", 2}}});
    register_buildable({"conveyor","Conveyor",{{"iron_ingot", 1}}});
    register_machine_recipe({"smelt", {{"iron_ore", 1}}, {{"iron_ingot", 1}}, 2.0f});
}

int EcsSceneBridge::load_gameplay_data(const std::string& dir) { return ecs::load_items_dir(dir); }

bool EcsSceneBridge::create_example_items(const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << ecs::item_file_template();
    return static_cast<bool>(f);
}

bool EcsSceneBridge::make_gameplay_dummy(const scene::Transform* tf) {
    seed_demo_content();
    const uint32_t id = ecs_entity_id(tf);
    if (id == kNoEcsEntity) return false;
    using namespace ecs;
    World& w = impl_->world;
    const Entity e = static_cast<Entity>(id);

    AttributeSet attrs;
    attrs.define("Health", 100, 0, 100);
    attrs.define("Stamina", 100, 0, 100);
    attrs.define("Strength", 10, 0, 999);
    attrs.define("Vitality", 10, 0, 999);
    attrs.define("AttackPower", 0, 0, 999);
    attrs.define("Armor", 0, 0, 999);
    attrs.define("resist.fire", 0.25f, 0, 1);
    w.add<AttributeSet>(e, attrs);

    DerivedStats ds; ds.stats.push_back({"AttackPower", 0.0f, {{"Strength", 2.0f}}, false});
    w.add<DerivedStats>(e, ds);

    Regeneration rg; rg.entries.push_back({"Stamina", 15.0f}); rg.entries.push_back({"Health", 2.0f});
    w.add<Regeneration>(e, rg);

    Progression prog; prog.curve = {100.0f, 0.0f, 0.0f}; prog.points_per_level = 1; prog.skill_points = 3;
    prog.per_level_growth.push_back({"Strength", 1.0f});
    w.add<Progression>(e, prog);

    SkillTree tree;
    tree.nodes.push_back({"might1", "Might I",  1, {},         {{"Strength", 5.0f}}, {}});
    tree.nodes.push_back({"might2", "Might II", 2, {"might1"}, {{"Strength", 5.0f}}, {"ability.power_strike"}});
    w.add<SkillTree>(e, tree);
    w.add<UnlockedSkills>(e, UnlockedSkills{});

    GameplayTags tags; tags.add("faction.player");
    w.add<GameplayTags>(e, tags);

    StateMachine sm;
    sm.states = { {"idle", {"state.idle"}, 0.0f, ""}, {"move", {"state.move"}, 0.0f, ""},
                  {"dodge", {"state.dodge", "state.iframe"}, 0.4f, "dodge.end"}, {"dead", {"state.dead"}, 0.0f, ""} };
    sm.transitions = { {"idle", "move", "input.move", {}, {}}, {"move", "idle", "input.stop", {}, {}},
                       {"", "dodge", "input.dodge", {}, {"state.dodge"}}, {"dodge", "idle", "dodge.end", {}, {}},
                       {"", "dead", "died", {}, {}} };
    sm.initial = "idle";
    w.add<StateMachine>(e, sm);

    CombatActor ca; ca.damage_type = "physical";
    w.add<CombatActor>(e, ca);

    AbilitySet abil;
    Ability dash; dash.name = "Dash"; dash.cost_attribute = "Stamina"; dash.cost = 30.0f; dash.cooldown = 3.0f;
    { GameplayEffect fx; fx.name = "Dashing"; fx.duration_type = EffectDurationType::Duration;
      fx.duration = 0.3f; fx.granted_tags = {"state.dashing"}; dash.effects_self.push_back(fx); }
    abil.abilities.push_back({dash, 0.0f});
    w.add<AbilitySet>(e, abil);

    Inventory inv; inv.max_slots = 20;
    inventory_add(inv, ItemInstance{"potion_hp", 5, 0, {}});
    inventory_add(inv, ItemInstance{"sword_iron", 1, 0, {}});
    inventory_add(inv, ItemInstance{"helm_set", 1, 0, {}});
    inventory_add(inv, ItemInstance{"chest_set", 1, 0, {}});
    w.add<Inventory>(e, inv);
    w.add<Equipment>(e, Equipment{});

    recompute_derived(w, e);
    return true;
}

uint32_t EcsSceneBridge::ecs_entity_id(const schizo::scene::Transform* tf) const {
    auto it = impl_->tf_to_entity.find(const_cast<scene::Transform*>(tf));
    if (it == impl_->tf_to_entity.end()) return kNoEcsEntity;
    return entt::to_integral(it->second);
}

// Generic reflection-driven component (de)serialization lives in
// ecs/component_serialize.h (shared with prefabs); used via ecs:: below.

bool EcsSceneBridge::save_gameplay(
    const std::shared_ptr<schizo::scene::Scene>& scene, const std::string& path) {
    if (!scene) return false;
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << "# GameWorldshaper gameplay components (entity-index keyed)\n";
    const auto& ents = scene->GetEntities();
    for (size_t i = 0; i < ents.size(); ++i) {
        const auto& e = ents[i];
        if (!e) continue;
        const uint32_t id = ecs_entity_id(e->GetTransform());
        if (id == kNoEcsEntity) continue;
        const ecs::Entity ee = static_cast<ecs::Entity>(id);
        bool header = false;
        for (const auto& ct : ecs::authorable_components()) {
            if (!ct.has(impl_->world, ee)) continue;
            void* comp = ct.get(impl_->world, ee);
            if (!comp) continue;
            if (!header) { out << "ENTITY " << i << "\n"; header = true; }
            out << ct.name << '\t' << ecs::to_hex(ecs::serialize_authorable(ct, comp)) << "\n";
        }
    }
    return true;
}

bool EcsSceneBridge::load_gameplay(
    const std::shared_ptr<schizo::scene::Scene>& scene, const std::string& path) {
    if (!scene) return false;
    std::ifstream in(path);
    if (!in) return true;             // no sidecar -> nothing to restore (not an error)
    sync_and_run(scene);              // create ECS entities for the freshly-loaded scene
    const auto& ents = scene->GetEntities();
    std::string line;
    int idx = -1;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line.compare(0, 7, "ENTITY ") == 0) { idx = std::atoi(line.c_str() + 7); continue; }
        if (idx < 0 || idx >= static_cast<int>(ents.size()) || !ents[idx]) continue;
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        const std::string name = line.substr(0, tab);
        const ecs::AuthorableComponent* ct = ecs::find_authorable(name.c_str());
        if (!ct) continue;
        const uint32_t id = ecs_entity_id(ents[idx]->GetTransform());
        if (id == kNoEcsEntity) continue;
        const ecs::Entity ee = static_cast<ecs::Entity>(id);
        ct->add(impl_->world, ee);    // default-construct, then overwrite from bytes
        if (void* comp = ct->get(impl_->world, ee)) {
            const std::vector<uint8_t> bytes = ecs::from_hex(line.substr(tab + 1));
            ecs::deserialize_authorable(*ct, comp, bytes.data(), bytes.size());
        }
    }
    return true;
}

bool EcsSceneBridge::save_entity_prefab(const schizo::scene::Transform* tf, const std::string& path) {
    const uint32_t id = ecs_entity_id(tf);
    if (id == kNoEcsEntity) return false;
    // Name = the file's stem (after the last slash, before the last dot).
    std::string name = path;
    if (const size_t s = name.find_last_of("/\\"); s != std::string::npos) name = name.substr(s + 1);
    if (const size_t d = name.find_last_of('.');  d != std::string::npos) name = name.substr(0, d);

    const ecs::Prefab p = ecs::Prefab::capture(impl_->world, static_cast<ecs::Entity>(id), name);
    if (p.empty()) return false;   // nothing to save (no gameplay components on it)
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << p.to_text();
    return true;
}

bool EcsSceneBridge::apply_prefab_file(const schizo::scene::Transform* tf, const std::string& path) {
    const uint32_t id = ecs_entity_id(tf);
    if (id == kNoEcsEntity) return false;
    std::ifstream in(path);
    if (!in) return false;
    std::stringstream ss; ss << in.rdbuf();
    ecs::Prefab::from_text(ss.str()).apply(impl_->world, static_cast<ecs::Entity>(id));
    return true;
}

bool EcsSceneBridge::snapshot_selfcheck(
    size_t& out_bytes, size_t& out_reloaded_entities) const {
    ecs::World& world = impl_->world;

    std::vector<uint8_t> buf;
    gws::serialize::BinaryWriter w(buf);
    ecs::save_world<ecs::Transform, ecs::LocalToWorld,
                    ecs::Velocity, ecs::Name>(w, world);
    out_bytes = buf.size();

    ecs::World tmp;
    gws::serialize::BinaryReader r(buf.data(), buf.size());
    const bool ok = ecs::load_world<ecs::Transform, ecs::LocalToWorld,
                                    ecs::Velocity, ecs::Name>(r, tmp);
    out_reloaded_entities = tmp.count<ecs::Transform>();

    return ok
        && tmp.count<ecs::Transform>()    == world.count<ecs::Transform>()
        && tmp.count<ecs::LocalToWorld>() == world.count<ecs::LocalToWorld>();
}

size_t EcsSceneBridge::collect_scene_draws(double& out_ms) const {
    std::vector<ecs::DrawRecord> draws;
    const auto t0 = std::chrono::steady_clock::now();
    const size_t n = ecs::collect_draws(impl_->world, draws);
    out_ms = std::chrono::duration<double, std::milli>(
                 std::chrono::steady_clock::now() - t0).count();
    return n;
}

size_t EcsSceneBridge::draw_benchmark(size_t n, double& out_ms) {
    ecs::World w;
    for (size_t i = 0; i < n; ++i) {
        ecs::Entity e = w.create();
        ecs::Transform t;
        t.position = {static_cast<float>(i % 1000),
                      static_cast<float>((i / 1000) % 1000), 0.0f};
        w.add<ecs::Transform>(e, t);
        w.add<ecs::LocalToWorld>(e, ecs::LocalToWorld{compose_trs(t)});
        ecs::MeshRenderer mr;
        mr.mesh     = (i % 8) + 1;
        mr.material = static_cast<int32_t>(i % 4);
        w.add<ecs::MeshRenderer>(e, mr);
    }
    std::vector<ecs::DrawRecord> draws;
    const auto t0 = std::chrono::steady_clock::now();
    const size_t got = ecs::collect_draws(w, draws);
    out_ms = std::chrono::duration<double, std::milli>(
                 std::chrono::steady_clock::now() - t0).count();
    return got;
}

EcsSceneBridge::EcsSceneBridge() : impl_(std::make_unique<Impl>()) {
    // ONE registration entry point, shared with tools/gameplay_check and the
    // generated component reference (tools/docgen). The list used to be
    // duplicated here and in gameplay_check, so a new module could silently
    // be missing from one of them. See engine/core/ecs/register_all.h.
    ecs::register_all_components();

    // G6: route every gameplay event into quest progress (deferred flush = safe).
    impl_->event_bus.subscribe("*", [impl = impl_.get()](const ecs::GameplayEvent& ev) {
        ecs::advance_quests(impl->world, ev, &impl->event_bus);
    });
    // G16: a gunshot is loud — alert guards within earshot of the shooter.
    impl_->event_bus.subscribe("weapon.fire", [impl = impl_.get()](const ecs::GameplayEvent& ev) {
        if (const auto* t = impl->world.try_get<ecs::Transform>(ev.instigator))
            ecs::emit_noise(impl->world, t->position, 25.0f, &impl->event_bus, ev.instigator);
    });
}

EcsSceneBridge::~EcsSceneBridge() = default;

// TRS compose — how a flat LocalToWorld system turns a Transform into its
// LOCAL matrix (the world matrix for a root; combined with parents below).
static glm::mat4 compose_trs(const ecs::Transform& t) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), t.position);
    m = m * glm::mat4_cast(t.rotation);
    m = glm::scale(m, t.scale);
    return m;
}

void EcsSceneBridge::sync_and_run(
    const std::shared_ptr<schizo::scene::Scene>& scene) {

    entity_count_ = matrices_written_ = checked_ = verified_ = 0;
    created_ = reused_ = destroyed_ = 0;

    auto& world        = impl_->world;
    auto& tf_to_entity = impl_->tf_to_entity;
    auto& shadow       = impl_->shadow;
    auto& oop_tf       = impl_->oop_tf;
    auto& oop_ent      = impl_->oop_ent;
    auto& parent_idx   = impl_->parent_idx;

    shadow.clear();
    oop_tf.clear();
    oop_ent.clear();
    parent_idx.clear();
    impl_->result.clear();
    if (!scene) {
        // Scene gone: tear down all persistent shadow entities.
        for (auto& kv : tf_to_entity) world.destroy(kv.second);
        destroyed_ = tf_to_entity.size();
        tf_to_entity.clear();
        return;
    }

    // --- Pass 1: gather active entities (active-in-hierarchy implies every
    // ancestor is also active, so the parent map below resolves for non-roots).
    for (const auto& ent : scene->GetEntities()) {
        if (!ent || !ent->IsActiveInHierarchy()) continue;
        scene::Transform* tf = ent->GetTransform();
        if (!tf) continue;
        oop_tf.push_back(tf);
        oop_ent.push_back(ent.get());
    }

    // --- Pass 2: reconcile persistent map. Destroy shadow entities whose OOP
    // transform is no longer active, create entities for new ones, reuse the
    // rest (keeping their identity + components across frames).
    std::unordered_map<scene::Transform*, int> tf_to_index;
    tf_to_index.reserve(oop_tf.size());
    for (size_t i = 0; i < oop_tf.size(); ++i)
        tf_to_index.emplace(oop_tf[i], static_cast<int>(i));

    std::vector<scene::Transform*> to_erase;
    for (auto& kv : tf_to_entity)
        if (tf_to_index.find(kv.first) == tf_to_index.end()) {
            world.destroy(kv.second);
            to_erase.push_back(kv.first);
        }
    for (scene::Transform* tf : to_erase) tf_to_entity.erase(tf);
    destroyed_ = to_erase.size();

    // Create-or-reuse, in active order, refreshing the Transform component.
    shadow.resize(oop_tf.size());
    for (size_t i = 0; i < oop_tf.size(); ++i) {
        scene::Transform* tf = oop_tf[i];
        ecs::Entity e;
        auto found = tf_to_entity.find(tf);
        if (found != tf_to_entity.end()) {
            e = found->second;
            ++reused_;
        } else {
            e = world.create();
            world.add<ecs::LocalToWorld>(e, ecs::LocalToWorld{});
            world.add<ShadowParent>(e, ShadowParent{});
            tf_to_entity.emplace(tf, e);
            ++created_;
        }
        // Transform sync. Normally OOP -> ECS: the scene is authoritative and
        // the ECS is a per-frame mirror.
        //
        // An entity tagged EcsAuthoritative syncs the OTHER WAY (Phase 3.6).
        // Without that, an ECS system writing a Transform has its work
        // overwritten here before anything can see it, which is precisely why
        // gameplay cannot move anything without going through the OOP scene.
        // Per-entity rather than a global switch: flipping everything at once
        // would change the gizmo, physics, playback and serialization in one
        // step, and a half-finished authority flip leaves the editor subtly
        // wrong rather than obviously broken.
        if (world.has<ecs::EcsAuthoritative>(e) && world.has<ecs::Transform>(e)) {
            const auto& t = *world.try_get<ecs::Transform>(e);
            // Write back only on a real difference. Unconditional writes would
            // dirty the scene every frame and defeat the editor's
            // unsaved-changes tracking.
            if (t.position != tf->GetLocalPosition()) tf->SetLocalPosition(t.position);
            if (t.rotation != tf->GetLocalRotation()) tf->SetLocalRotation(t.rotation);
            if (t.scale    != tf->GetLocalScale())    tf->SetLocalScale(t.scale);
        } else {
            ecs::Transform t;
            t.position = tf->GetLocalPosition();
            t.rotation = tf->GetLocalRotation();
            t.scale    = tf->GetLocalScale();
            world.add<ecs::Transform>(e, t);  // emplace_or_replace
        }
        shadow[i] = e;

        // Mirror the OOP entity's drawable info into an ECS MeshRenderer so the
        // scene's drawables live in the ECS (Stage 1.4) and collect_draws()
        // runs on the real scene. Non-authoritative: rendering still uses the
        // OOP path; this proves the data-oriented draw hand-off on real data.
        scene::Entity* oent = oop_ent[i];
        ecs::MeshRenderer mr{};
        bool drawable = false;
        if (auto m = oent->GetComponent<scene::MeshRendererComponent>()) {
            mr.primitive = static_cast<uint32_t>(m->GetMeshType());
            drawable = true;
        }
        if (const auto* mc = oent->GetMeshComponent()) {
            if (!mc->mesh_path.empty()) {
                mr.mesh = static_cast<uint64_t>(std::hash<std::string>{}(mc->mesh_path));
                drawable = true;
            }
        }
        if (drawable) world.add<ecs::MeshRenderer>(e, mr);
        else if (world.has<ecs::MeshRenderer>(e)) world.remove<ecs::MeshRenderer>(e);
    }
    entity_count_ = shadow.size();

    // --- Pass 3: wire parents (resolve OOP parent -> shadow index / entity).
    parent_idx.assign(shadow.size(), -1);
    for (size_t i = 0; i < shadow.size(); ++i) {
        scene::Transform* p = oop_tf[i]->GetParent();
        if (!p) continue;
        auto it = tf_to_index.find(p);
        if (it != tf_to_index.end()) {
            parent_idx[i] = it->second;
            world.get<ShadowParent>(shadow[i]).value = shadow[it->second];
        }
    }

    // --- System phase A (PARALLEL): LocalToWorld.value = local matrix.
    ecs::parallel_each<ecs::Transform, ecs::LocalToWorld>(
        world, [](ecs::Entity, ecs::Transform& t, ecs::LocalToWorld& l) {
            l.value = compose_trs(t);
        });

    // --- System phase B (hierarchy resolve): world = parent_world * local.
    // Dependency-ordered, so serial with memoisation here; the real engine
    // parallelises this by depth-wave (noted for step 2). Reads the local
    // matrices written above, writes world matrices back into LocalToWorld.
    std::vector<glm::mat4> local(shadow.size());
    for (size_t i = 0; i < shadow.size(); ++i)
        local[i] = world.get<ecs::LocalToWorld>(shadow[i]).value;

    std::vector<glm::mat4> world_mat(shadow.size());
    std::vector<char>      done(shadow.size(), 0);
    // Iterative resolve up the parent chain (no recursion depth limit risk).
    for (size_t i = 0; i < shadow.size(); ++i) {
        if (done[i]) continue;
        // Walk up to the highest unresolved ancestor, recording the chain.
        std::vector<int> chain;
        int cur = static_cast<int>(i);
        while (cur != -1 && !done[cur]) { chain.push_back(cur); cur = parent_idx[cur]; }
        glm::mat4 acc = (cur == -1) ? glm::mat4(1.0f) : world_mat[cur];
        // Apply from the top of the chain down.
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            acc = acc * local[*it];
            world_mat[*it] = acc;
            done[*it] = 1;
        }
    }
    for (size_t i = 0; i < shadow.size(); ++i) {
        world.get<ecs::LocalToWorld>(shadow[i]).value = world_mat[i];
        impl_->result.emplace(oop_tf[i], world_mat[i]);
    }
    matrices_written_ = shadow.size();

    // --- Verify: EVERY entity's ECS world matrix must equal the OOP one. A
    // full match validates components + storage + parallel TRS + hierarchy.
    for (size_t i = 0; i < shadow.size(); ++i) {
        ++checked_;
        const glm::mat4 expected = oop_tf[i]->GetWorldMatrix();
        const float* a = &world_mat[i][0][0];
        const float* b = &expected[0][0];
        float max_diff = 0.0f;
        for (int k = 0; k < 16; ++k)
            max_diff = std::max(max_diff, std::fabs(a[k] - b[k]));
        if (max_diff < 1e-3f) ++verified_;
    }
}

}  // namespace schizo::editor
