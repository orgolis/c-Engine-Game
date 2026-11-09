#include "ecs_bridge.h"
#include "profiler/profiler.h"
#include <optional>
#include "ecs/register_all.h"
#include "ecs/gameplay_tick.h"

#include "ecs/world.h"
#include "camera_component.h"
#include "water_component.h"
#include "terrain_component.h"
#include "script_component.h"
#include "audio_components.h"
#include "light_component.h"
#include "collider_component.h"
#include "npc_agent_component.h"
#include "particle_emitter_component.h"
#include "skinned_mesh_component.h"
#include "ecs/components.h"
#include "ecs/scene_components.h"        // 3.6: the ECS half of the scene component set
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
#include <cstdint>

namespace schizo::editor {

namespace {

using ecs::flag_get;
using ecs::flag_set;

// ---- OOP -> ECS converters ------------------------------------------------
// One per mirrored kind: a fresh ECS value built from the scene component --
// fresh, because World::add replaces the whole component, so the in-place path
// in sync_and_run must too. Pure reads of the scene, which is what lets them
// run on the job system; the serial path below uses the same functions, so the
// two paths cannot disagree about a field.
ecs::LightComponent to_ecs(const scene::LightComponent& lc) {
    ecs::LightComponent l{};
    l.type      = static_cast<uint32_t>(lc.GetType());
    l.color     = lc.GetColor();
    l.intensity = lc.GetIntensity();
    l.range     = lc.GetRange();
    l.flags     = flag_set(flag_set(0u, 0, lc.GetCastShadow()), 1, lc.IsEnabled());
    return l;
}
ecs::Collider to_ecs(const scene::ColliderComponent& cc) {
    ecs::Collider c{};
    c.shape  = static_cast<uint32_t>(cc.GetShape());
    c.radius = cc.GetRadius();
    c.height = cc.GetHeight();
    c.layer  = static_cast<uint32_t>(cc.GetMask());
    return c;
}
ecs::CameraComponent to_ecs(const scene::CameraComponent& cam) {
    ecs::CameraComponent c{};
    c.projection = static_cast<uint32_t>(cam.GetProjection());
    c.fov        = cam.GetFOV();
    c.ortho_size = cam.GetOrthographicSize();
    c.near_plane = cam.GetNearPlane();
    return c;
}
ecs::WaterComponent to_ecs(const scene::WaterComponent& w) {
    ecs::WaterComponent c{};
    c.wave_height  = w.GetWaveHeight();
    c.wave_speed   = w.GetWaveSpeed();
    c.wave_scale   = w.GetWaveScale();
    c.clarity      = w.GetClarity();
    c.reflectivity = w.GetReflectivity();
    c.flags        = flag_set(0u, 0, w.IsPhysical());
    return c;
}
ecs::TerrainComponent to_ecs(const scene::TerrainComponent& t) {
    ecs::TerrainComponent c{};
    c.size         = t.GetSize();
    c.height_scale = t.GetHeightScale();
    c.water_level  = t.GetWaterLevel();
    return c;
}
ecs::ScriptComponent to_ecs(const scene::ScriptComponent& sc) {
    ecs::ScriptComponent c{};
    // The path is a string and reflected fields must be trivially
    // copyable, so the ECS side carries a hash of it. Enough to ask "is
    // this the same script"; not enough to load one, which stays the OOP
    // side's job until the asset system hands out real ids.
    const std::string& path = sc.GetScriptPath();
    uint64_t h = 1469598103934665603ull;
    for (char ch : path) { h ^= static_cast<uint8_t>(ch); h *= 1099511628211ull; }
    c.script_asset = path.empty() ? 0ull : h;
    c.flags        = flag_set(0u, 0, sc.IsEnabled());
    return c;
}
ecs::AudioSource to_ecs(const scene::AudioSourceComponent& a) {
    ecs::AudioSource c{};
    c.volume = a.GetVolume();
    c.pitch  = a.GetPitch();
    c.radius = a.GetRadius();
    c.flags  = flag_set(flag_set(flag_set(0u, 0, a.IsLooping()),
                                 1, a.IsSpatial()),
                        2, a.IsPlaying());
    return c;
}
// The three below are mirrored only while enabled/active; callers check that.
ecs::ParticleEmitter to_ecs(const scene::ParticleEmitterComponent& pe) {
    ecs::ParticleEmitter c{};
    c.spawn_rate    = pe.spawn_rate;
    c.max_particles = pe.max_particles;
    c.color_start   = pe.color_start;
    c.color_end     = pe.color_end;
    c.flags         = flag_set(flag_set(0u, 0, pe.enabled), 1, pe.emitting);
    return c;
}
ecs::NpcAgent to_ecs(const scene::NpcAgentComponent& na) {
    ecs::NpcAgent c{};
    c.sight_range   = na.sight_range;
    c.sight_fov_deg = na.sight_fov_deg;
    c.move_speed    = na.move_speed;
    c.patrol_radius = na.patrol_radius;
    c.attack_range  = na.attack_range;
    c.ability_index = na.ability_index;
    c.flags         = flag_set(0u, 0, na.enabled);
    return c;
}
ecs::SkinnedMesh to_ecs(const scene::SkinnedMeshComponent& sm) {
    ecs::SkinnedMesh c{};
    c.clip_index     = sm.clip_index;
    c.idle_clip      = sm.idle_clip;
    c.move_clip      = sm.move_clip;
    c.speed          = sm.speed;
    c.move_threshold = sm.move_threshold;
    c.flags          = flag_set(flag_set(0u, 0, sm.playing), 1, sm.drive_from_motion);
    return c;
}

// Mirror every non-Transform component kind between the OOP entity and its ECS
// entity.
//
// `ecs_authoritative` picks the direction, per entity, exactly as the Transform
// sync does. Writing OOP -> ECS unconditionally is what made the ECS a shadow;
// writing ECS -> OOP unconditionally would break the editor. One flag, both
// behaviours, chosen per entity.
//
// Components are added only when the OOP entity HAS them, so an ECS query for
// "everything with a light" returns the lights rather than every entity in the
// scene carrying a default-constructed one.
void sync_scene_components(ecs::World& world, ecs::Entity e,
                           scene::Entity* oent, bool ecs_authoritative) {
    if (!oent) return;

    // ---- Light ----------------------------------------------------------
    if (auto lc = oent->GetComponent<scene::LightComponent>()) {
        if (ecs_authoritative) {
            if (const auto* l = world.try_get<ecs::LightComponent>(e)) {
                lc->SetIntensity(l->intensity);
                lc->SetRange(l->range);
                lc->SetColor(l->color);
                lc->SetEnabled(flag_get(l->flags, 1));
                lc->SetCastShadow(flag_get(l->flags, 0));
            }
        } else {
            world.add<ecs::LightComponent>(e, to_ecs(*lc));
        }
    }

    // ---- Collider -------------------------------------------------------
    if (auto cc = oent->GetComponent<scene::ColliderComponent>()) {
        if (ecs_authoritative) {
            if (const auto* c = world.try_get<ecs::Collider>(e)) {
                cc->SetRadius(c->radius);
                cc->SetHeight(c->height);
            }
        } else {
            world.add<ecs::Collider>(e, to_ecs(*cc));
        }
    }

    // ---- Camera ---------------------------------------------------------
    if (auto cam = oent->GetComponent<scene::CameraComponent>()) {
        if (ecs_authoritative) {
            if (const auto* c = world.try_get<ecs::CameraComponent>(e)) {
                cam->SetFOV(c->fov);
                cam->SetNearPlane(c->near_plane);
            }
        } else {
            world.add<ecs::CameraComponent>(e, to_ecs(*cam));
        }
    }

    // ---- Water ----------------------------------------------------------
    if (auto w = oent->GetComponent<scene::WaterComponent>()) {
        if (ecs_authoritative) {
            if (const auto* c = world.try_get<ecs::WaterComponent>(e)) {
                w->SetWaveHeight(c->wave_height);
                w->SetWaveSpeed(c->wave_speed);
                w->SetClarity(c->clarity);
            }
        } else {
            world.add<ecs::WaterComponent>(e, to_ecs(*w));
        }
    }

    // ---- Terrain --------------------------------------------------------
    // One direction only, and deliberately: the heightfield is megabytes with
    // its own editing tools, so an ECS write cannot meaningfully describe it.
    // A half-supported reverse path would be worse than none.
    if (auto t = oent->GetComponent<scene::TerrainComponent>()) {
        world.add<ecs::TerrainComponent>(e, to_ecs(*t));
    }

    // ---- Script ---------------------------------------------------------
    if (auto sc = oent->GetComponent<scene::ScriptComponent>()) {
        world.add<ecs::ScriptComponent>(e, to_ecs(*sc));
    }

    // ---- Audio ----------------------------------------------------------
    if (auto a = oent->GetComponent<scene::AudioSourceComponent>()) {
        if (ecs_authoritative) {
            if (const auto* c = world.try_get<ecs::AudioSource>(e)) {
                a->SetVolume(c->volume);
                a->SetPitch(c->pitch);
            }
        } else {
            world.add<ecs::AudioSource>(e, to_ecs(*a));
        }
    }

    // ---- Particle emitter (POD on both sides) ---------------------------
    if (auto* pe = oent->GetParticleEmitterComponent()) {
        if (ecs_authoritative) {
            if (const auto* c = world.try_get<ecs::ParticleEmitter>(e)) {
                pe->spawn_rate    = c->spawn_rate;
                pe->max_particles = c->max_particles;
                pe->color_start   = c->color_start;
                pe->color_end     = c->color_end;
                pe->enabled       = flag_get(c->flags, 0);
                pe->emitting      = flag_get(c->flags, 1);
            }
        } else if (pe->enabled) {
            world.add<ecs::ParticleEmitter>(e, to_ecs(*pe));
        }
    }

    // ---- NPC agent ------------------------------------------------------
    if (auto* na = oent->GetNpcAgentComponent()) {
        if (ecs_authoritative) {
            if (const auto* c = world.try_get<ecs::NpcAgent>(e)) {
                na->sight_range   = c->sight_range;
                na->sight_fov_deg = c->sight_fov_deg;
                na->move_speed    = c->move_speed;
                na->attack_range  = c->attack_range;
                na->ability_index = c->ability_index;
                na->enabled       = flag_get(c->flags, 0);
            }
        } else if (na->enabled) {
            world.add<ecs::NpcAgent>(e, to_ecs(*na));
        }
    }

    // ---- Skinned mesh ---------------------------------------------------
    if (auto* sm = oent->GetSkinnedMeshComponent()) {
        if (ecs_authoritative) {
            if (const auto* c = world.try_get<ecs::SkinnedMesh>(e)) {
                sm->clip_index        = c->clip_index;
                sm->idle_clip         = c->idle_clip;
                sm->move_clip         = c->move_clip;
                sm->speed             = c->speed;
                sm->move_threshold    = c->move_threshold;
                sm->playing           = flag_get(c->flags, 0);
                sm->drive_from_motion = flag_get(c->flags, 1);
            }
        } else if (sm->active()) {
            world.add<ecs::SkinnedMesh>(e, to_ecs(*sm));
        }
    }
}

// The entity's drawable info as an ECS MeshRenderer; false when it has none.
bool mesh_renderer_of(scene::Entity* oent, ecs::MeshRenderer& mr) {
    mr = ecs::MeshRenderer{};
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
    return drawable;
}

// ---- The in-place mirror (sync_and_run's parallel path) -------------------
template <typename T>
using PoolOf = std::remove_reference_t<
    decltype(std::declval<entt::registry&>().template storage<T>())>;

// Every pool the in-place mirror touches, fetched ONCE on the calling thread:
// registry::storage<T>() creates a missing pool, and creating one is a
// mutation. The workers then only call contains() and get() on these, which
// read the pools' existing arrays.
struct MirrorPools {
    PoolOf<ecs::EcsAuthoritative>& authoritative;
    PoolOf<ecs::Transform>&        transform;
    PoolOf<ecs::LightComponent>&   light;
    PoolOf<ecs::Collider>&         collider;
    PoolOf<ecs::CameraComponent>&  camera;
    PoolOf<ecs::WaterComponent>&   water;
    PoolOf<ecs::TerrainComponent>& terrain;
    PoolOf<ecs::ScriptComponent>&  script;
    PoolOf<ecs::AudioSource>&      audio;
    PoolOf<ecs::ParticleEmitter>&  particle;
    PoolOf<ecs::NpcAgent>&         npc;
    PoolOf<ecs::SkinnedMesh>&      skinned;
    PoolOf<ecs::MeshRenderer>&     mesh;

