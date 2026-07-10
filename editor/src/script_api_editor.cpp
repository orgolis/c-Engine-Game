// ============================================================
// Editor ScriptApi implementation — see script_api_editor.h
// ============================================================

#include "script_api_editor.h"
#include "scene_playback_manager.h"

#include "scene.h"
#include "entity.h"
#include "transform.h"
#include "entity_factory.h"
#include "mesh_renderer_component.h"
#include "collider_component.h"
#include "audio_components.h"
#include "physics/jolt_physics.h"   // Stage 4 — Jolt-backed PhysicsWorld

#include <GLFW/glfw3.h>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>
#include <atomic>
#include <string>

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
}

}  // namespace schizo::editor
