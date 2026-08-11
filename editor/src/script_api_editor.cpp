// ============================================================
// Editor ScriptApi implementation — see script_api_editor.h
// ============================================================

#include "script_api_editor.h"
#include "scene_playback_manager.h"
#include "ecs_bridge.h"

#include "scene.h"
#include "entity.h"
#include "transform.h"
#include "entity_factory.h"
#include "mesh_renderer_component.h"
#include "collider_component.h"
#include "audio_components.h"
#include "script_component.h"       // Stage 12 script params (get_param_*)
#include "physics/jolt_physics.h"   // Stage 4 — Jolt-backed PhysicsWorld

// Gameplay ECS (G0–G4) — scripts drive these via the ECS bridge.
#include "ecs/world.h"
#include "ecs/gameplay_attributes.h"
#include "ecs/gameplay_tags.h"
#include "ecs/gameplay_effects.h"
#include "ecs/gameplay_damage.h"
#include "ecs/gameplay_abilities.h"
#include "ecs/gameplay_state_machine.h"
#include "ecs/gameplay_progression.h"
#include "ecs/gameplay_skills.h"
#include "ecs/gameplay_items.h"
#include "ecs/gameplay_inventory.h"
#include "ecs/gameplay_interaction.h"
#include "ecs/gameplay_weapons.h"
#include "ecs/gameplay_vehicles.h"
#include "ecs/gameplay_survival.h"
#include "ecs/gameplay_events.h"        // GameplayEvent (emit_event)

#include <GLFW/glfw3.h>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cstdint>