    static MirrorPools fetch(entt::registry& r) {
        return MirrorPools{
            r.storage<ecs::EcsAuthoritative>(), r.storage<ecs::Transform>(),
            r.storage<ecs::LightComponent>(),   r.storage<ecs::Collider>(),
            r.storage<ecs::CameraComponent>(),  r.storage<ecs::WaterComponent>(),
            r.storage<ecs::TerrainComponent>(), r.storage<ecs::ScriptComponent>(),
            r.storage<ecs::AudioSource>(),      r.storage<ecs::ParticleEmitter>(),
            r.storage<ecs::NpcAgent>(),         r.storage<ecs::SkinnedMesh>(),
            r.storage<ecs::MeshRenderer>()};
    }
};

// Mirror one entity OOP -> ECS by overwriting components it ALREADY has, which
// is every component of every entity in a scene that is not changing shape.
// Overwriting an existing component is a write to that entity's slot and
// nothing else, so different entities can do it on different threads -- and
// it is exactly what World::add does for an existing component (EnTT's
// emplace_or_replace), with no update listeners anywhere in this codebase to
// skip.
//
// Returns false, having possibly written some components, when the entity
// needs anything else: an ECS-authoritative tag (the other direction), a
// component the ECS does not have yet, or a MeshRenderer to remove -- every
// one a structural change. The caller then runs the original serial code for
// that entity, which rewrites the same values through World::add.
bool mirror_in_place(MirrorPools& P, ecs::Entity e, scene::Entity* o,
                     const scene::Transform* tf) {
    if (P.authoritative.contains(e) || !P.transform.contains(e)) return false;
    {
        ecs::Transform t;
        t.position = tf->GetLocalPosition();
        t.rotation = tf->GetLocalRotation();
        t.scale    = tf->GetLocalScale();
        P.transform.get(e) = t;
    }
    const auto put = [e](auto& pool, const auto& value) {
        if (!pool.contains(e)) return false;
        pool.get(e) = value;
        return true;
    };
    if (auto c = o->GetComponent<scene::LightComponent>())
        if (!put(P.light, to_ecs(*c))) return false;
    if (auto c = o->GetComponent<scene::ColliderComponent>())
        if (!put(P.collider, to_ecs(*c))) return false;
    if (auto c = o->GetComponent<scene::CameraComponent>())
        if (!put(P.camera, to_ecs(*c))) return false;
    if (auto c = o->GetComponent<scene::WaterComponent>())
        if (!put(P.water, to_ecs(*c))) return false;
    if (auto c = o->GetComponent<scene::TerrainComponent>())
        if (!put(P.terrain, to_ecs(*c))) return false;
    if (auto c = o->GetComponent<scene::ScriptComponent>())
        if (!put(P.script, to_ecs(*c))) return false;
    if (auto c = o->GetComponent<scene::AudioSourceComponent>())
        if (!put(P.audio, to_ecs(*c))) return false;
    if (auto* pe = o->GetParticleEmitterComponent(); pe && pe->enabled)
        if (!put(P.particle, to_ecs(*pe))) return false;
    if (auto* na = o->GetNpcAgentComponent(); na && na->enabled)
        if (!put(P.npc, to_ecs(*na))) return false;
    if (auto* sm = o->GetSkinnedMeshComponent(); sm && sm->active())
        if (!put(P.skinned, to_ecs(*sm))) return false;

    ecs::MeshRenderer mr;
    if (mesh_renderer_of(o, mr)) {
        if (!put(P.mesh, mr)) return false;
    } else if (P.mesh.contains(e)) {
        return false;   // a removal is structural
    }
    return true;
}

// Below this many entities, fanning out costs more than it spreads.
constexpr size_t kParallelMirrorMin = 1024;

}  // namespace


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
    // Persistent, one slot per active OOP Transform, kept ACROSS frames and
    // reconciled each sync (create new / reuse existing / destroy gone) so an
    // entity keeps its identity and components frame to frame.
    //
    // This used to be three maps: this one (Transform* -> entity), a
    // Transform* -> index map rebuilt from scratch every sync, and the
    // Transform* -> world-matrix map rendering reads, cleared and refilled
    // every sync. At 20k entities that was ~4.5 ms a frame of hashing and of
    // allocating and freeing 40k map nodes to describe a scene that had not
    // changed. One slot holds all three facts, so a steady-state sync does one
    // lookup per entity and allocates nothing. unordered_map never moves an
    // element on insert, so a Slot* stays valid for the whole sync.
    struct Slot {
        ecs::Entity entity = ecs::null_entity;
        int         index  = -1;     // position in this sync's active arrays
        uint64_t    stamp  = 0;      // the last sync that saw it active
        glm::mat4   world{1.0f};     // ECS-computed world matrix, this sync
    };
    std::unordered_map<scene::Transform*, Slot> slots;
    uint64_t                                    sync_stamp = 0;

    // Per-sync working arrays (active entities this sync, in iteration
    // order). Members rather than locals so their capacity survives: at scene
    // scale, re-growing them every frame is its own cost.
    std::vector<ecs::Entity>       shadow;
    std::vector<scene::Transform*> oop_tf;
    std::vector<scene::Entity*>    oop_ent;     // parallel to oop_tf (for components)
    std::vector<Slot*>             slot_of;     // parallel to oop_tf
    // The previous sync's gathered transforms and their parents, index-aligned
    // with slot_of/parent_idx as that sync left them. See the reconcile and
    // parents passes: when this sync gathers the same list, both reuse them.
    std::vector<scene::Transform*> prev_tf;
    std::vector<scene::Transform*> oop_parent, prev_parent;
    std::vector<int>               parent_idx;  // -1 == root
    std::vector<glm::mat4>         local, world_mat;
    std::vector<char>              done;
    std::vector<int>               chain;
    std::vector<char>              needs_serial;   // sync.entities: 1 = original path
    size_t                         verify_cursor = 0;

    // Incremental transforms (see the local_trs pass). oop_stamp is parallel
    // to oop_tf: each Transform's ChangeStamp() as gathered. local/world_mat
    // above persist across syncs and hold the previous sync's complete result
    // when have_prev_matrices is set.
    std::vector<uint64_t>          oop_stamp;
    std::vector<uint8_t>           wstate;         // 0 unknown, 1 world changed, 2 unchanged
    std::vector<uint32_t>          changed;        // indices whose world matrix is recomputed
    uint64_t                       prev_sync_begin = 0;
    bool                           have_prev_matrices = false;

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
    auto it = impl_->slots.find(const_cast<scene::Transform*>(tf));
    return it == impl_->slots.end() ? nullptr : &it->second.world;
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
    auto it = impl_->slots.find(const_cast<scene::Transform*>(tf));
    if (it == impl_->slots.end()) return kNoEcsEntity;
    return entt::to_integral(it->second.entity);
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
    auto& slots        = impl_->slots;
    auto& shadow       = impl_->shadow;
    auto& oop_tf       = impl_->oop_tf;
    auto& oop_stamp    = impl_->oop_stamp;
    // Any Transform stamped from here on counts as changed at the next sync.
    const uint64_t sync_begin = scene::Transform::ChangeStampCounter();
    auto& oop_ent      = impl_->oop_ent;
    auto& slot_of      = impl_->slot_of;
    auto& parent_idx   = impl_->parent_idx;
    const uint64_t stamp = ++impl_->sync_stamp;