namespace schizo::editor {

namespace {

inline EditorScriptCtx* C(void* ctx) { return static_cast<EditorScriptCtx*>(ctx); }

std::shared_ptr<schizo::scene::Entity> ent_of(void* ctx, uint32_t e) {
    auto* c = C(ctx);
    return (c->scene && e) ? c->scene->GetEntityById(e) : nullptr;
}

// ---- logging ----
void api_log(void*, const char* msg) { spdlog::info("[script] {}", msg); }

// ---- entities / transforms ----
uint32_t api_find(void* ctx, const char* name) {
    auto* c = C(ctx);
    if (!c->scene || !name) return 0;
    auto e = c->scene->GetEntityByName(name);
    return e ? e->GetId() : 0;
}
bool api_get_pos(void* ctx, uint32_t e, float out[3]) {
    auto ent = ent_of(ctx, e);
    if (!ent) return false;
    const glm::vec3 p = ent->GetTransform()->GetWorldPosition();
    out[0] = p.x; out[1] = p.y; out[2] = p.z;
    return true;
}
void api_set_pos(void* ctx, uint32_t e, const float p[3]) {
    if (auto ent = ent_of(ctx, e))
        ent->GetTransform()->SetWorldPosition(glm::vec3(p[0], p[1], p[2]));
}
bool api_get_rot(void* ctx, uint32_t e, float out[3]) {
    auto ent = ent_of(ctx, e);
    if (!ent) return false;
    const glm::vec3 r = glm::degrees(glm::eulerAngles(ent->GetTransform()->GetWorldRotation()));
    out[0] = r.x; out[1] = r.y; out[2] = r.z;
    return true;
}
void api_set_rot(void* ctx, uint32_t e, const float deg[3]) {
    if (auto ent = ent_of(ctx, e))
        ent->GetTransform()->SetWorldRotation(
            glm::quat(glm::radians(glm::vec3(deg[0], deg[1], deg[2]))));
}
bool api_get_scale(void* ctx, uint32_t e, float out[3]) {
    auto ent = ent_of(ctx, e);
    if (!ent) return false;
    const glm::vec3 s = ent->GetTransform()->GetLocalScale();
    out[0] = s.x; out[1] = s.y; out[2] = s.z;
    return true;
}
void api_set_scale(void* ctx, uint32_t e, const float s[3]) {
    if (auto ent = ent_of(ctx, e))
        ent->GetTransform()->SetLocalScale(glm::vec3(s[0], s[1], s[2]));
}

// ---- input ----
bool api_key_down(void* ctx, int key) {
    auto* c = C(ctx);
    return c->window && glfwGetKey(c->window, key) == GLFW_PRESS;
}
bool api_mouse_down(void* ctx, int button) {
    auto* c = C(ctx);
    return c->window && glfwGetMouseButton(c->window, button) == GLFW_PRESS;
}
void api_mouse_delta(void* ctx, float out[2]) {
    auto* c = C(ctx);
    out[0] = c->mouse_dx; out[1] = c->mouse_dy;
}

// ---- spawn / destroy ----
uint32_t api_spawn(void* ctx, int shape, const float pos[3], float size,
                   const float rgba[4], int dynamic) {
    auto* c = C(ctx);
    if (!c->scene) return 0;
    static std::atomic<uint32_t> counter{0};
    const std::string name = "script_" + std::string(shape == 1 ? "sphere_" : "cube_") +
                             std::to_string(counter++);
    const glm::vec4 col(rgba[0], rgba[1], rgba[2], rgba[3]);
    std::shared_ptr<schizo::scene::Entity> ent;
    if (shape == 1)
        ent = schizo::scene::EntityFactory::CreateSphere(c->scene, name, size * 0.5f, col);
    else
        ent = schizo::scene::EntityFactory::CreateCube(c->scene, name, size, col);
    if (!ent) return 0;
    ent->GetTransform()->SetWorldPosition(glm::vec3(pos[0], pos[1], pos[2]));
    if (dynamic) {
        auto colc = ent->GetComponent<schizo::scene::ColliderComponent>();
        if (!colc)
            colc = ent->AddComponent<schizo::scene::ColliderComponent>(
                shape == 1 ? schizo::scene::ColliderShape::Sphere
                           : schizo::scene::ColliderShape::Box);
        if (colc) {
            colc->SetDynamic(true);
            colc->SetMass(1.0f);
            if (shape == 1) colc->SetRadius(size * 0.5f);
            else            colc->SetHalfExtents(glm::vec3(size * 0.5f));
        }
        // Live body when spawned during play (the world was built at play start).
        if (c->playback && c->playback->IsPlaying())
            c->playback->AddRuntimeBody(ent);
    }
    return ent->GetId();
}
void api_destroy(void* ctx, uint32_t e) {
    auto* c = C(ctx);
    auto ent = ent_of(ctx, e);
    if (!ent) return;
    if (c->playback) c->playback->RemoveBodyForEntity(e);
    c->scene->RemoveEntity(ent);
}

// ---- physics ----
void api_set_velocity(void* ctx, uint32_t e, const float v[3]) {
    auto* c = C(ctx);
    if (!c->playback) return;
    auto* pw = c->playback->GetPhysicsWorld();
    const uint32_t body = c->playback->BodyForEntity(e);
    if (pw && body != 0xFFFFFFFFu)
        pw->set_linear_velocity(body, glm::vec3(v[0], v[1], v[2]));
}
void api_add_impulse(void* ctx, uint32_t e, const float i[3]) {
    auto* c = C(ctx);
    if (!c->playback) return;
    auto* pw = c->playback->GetPhysicsWorld();
    const uint32_t body = c->playback->BodyForEntity(e);
    if (pw && body != 0xFFFFFFFFu)
        pw->add_impulse(body, glm::vec3(i[0], i[1], i[2]));
}
bool api_raycast(void* ctx, const float origin[3], const float dir[3], float max_dist,
                 float out_pos[3], uint32_t* out_entity) {
    auto* c = C(ctx);
    if (!c->playback) return false;
    auto* pw = c->playback->GetPhysicsWorld();
    if (!pw) return false;
    const auto hit = pw->raycast(glm::vec3(origin[0], origin[1], origin[2]),
                                 glm::vec3(dir[0], dir[1], dir[2]), max_dist);
    if (!hit.hit) return false;
    out_pos[0] = hit.position.x; out_pos[1] = hit.position.y; out_pos[2] = hit.position.z;
    if (out_entity) *out_entity = c->playback->EntityForBody(hit.body);
    return true;
}

// ---- rendering ----
void api_set_color(void* ctx, uint32_t e, const float rgba[4]) {
    if (auto ent = ent_of(ctx, e))
        if (auto mr = ent->GetComponent<schizo::scene::MeshRendererComponent>())
            mr->SetColor(glm::vec4(rgba[0], rgba[1], rgba[2], rgba[3]));
}
void api_set_emissive(void* ctx, uint32_t e, const float rgb[3], float intensity) {
    if (auto ent = ent_of(ctx, e))
        if (auto mr = ent->GetComponent<schizo::scene::MeshRendererComponent>()) {
            mr->SetEmissive(glm::vec3(rgb[0], rgb[1], rgb[2]));
            mr->SetEmissiveIntensity(intensity);
        }
}

// ---- audio ----
void api_audio_play(void* ctx, uint32_t e) {
    if (auto ent = ent_of(ctx, e))
        if (auto src = ent->GetComponent<schizo::scene::AudioSourceComponent>())
            src->SetPlaying(true);
}
void api_audio_stop(void* ctx, uint32_t e) {
    if (auto ent = ent_of(ctx, e))
        if (auto src = ent->GetComponent<schizo::scene::AudioSourceComponent>())
            src->SetPlaying(false);
}

// ---- gameplay (G0–G4): map a scene id -> its ECS entity via the bridge ----
namespace ecs = schizo::ecs;

// Resolve a script entity id to (world, ecs entity). Returns false if unmapped.
bool gp_entity(void* ctx, uint32_t e, ecs::World*& w, ecs::Entity& out) {
    auto* c = C(ctx);
    if (!c->bridge || !c->scene || !e) return false;
    auto ent = c->scene->GetEntityById(e);
    if (!ent) return false;
    const uint32_t id = c->bridge->ecs_entity_id(ent->GetTransform());
    if (id == kNoEcsEntity) return false;
    w = &c->bridge->world();
    out = static_cast<ecs::Entity>(id);
    return true;
}

float api_get_attribute(void* ctx, uint32_t e, const char* name) {
    ecs::World* w; ecs::Entity ent;
    if (!gp_entity(ctx, e, w, ent) || !name) return 0.0f;
    auto* a = w->try_get<ecs::AttributeSet>(ent);
    return a ? a->get(name) : 0.0f;
}
void api_set_attribute(void* ctx, uint32_t e, const char* name, float value) {
    ecs::World* w; ecs::Entity ent;
    if (!gp_entity(ctx, e, w, ent) || !name) return;
    if (auto* a = w->try_get<ecs::AttributeSet>(ent)) a->set(name, value);
}
void api_adjust_attribute(void* ctx, uint32_t e, const char* name, float delta) {
    ecs::World* w; ecs::Entity ent;
    if (!gp_entity(ctx, e, w, ent) || !name) return;
    if (auto* a = w->try_get<ecs::AttributeSet>(ent)) a->adjust(name, delta);
}
bool api_has_tag(void* ctx, uint32_t e, const char* tag) {
    ecs::World* w; ecs::Entity ent;
    if (!gp_entity(ctx, e, w, ent) || !tag) return false;
    auto* g = w->try_get<ecs::GameplayTags>(ent);
    return g && g->has(tag);
}
void api_add_tag(void* ctx, uint32_t e, const char* tag) {
    ecs::World* w; ecs::Entity ent;
    if (!gp_entity(ctx, e, w, ent) || !tag) return;
    if (!w->has<ecs::GameplayTags>(ent)) w->add<ecs::GameplayTags>(ent, ecs::GameplayTags{});
    w->get<ecs::GameplayTags>(ent).add(tag);
}
void api_remove_tag(void* ctx, uint32_t e, const char* tag) {
    ecs::World* w; ecs::Entity ent;
    if (!gp_entity(ctx, e, w, ent) || !tag) return;
    if (auto* g = w->try_get<ecs::GameplayTags>(ent)) g->remove_exact(tag);
}
float api_apply_damage(void* ctx, uint32_t e, float amount, const char* type) {
    ecs::World* w; ecs::Entity ent;
    if (!gp_entity(ctx, e, w, ent)) return 0.0f;
    return ecs::apply_damage(*w, ent, ecs::DamageInfo{amount, type ? type : ""});
}
void api_apply_heal(void* ctx, uint32_t e, const char* attribute, float amount) {
    ecs::World* w; ecs::Entity ent;
    if (!gp_entity(ctx, e, w, ent)) return;
    ecs::apply_effect(*w, ent, ecs::make_instant("Heal", attribute ? attribute : "Health", amount));
}
bool api_activate_ability(void* ctx, uint32_t e, int index, uint32_t target) {
    ecs::World* w; ecs::Entity caster;
    if (!gp_entity(ctx, e, w, caster) || index < 0) return false;
    ecs::Entity tgt = ecs::null_entity;
    if (target) { ecs::World* tw; ecs::Entity te; if (gp_entity(ctx, target, tw, te) && tw == w) tgt = te; }
    return ecs::try_activate_ability(*w, caster, static_cast<size_t>(index), tgt);
}
void api_grant_xp(void* ctx, uint32_t e, float amount) {
    ecs::World* w; ecs::Entity ent;
    if (!gp_entity(ctx, e, w, ent)) return;
    ecs::grant_xp(*w, ent, amount, &C(ctx)->bridge->events());
}
int api_get_level(void* ctx, uint32_t e) {
    ecs::World* w; ecs::Entity ent;
    if (!gp_entity(ctx, e, w, ent)) return 0;
    auto* p = w->try_get<ecs::Progression>(ent);
    return p ? p->level : 0;
}
bool api_unlock_skill(void* ctx, uint32_t e, const char* node_id) {
    ecs::World* w; ecs::Entity ent;
    if (!gp_entity(ctx, e, w, ent) || !node_id) return false;
    return ecs::unlock_skill(*w, ent, node_id, &C(ctx)->bridge->events());
}
bool api_send_state_event(void* ctx, uint32_t e, const char* event) {
    ecs::World* w; ecs::Entity ent;
    if (!gp_entity(ctx, e, w, ent) || !event) return false;
    auto* sm = w->try_get<ecs::StateMachine>(ent);
    return sm && ecs::send_state_event(*w, ent, *sm, event, &C(ctx)->bridge->events());
}
bool api_in_state(void* ctx, uint32_t e, const char* state) {
    ecs::World* w; ecs::Entity ent;
    if (!gp_entity(ctx, e, w, ent) || !state) return false;
    auto* sm = w->try_get<ecs::StateMachine>(ent);
    return sm && sm->current == state;
}
int api_add_item(void* ctx, uint32_t e, const char* def_id, int qty) {
    ecs::World* w; ecs::Entity ent;
    if (!gp_entity(ctx, e, w, ent) || !def_id) return qty;
    if (!w->has<ecs::Inventory>(ent)) w->add<ecs::Inventory>(ent, ecs::Inventory{});
    return ecs::inventory_add(w->get<ecs::Inventory>(ent), ecs::ItemInstance{def_id, qty, 0, {}});
}
int api_item_count(void* ctx, uint32_t e, const char* def_id) {
    ecs::World* w; ecs::Entity ent;
    if (!gp_entity(ctx, e, w, ent) || !def_id) return 0;
    auto* inv = w->try_get<ecs::Inventory>(ent);
    return inv ? ecs::inventory_count(*inv, def_id) : 0;
}
bool api_equip_item(void* ctx, uint32_t e, const char* def_id) {
    ecs::World* w; ecs::Entity ent;
    if (!gp_entity(ctx, e, w, ent) || !def_id) return false;
    if (auto* inv = w->try_get<ecs::Inventory>(ent)) ecs::inventory_remove(*inv, def_id, 1);
    return ecs::equip_item(*w, ent, ecs::ItemInstance{def_id, 1, 0, {}}, &C(ctx)->bridge->events());
}
bool api_use_item(void* ctx, uint32_t e, const char* def_id) {
    ecs::World* w; ecs::Entity ent;
    if (!gp_entity(ctx, e, w, ent) || !def_id) return false;
    auto* inv = w->try_get<ecs::Inventory>(ent);
    if (!inv) return false;
    for (size_t i = 0; i < inv->items.size(); ++i)
        if (inv->items[i].def_id == def_id)
            return ecs::use_item(*w, ent, *inv, i, &C(ctx)->bridge->events());
    return false;
}
bool api_interact(void* ctx, uint32_t e) {
    ecs::World* w; ecs::Entity ent;
    if (!gp_entity(ctx, e, w, ent)) return false;
    return ecs::try_interact(*w, ent, &C(ctx)->bridge->events());
}
bool api_fire_weapon(void* ctx, uint32_t e) {
    ecs::World* w; ecs::Entity ent;
    if (!gp_entity(ctx, e, w, ent)) return false;
    auto* ws = w->try_get<ecs::WeaponState>(ent);
    auto* tf = w->try_get<ecs::Transform>(ent);
    if (!ws || !tf) return false;
    const glm::vec3 fwd = glm::normalize(tf->rotation * glm::vec3(0.0f, 0.0f, 1.0f));   // Transform forward (+Z)
    const glm::vec3 origin = tf->position + glm::vec3(0.0f, 1.0f, 0.0f);                // ~eye height
    return ecs::fire_weapon(*w, ent, origin, fwd, *ws, &C(ctx)->bridge->events(),
                            static_cast<uint64_t>(ws->ammo_in_mag) + 1u);
}
bool api_reload_weapon(void* ctx, uint32_t e) {
    ecs::World* w; ecs::Entity ent;
    if (!gp_entity(ctx, e, w, ent)) return false;
    auto* ws = w->try_get<ecs::WeaponState>(ent);
    if (!ws) return false;
    const float before = ws->reloading;
    ecs::reload_weapon(*ws);
    return ws->reloading > 0.0f && before <= 0.0f;
}
void api_drive(void* ctx, uint32_t e, float throttle, float steer, int brake, int boost) {
    ecs::World* w; ecs::Entity ent;
    if (!gp_entity(ctx, e, w, ent)) return;
    ecs::VehicleInput in;
    in.throttle = throttle; in.steer = steer; in.brake = brake != 0; in.boost = boost != 0;
    ecs::drive_vehicle(*w, ent, in, C(ctx)->dt);
}
bool api_toggle_flashlight(void* ctx, uint32_t e) {
    ecs::World* w; ecs::Entity ent;
    if (!gp_entity(ctx, e, w, ent)) return false;
    return ecs::toggle_flashlight(*w, ent);
}

// ---- script public parameters (Stage 12 script fields) ----
// Resolve entity `e`'s ScriptComponent authored override for `name` (nullptr if
// absent). Values are string-typed and parsed by the caller.
const std::string* param_of(void* ctx, uint32_t e, const char* name) {
    if (!name) return nullptr;
    auto ent = ent_of(ctx, e);
    if (!ent) return nullptr;
    auto sc = ent->GetComponent<schizo::scene::ScriptComponent>();
    return sc ? sc->FindParam(name) : nullptr;
}
float api_get_param_float(void* ctx, uint32_t e, const char* name, float def) {
    const std::string* v = param_of(ctx, e, name);
    return v ? static_cast<float>(std::atof(v->c_str())) : def;
}
int api_get_param_int(void* ctx, uint32_t e, const char* name, int def) {
    const std::string* v = param_of(ctx, e, name);
    return v ? std::atoi(v->c_str()) : def;
}
bool api_get_param_bool(void* ctx, uint32_t e, const char* name, int def) {
    const std::string* v = param_of(ctx, e, name);
    if (!v) return def != 0;
    return (*v == "1" || *v == "true" || *v == "True");
}
int api_get_param_string(void* ctx, uint32_t e, const char* name,
                         char* out, int out_size, const char* def) {
    if (!out || out_size <= 0) return 0;
    const std::string* v = param_of(ctx, e, name);
    const std::string s  = v ? *v : (def ? std::string(def) : std::string());
    const int n = std::min(static_cast<int>(s.size()), out_size - 1);
    std::memcpy(out, s.data(), static_cast<size_t>(n));
    out[n] = '\0';
    return n;
}

// ---- world flags + events (bridge scripts <-> the scene logic graph) ----
int api_get_flag(void* ctx, const char* key) {
    auto* c = C(ctx);
    return (c->bridge && key) ? c->bridge->world_flag(key) : 0;
}
void api_set_flag(void* ctx, const char* key, int value) {
    auto* c = C(ctx);
    if (c->bridge && key) c->bridge->set_world_flag(key, value);
}
void api_emit_event(void* ctx, const char* name) {
    auto* c = C(ctx);
    if (c->bridge && name) { ecs::GameplayEvent ev; ev.name = name; c->bridge->events().publish(ev); }
}

// ---- spatial helpers ----
float api_distance(void* ctx, uint32_t a, uint32_t b) {
    auto ea = ent_of(ctx, a), eb = ent_of(ctx, b);
    if (!ea || !eb) return -1.0f;
    return glm::distance(ea->GetTransform()->GetWorldPosition(),
                         eb->GetTransform()->GetWorldPosition());
}
void api_translate(void* ctx, uint32_t e, const float d[3]) {
    if (auto ent = ent_of(ctx, e)) {
        auto* t = ent->GetTransform();
        t->SetWorldPosition(t->GetWorldPosition() + glm::vec3(d[0], d[1], d[2]));
    }
}
bool api_get_forward(void* ctx, uint32_t e, float out[3]) {
    auto ent = ent_of(ctx, e);
    if (!ent) return false;
    const glm::vec3 f = glm::normalize(ent->GetTransform()->GetWorldRotation() * glm::vec3(0, 0, -1));
    out[0] = f.x; out[1] = f.y; out[2] = f.z;
    return true;
}

}  // namespace

void bind_editor_script_api(ScriptApi& api, EditorScriptCtx* ctx) {
    api.ctx                = ctx;
    api.log                = &api_log;
    api.find_entity        = &api_find;
    api.get_position       = &api_get_pos;
    api.set_position       = &api_set_pos;
    api.get_rotation_euler = &api_get_rot;
    api.set_rotation_euler = &api_set_rot;
    api.get_scale          = &api_get_scale;
    api.set_scale          = &api_set_scale;
    api.key_down           = &api_key_down;
    api.mouse_down         = &api_mouse_down;
    api.mouse_delta        = &api_mouse_delta;
    api.spawn_primitive    = &api_spawn;
    api.destroy_entity     = &api_destroy;
    api.set_velocity       = &api_set_velocity;
    api.add_impulse        = &api_add_impulse;
    api.raycast            = &api_raycast;
    api.set_color          = &api_set_color;
    api.set_emissive       = &api_set_emissive;
    api.audio_play         = &api_audio_play;
    api.audio_stop         = &api_audio_stop;

    // ---- gameplay (G0–G4) ----
    api.get_attribute      = &api_get_attribute;
    api.set_attribute      = &api_set_attribute;
    api.adjust_attribute   = &api_adjust_attribute;
    api.has_tag            = &api_has_tag;
    api.add_tag            = &api_add_tag;
    api.remove_tag         = &api_remove_tag;
    api.apply_damage       = &api_apply_damage;
    api.apply_heal         = &api_apply_heal;
    api.activate_ability   = &api_activate_ability;
    api.grant_xp           = &api_grant_xp;
    api.get_level          = &api_get_level;
    api.unlock_skill       = &api_unlock_skill;
    api.send_state_event   = &api_send_state_event;
    api.in_state           = &api_in_state;
    api.add_item           = &api_add_item;
    api.item_count         = &api_item_count;
    api.equip_item         = &api_equip_item;
    api.use_item           = &api_use_item;
    api.interact           = &api_interact;
    api.fire_weapon        = &api_fire_weapon;
    api.reload_weapon      = &api_reload_weapon;
    api.drive              = &api_drive;
    api.toggle_flashlight  = &api_toggle_flashlight;
    api.get_param_float    = &api_get_param_float;
    api.get_param_int      = &api_get_param_int;
    api.get_param_bool     = &api_get_param_bool;
    api.get_param_string   = &api_get_param_string;
    api.get_flag           = &api_get_flag;
    api.set_flag           = &api_set_flag;
    api.emit_event         = &api_emit_event;
    api.distance           = &api_distance;
    api.translate          = &api_translate;
    api.get_forward        = &api_get_forward;
}

}  // namespace schizo::editor