    // One profiler zone per pass, back to back. This runs over every entity in
    // the scene every frame, so it is where scene size turns into milliseconds
    // -- at 20k entities it was a third of a 100 ms frame -- and a single zone
    // around the whole call could not say which pass. The passes share locals,
    // so scoped blocks do not fit; an optional zone that is reset and
    // re-emplaced gives each pass exactly its own span.
    std::optional<gws::profile::ScopedZone> lap;
    auto next = [&](const char* t) { lap.reset(); lap.emplace(t); };
    next("sync.clear");
    shadow.clear();
    std::swap(impl_->prev_tf, oop_tf);               // last sync's list, kept
    std::swap(impl_->prev_parent, impl_->oop_parent);
    oop_tf.clear();
    oop_ent.clear();
    oop_stamp.clear();
    impl_->oop_parent.clear();
    if (!scene) {
        impl_->have_prev_matrices = false;
        // Scene gone: tear down all persistent shadow entities.
        for (auto& kv : slots) world.destroy(kv.second.entity);
        destroyed_ = slots.size();
        slots.clear();
        impl_->prev_tf.clear();      // nothing from before may be reused
        impl_->prev_parent.clear();
        return;
    }

    next("sync.gather");
    // --- Pass 1: gather active entities (active-in-hierarchy implies every
    // ancestor is also active, so the parent map below resolves for non-roots).
    for (const auto& ent : scene->GetEntities()) {
        if (!ent || !ent->IsActiveInHierarchy()) continue;
        scene::Transform* tf = ent->GetTransform();
        if (!tf) continue;
        oop_tf.push_back(tf);
        oop_ent.push_back(ent.get());
        impl_->oop_parent.push_back(tf->GetParent());
        oop_stamp.push_back(tf->ChangeStamp());   // the Transform is in cache right now
    }

    next("sync.reconcile");
    // --- Pass 2: reconcile the persistent slots. Create entities for new
    // transforms, reuse the rest (keeping their identity + components across
    // frames), destroy the ones no longer active. One lookup per entity, and
    // in steady state no allocation.
    //
    // Unless nothing changed shape. If this sync gathered exactly the same
    // transforms in the same order as the last one, every slot_of pointer,
    // index and entity from the last sync is still right: slots are only
    // erased by the sweep below, and that sweep never erases a slot the last
    // sync used. So the per-entity lookup -- a scattered cache miss for every
    // entity, every frame, ~1 ms at 20k -- is skipped. Stamps go stale while
    // nothing changes, which is safe: the next sync that does change something
    // stamps every active slot, so anything stale still sweeps.
    const bool same_list = impl_->prev_tf.size() == oop_tf.size() &&
                           slots.size() == oop_tf.size() &&
                           std::equal(oop_tf.begin(), oop_tf.end(), impl_->prev_tf.begin());
    if (same_list) {
        reused_ = oop_tf.size();
    } else {
    slot_of.resize(oop_tf.size());
    for (size_t i = 0; i < oop_tf.size(); ++i) {
        auto [it, inserted] = slots.try_emplace(oop_tf[i]);
        Impl::Slot& sl = it->second;
        if (inserted) {
            sl.entity = world.create();
            world.add<ecs::LocalToWorld>(sl.entity, ecs::LocalToWorld{});
            world.add<ShadowParent>(sl.entity, ShadowParent{});
            ++created_;
        } else {
            ++reused_;
        }
        sl.index   = static_cast<int>(i);
        sl.stamp   = stamp;
        slot_of[i] = &sl;
    }
    }
    // Every active transform now has a slot stamped this sync, so a stale slot
    // exists exactly when there are more slots than active transforms. Only
    // then is the table walked -- not every frame, which is what the old
    // per-frame "build a map, diff it against the other map" did.
    if (slots.size() != oop_tf.size()) {
        for (auto it = slots.begin(); it != slots.end();) {
            if (it->second.stamp != stamp) {
                world.destroy(it->second.entity);
                it = slots.erase(it);
                ++destroyed_;
            } else {
                ++it;
            }
        }
    }

    next("sync.entities");
    // Two passes. The first mirrors, on the job system, every entity whose ECS
    // components all exist already -- in a scene that is not changing shape,
    // all of them -- by overwriting them in place (see mirror_in_place). It is
    // worth it because this loop is memory-bound: each entity is a cache miss
    // on the Entity, its component list and every component, and more cores
    // keep more of those misses in flight. At 20k entities it was ~7 ms on one
    // thread.
    //
    // The second pass is the original code, run for whatever the first could
    // not do: authoritative entities, components the ECS does not have yet,
    // removals. Below kParallelMirrorMin entities the first pass is skipped
    // and the second does everything, exactly as before.
    shadow.resize(oop_tf.size());
    auto& serial = impl_->needs_serial;
    serial.assign(oop_tf.size(), 1);
    {
        auto& js = gws::jobs::JobSystem::instance();
        if (js.initialized() && oop_tf.size() >= kParallelMirrorMin) {
            MirrorPools pools = MirrorPools::fetch(world.registry());
            auto in_place = [&](size_t i) {
                const ecs::Entity e = slot_of[i]->entity;
                shadow[i] = e;
                serial[i] = mirror_in_place(pools, e, oop_ent[i], oop_tf[i]) ? 0 : 1;
            };
            js.parallel_for(0, oop_tf.size(), [&in_place](size_t i) { in_place(i); });
        }
    }
    for (size_t i = 0; i < oop_tf.size(); ++i) {
        if (!serial[i]) continue;
        scene::Transform* tf = oop_tf[i];
        const ecs::Entity e = slot_of[i]->entity;
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
        // ---- every OTHER component kind, both directions (3.6) ----------
        //
        // Four of thirteen kinds used to reach the ECS. A gameplay system could
        // not see a light, could not tell whether an entity had a collider, and
        // could not read a camera's FOV -- so gameplay had to be written
        // against the editor's object model instead. This closes that.
        //
        // Same rule as Transform: tagged entities flow ECS -> OOP, everything
        // else keeps the existing OOP -> ECS direction, so nothing that works
        // today changes behaviour.
        sync_scene_components(world, e, oop_ent[i],
                              world.has<ecs::EcsAuthoritative>(e));

        shadow[i] = e;

        // Mirror the OOP entity's drawable info into an ECS MeshRenderer so the
        // scene's drawables live in the ECS (Stage 1.4) and collect_draws()
        // runs on the real scene. Non-authoritative: rendering still uses the
        // OOP path; this proves the data-oriented draw hand-off on real data.
        ecs::MeshRenderer mr;
        const bool drawable = mesh_renderer_of(oop_ent[i], mr);
        if (drawable) world.add<ecs::MeshRenderer>(e, mr);
        else if (world.has<ecs::MeshRenderer>(e)) world.remove<ecs::MeshRenderer>(e);
    }
    entity_count_ = shadow.size();

    next("sync.parents");
    // --- Pass 3: wire parents (resolve OOP parent -> shadow index / entity).
    //
    // With the same list as last sync, an entity whose parent pointer is also
    // unchanged keeps last sync's parent_idx and ShadowParent -- indices and
    // entities are the same, so the resolution would come out the same. Only
    // a re-parented entity is resolved again. The parent pointers were read in
    // the gather pass, while each entity was already in cache.
    const auto& opar = impl_->oop_parent;
    const auto& ppar = impl_->prev_parent;
    const bool reuse_parents = same_list && ppar.size() == opar.size() &&
                               parent_idx.size() == opar.size();
    if (!reuse_parents) parent_idx.assign(shadow.size(), -1);
    for (size_t i = 0; i < shadow.size(); ++i) {
        scene::Transform* p = opar[i];
        if (reuse_parents && p == ppar[i]) continue;
        parent_idx[i] = -1;
        if (!p) continue;
        auto it = slots.find(p);   // every remaining slot is active this sync
        if (it != slots.end()) {
            parent_idx[i] = it->second.index;
            world.get<ShadowParent>(shadow[i]).value = shadow[it->second.index];
        }
    }

    // ---- Transforms: incremental when the scene kept its shape ----------
    //
    // Recomputing every entity's local and world matrix every frame is what a
    // static scene paid for nothing: ~1 ms at 20k entities. When this sync has
    // the same entities in the same order with the same parents as the last
    // one (reuse_parents), and the last one left a complete result, only the
    // transforms stamped since the last sync began -- plus any entity on the
    // serial path (ECS-authoritative write-back, new components) -- get a new
    // local matrix; "world changed" then propagates from parent to child, and
    // only those chains are multiplied and published. Everything else keeps
    // last sync's matrices, which were computed from the same inputs.
    //
    // GWS_ECS_FULL_TRANSFORMS=1 forces the full path (A/B, bisecting). The
    // verify pass below compares the result with the scene's own matrices
    // every frame for a rotating slice (GWS_ECS_VERIFY_ALL=1: all of them).
    static const bool force_full_tf = std::getenv("GWS_ECS_FULL_TRANSFORMS") != nullptr;
    const bool incremental = !force_full_tf && reuse_parents && impl_->have_prev_matrices &&
                             impl_->local.size() == shadow.size() &&
                             impl_->world_mat.size() == shadow.size() &&
                             oop_stamp.size() == shadow.size();
    if (!incremental) {
        next("sync.local_trs");
        // --- System phase A (PARALLEL): LocalToWorld.value = local matrix.
        ecs::parallel_each<ecs::Transform, ecs::LocalToWorld>(
            world, [](ecs::Entity, ecs::Transform& t, ecs::LocalToWorld& l) {
                l.value = compose_trs(t);
            });

        next("sync.hierarchy");
        // --- System phase B (hierarchy resolve): world = parent_world * local.
        // Dependency-ordered, so serial with memoisation here; the real engine
        // parallelises this by depth-wave (noted for step 2). Reads the local
        // matrices written above, writes world matrices back into LocalToWorld.
        //
        // The scratch arrays persist in Impl. This allocated three scene-sized
        // vectors per sync plus a fresh chain vector PER ENTITY -- 20k heap
        // allocations a frame at 20k entities to walk, usually, a chain of one.
        auto& local     = impl_->local;
        auto& world_mat = impl_->world_mat;
        auto& done      = impl_->done;
        auto& chain     = impl_->chain;
        auto& ltw_pool = world.registry().storage<ecs::LocalToWorld>();   // fetched once
        local.resize(shadow.size());
        for (size_t i = 0; i < shadow.size(); ++i)
            local[i] = ltw_pool.get(shadow[i]).value;

        world_mat.resize(shadow.size());
        done.assign(shadow.size(), 0);
        // Iterative resolve up the parent chain (no recursion depth limit risk).
        for (size_t i = 0; i < shadow.size(); ++i) {
            if (done[i]) continue;
            // Walk up to the highest unresolved ancestor, recording the chain.
            chain.clear();
            int cur = static_cast<int>(i);
            // Bounded: a chain longer than the entity count can only be a parent
            // cycle, and an unbounded walk around one never ends -- the editor
            // hangs with no error. A cycle cannot come from a well-formed scene,
            // but a bug that corrupts parent_idx (or a scene that parents A under
            // B under A) should show up as wrong matrices and a warning, not as a
            // frozen editor. The cycle's top is treated as a root.
            bool cycle = false;
            while (cur != -1 && !done[cur]) {
                if (chain.size() > shadow.size()) { cycle = true; break; }
                chain.push_back(cur);
                cur = parent_idx[cur];
            }
            if (cycle) {
                static bool warned = false;
                if (!warned) {
                    warned = true;
                    spdlog::warn("[ecs] parent cycle in the scene hierarchy; resolving it as a root "
                                 "(reported once)");
                }
                cur = -1;
            }
            glm::mat4 acc = (cur == -1) ? glm::mat4(1.0f) : world_mat[cur];
            // Apply from the top of the chain down.
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                acc = acc * local[*it];
                world_mat[*it] = acc;
                done[*it] = 1;
            }
        }
        next("sync.publish");
        // Independent per entity -- its own LocalToWorld element, its own slot --
        // and each write a cache miss, so it goes on the job system above the
        // threshold. The pool was fetched once above; workers only call get().
        {
            auto publish = [&](size_t i) {
                ltw_pool.get(shadow[i]).value = world_mat[i];
                slot_of[i]->world = world_mat[i];
            };
            auto& js = gws::jobs::JobSystem::instance();
            if (js.initialized() && shadow.size() >= kParallelMirrorMin)
                js.parallel_for(0, shadow.size(), [&publish](size_t i) { publish(i); });
            else
                for (size_t i = 0; i < shadow.size(); ++i) publish(i);
        }
        matrices_written_ = shadow.size();
        impl_->have_prev_matrices = true;
    } else {
        next("sync.local_trs");
        const size_t n = shadow.size();
        auto& local     = impl_->local;
        auto& world_mat = impl_->world_mat;
        auto& wst       = impl_->wstate;
        auto& changed   = impl_->changed;
        wst.assign(n, 0);
        changed.clear();
        for (size_t i = 0; i < n; ++i) {
            if (oop_stamp[i] >= impl_->prev_sync_begin || serial[i]) {
                wst[i] = 1;                        // own transform changed
                changed.push_back(static_cast<uint32_t>(i));
            }
        }
        auto& tf_pool  = world.registry().storage<ecs::Transform>();
        auto& ltw_pool = world.registry().storage<ecs::LocalToWorld>();
        auto& js = gws::jobs::JobSystem::instance();
        {
            auto compose = [&](size_t k) {
                const size_t i = changed[k];
                local[i] = compose_trs(tf_pool.get(shadow[i]));
            };
            if (js.initialized() && changed.size() >= kParallelMirrorMin)
                js.parallel_for(0, changed.size(), [&compose](size_t k) { compose(k); });
            else
                for (size_t k = 0; k < changed.size(); ++k) compose(k);
        }

        next("sync.hierarchy");
        // Nothing changed at all (a still scene): every matrix stands.
        if (changed.empty()) {
            matrices_written_ = 0;
        } else {
        // An entity's WORLD matrix changed iff its own transform did or an
        // ancestor's world did. Resolved with the same memoised upward walk
        // as the full path, bounded the same way against a parent cycle.
        auto& chain = impl_->chain;
        for (size_t i = 0; i < n; ++i) {
            if (wst[i] != 0) continue;
            chain.clear();
            int cur = static_cast<int>(i);
            while (cur != -1 && wst[cur] == 0 && chain.size() <= n) {
                chain.push_back(cur);
                cur = parent_idx[cur];
            }
            const uint8_t v = (cur == -1 || chain.size() > n) ? 2 : wst[cur];
            for (int c : chain) wst[c] = v;
        }
        // Recompute, parent before child: every unchanged entity is "done"
        // with last sync's world matrix.
        auto& done = impl_->done;
        done.resize(n);
        changed.clear();
        for (size_t i = 0; i < n; ++i) {
            done[i] = (wst[i] == 2) ? 1 : 0;
            if (wst[i] == 1) changed.push_back(static_cast<uint32_t>(i));
        }
        for (uint32_t i : changed) {
            if (done[i]) continue;
            chain.clear();
            int cur = static_cast<int>(i);
            while (cur != -1 && !done[cur] && chain.size() <= n) {
                chain.push_back(cur);
                cur = parent_idx[cur];
            }
            if (chain.size() > n) cur = -1;          // cycle: resolve as a root
            glm::mat4 acc = (cur == -1) ? glm::mat4(1.0f) : world_mat[cur];
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                acc = acc * local[*it];
                world_mat[*it] = acc;
                done[*it] = 1;
            }
        }

        next("sync.publish");
        {
            auto publish = [&](size_t k) {
                const size_t i = changed[k];
                ltw_pool.get(shadow[i]).value = world_mat[i];
                slot_of[i]->world = world_mat[i];
            };
            if (js.initialized() && changed.size() >= kParallelMirrorMin)
                js.parallel_for(0, changed.size(), [&publish](size_t k) { publish(k); });
            else
                for (size_t k = 0; k < changed.size(); ++k) publish(k);
        }
        matrices_written_ = changed.size();
        }   // !changed.empty()
    }
    impl_->prev_sync_begin = sync_begin;

    next("sync.verify");
    // --- Verify: the ECS world matrix must equal the OOP one. A match
    // validates components + storage + parallel TRS + hierarchy.
    //
    // Every entity when the set changed (including the first sync), otherwise
    // a rotating 1/32 slice: every entity is still checked every 32 frames.
    // A full pass is also recorded in full_checked_/full_verified_, which is
    // what the editor's one-time "ECS shadow live" log reports -- that log
    // runs on whatever frame first reaches it, not necessarily the first sync.
    // Checking all of them every frame cost 2-2.5 ms at 20k entities, in
    // release, to re-confirm a result that only changes when the set or the
    // math does.
    static const bool verify_all = std::getenv("GWS_ECS_VERIFY_ALL") != nullptr;
    const bool   full   = created_ > 0 || destroyed_ > 0;
    const size_t n_ent  = shadow.size();
    const size_t stride = (full || verify_all) ? 1 : 32;
    const size_t start  = (full || n_ent == 0) ? 0 : (impl_->verify_cursor++ % stride);
    for (size_t i = start; i < n_ent; i += stride) {
        ++checked_;
        const glm::mat4 expected = oop_tf[i]->GetWorldMatrix();
        const float* a = &impl_->world_mat[i][0][0];
        const float* b = &expected[0][0];
        float max_diff = 0.0f;
        for (int k = 0; k < 16; ++k)
            max_diff = std::max(max_diff, std::fabs(a[k] - b[k]));
        if (max_diff < 1e-3f) ++verified_;
    }
    if (full) {
        full_checked_  = checked_;
        full_verified_ = verified_;
    }
    if (verify_all && verified_ != checked_)
        spdlog::warn("[ecs] verify: {} of {} world matrices differ from the scene's "
                     "(incremental transforms {})",
                     checked_ - verified_, checked_, incremental ? "on" : "off");
}

}  // namespace schizo::editor
