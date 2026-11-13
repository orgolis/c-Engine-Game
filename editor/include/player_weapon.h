#pragma once
// ============================================================================
// player_weapon -- the player's gun in Play mode, and what its shots leave.
//
// PlayerWeapon reads the mouse and keys (left: fire, right: aim, R: reload),
// fires through ecs::trigger_weapon from the camera, lets rounds stop on level
// geometry (the physics world's ray cast), hands recoil to the view, and keeps
// what the HUD shows: the crosshair's spread, a hit marker, the ammunition.
//
// ShotEffects draws what the shots leave behind: a muzzle flash for a frame
// or two, tracers that streak along the round's path, and marks where rounds
// hit the world, fading after a while. They are draw items appended each
// frame, like foliage; nothing about them is in the scene.
// ============================================================================

#include "ecs/gameplay_item_file.h"   // weapon blocks in .items files
#include "ecs/gameplay_weapons.h"
#include "ecs_bridge.h"
#include "material_cache.h"
#include "primitive_meshes.h"
#include "scene_playback_manager.h"
#include "physics/jolt_physics.h"    // rounds stop on level geometry

#include "vulkan/vulkan_render_graph.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

namespace schizo::editor {

class ShotEffects {
public:
    struct Effect {
        enum class Kind : uint8_t { Flash, Tracer, Impact } kind = Kind::Flash;
        glm::vec3 a{0.0f}, b{0.0f}, normal{0.0f, 1.0f, 0.0f};
        glm::vec3 color{1.0f};
        float     size = 0.1f;
        float     age = 0.0f, life = 0.05f;
    };

    void add(const std::vector<schizo::ecs::ShotTrace>& shots, const schizo::ecs::WeaponDef& wd, const glm::vec3& muzzle) {
        if (wd.muzzle_flash && !shots.empty())
            effects_.push_back({Effect::Kind::Flash, muzzle, muzzle, glm::vec3(0, 1, 0), glm::vec3(1.0f, 0.55f, 0.15f), 0.07f, 0.0f, 0.04f});
        for (const auto& s : shots) {
            if (s.tracer) {
                const float len = glm::length(s.end - muzzle);
                effects_.push_back({Effect::Kind::Tracer, muzzle, s.end, glm::vec3(0, 1, 0), wd.tracer_color, 0.04f, 0.0f,
                                    std::max(kTracerMinLife, len / kTracerSpeed)});
            }
            if (s.hit == schizo::ecs::ShotTrace::Hit::World && wd.impact_size > 0.0f) {
                effects_.push_back({Effect::Kind::Impact, s.end, s.end, s.normal, glm::vec3(0.05f), wd.impact_size, 0.0f, kImpactLife});
                ++impacts_;
            }
        }
        // Keep the newest marks only.
        while (impacts_ > kMaxImpacts) {
            for (auto it = effects_.begin(); it != effects_.end(); ++it)
                if (it->kind == Effect::Kind::Impact) {
                    effects_.erase(it);
                    --impacts_;
                    break;
                }
        }
    }

    void update(float dt) {
        for (Effect& e : effects_) e.age += dt;
        const size_t before = effects_.size();
        effects_.erase(std::remove_if(effects_.begin(), effects_.end(), [](const Effect& e) { return e.age >= e.life; }), effects_.end());
        if (effects_.size() != before) {
            impacts_ = 0;
            for (const Effect& e : effects_) impacts_ += e.kind == Effect::Kind::Impact ? 1 : 0;
        }
    }
    void clear() {
        effects_.clear();
        impacts_ = 0;
    }
    size_t count() const { return effects_.size(); }

    void append(const PrimitiveMeshCache& prims, MaterialGpuCache& mats, gws::renderer::gpu::VulkanDevice* device,
                VkDescriptorSetLayout layout, VkDescriptorPool pool, gws::renderer::gpu::TextureManager* textures,
                std::vector<gws::renderer::gpu::DrawItem>& out) {
        auto glow = [&](const glm::vec3& c, float intensity) {
            gws::assets::MaterialDesc d;
            d.name = "shot_glow";
            d.base_color = glm::vec4(c, 1.0f);
            d.emissive = c;
            d.emissive_intensity = intensity;
            return mats.get_or_create(d, device, layout, pool, textures);
        };
        gws::assets::MaterialDesc mark;
        mark.name = "shot_mark";
        mark.base_color = glm::vec4(0.04f, 0.04f, 0.04f, 1.0f);
        mark.roughness = 1.0f;
        gws::renderer::gpu::Material* mark_mat = mats.get_or_create(mark, device, layout, pool, textures);
        for (const Effect& e : effects_) {
            gws::renderer::gpu::DrawItem di;
            switch (e.kind) {
                case Effect::Kind::Flash: {
                    di.mesh = prims.sphere.get();
                    di.material = glow(e.color, 8.0f);
                    const float s = e.size * (1.0f - 0.5f * e.age / e.life);
                    di.model = glm::scale(glm::translate(glm::mat4(1.0f), e.a), glm::vec3(s));
                    break;
                }
                case Effect::Kind::Tracer: {
                    // A streak a few metres long, travelling along the path.
                    const glm::vec3 path = e.b - e.a;
                    const float     len = glm::length(path);
                    if (len < 1e-3f) continue;
                    const glm::vec3 dir = path / len;
                    // It travels the whole path over its life, so even a close
                    // shot shows for a few frames.
                    const float     head = std::min(len, len * e.age / std::max(1e-4f, e.life) + 1.0f);
                    const float     streak = std::min(4.0f, head);
                    const glm::vec3 mid = e.a + dir * (head - streak * 0.5f);
                    di.mesh = prims.cube.get();
                    di.material = glow(e.color, 25.0f);
                    di.model = glm::translate(glm::mat4(1.0f), mid) * basis_along(dir) *
                               glm::scale(glm::mat4(1.0f), glm::vec3(e.size, e.size, streak));
                    break;
                }
                case Effect::Kind::Impact: {
                    // A dark disc lying on the surface, fading out at the end.
                    const float fade = std::min(1.0f, (e.life - e.age) / 2.0f);
                    if (fade <= 0.0f) continue;
                    di.mesh = prims.cylinder.get();
                    di.material = mark_mat;
                    di.model = glm::translate(glm::mat4(1.0f), e.a + e.normal * 0.01f) * basis_up(e.normal) *
                               glm::scale(glm::mat4(1.0f), glm::vec3(e.size * fade, 0.01f, e.size * fade));
                    break;
                }
            }
            di.casts_shadow = false;   // light and marks, not objects
            if (di.mesh && di.material) out.push_back(di);
        }
    }

private:
    static constexpr float  kTracerSpeed = 350.0f;   // metres per second, slower than a real round so it can be seen
    static constexpr float  kTracerMinLife = 0.07f;  // ... and never gone in under four frames
    static constexpr float  kImpactLife = 12.0f;
    static constexpr size_t kMaxImpacts = 64;

    /// Rotation taking +Z to `dir`.
    static glm::mat4 basis_along(const glm::vec3& dir) {
        const glm::vec3 up = std::fabs(dir.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        const glm::vec3 x = glm::normalize(glm::cross(up, dir)), y = glm::cross(dir, x);
        glm::mat4 m(1.0f);
        m[0] = glm::vec4(x, 0); m[1] = glm::vec4(y, 0); m[2] = glm::vec4(dir, 0);
        return m;
    }
    /// Rotation taking +Y to `n`.
    static glm::mat4 basis_up(const glm::vec3& n) {
        const glm::vec3 ref = std::fabs(n.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1);
        const glm::vec3 z = glm::normalize(glm::cross(ref, n)), x = glm::cross(n, z);
        glm::mat4 m(1.0f);
        m[0] = glm::vec4(x, 0); m[1] = glm::vec4(n, 0); m[2] = glm::vec4(z, 0);
        return m;
    }

    std::deque<Effect> effects_;
    size_t             impacts_ = 0;
};

class PlayerWeapon {
public:
    struct Input {
        bool fire_pressed = false, fire_held = false, aim_held = false, reload_pressed = false;
    };
    struct Hud {
        bool  armed = false;
        float spread_deg = 0.0f;          // the crosshair's gap
        float view_zoom = 1.0f;           // field-of-view scale while aiming
        bool  aiming = false;             // the aim button is held (a camera rig moves in)
        float hit_marker = 0.0f;          // seconds the hit marker still shows
        int   ammo = 0, reserve = 0;
        bool  reloading = false;
        const char* name = "";
    };
    const Hud& hud() const { return hud_; }

    /// One Play frame. `camera` is the view the player looks through.
    void update(float dt, ScenePlaybackManager& play, EcsSceneBridge& bridge, const glm::vec3& cam_pos, const glm::vec3& cam_fwd,
                const Input& in, ShotEffects& fx) {
        namespace ecs = schizo::ecs;
        hud_.armed = false;
        hud_.aiming = false;
        hud_.hit_marker = std::max(0.0f, hud_.hit_marker - dt);
        auto* player = play.GetPlayerEntity();
        if (!player || !play.IsPlaying() || play.IsPaused()) return;
        const uint32_t id = bridge.ecs_entity_id(player->GetTransform());
        ecs::World&    w = bridge.world();
        const ecs::Entity e = static_cast<ecs::Entity>(id);
        if (id == kNoEcsEntity || !w.valid(e)) return;
        auto* ws = w.try_get<ecs::WeaponState>(e);
        const ecs::WeaponDef* wd = ws ? ecs::find_weapon(ws->weapon_id) : nullptr;
        if (!ws || !wd) return;

        if (auto* inv = w.try_get<ecs::Inventory>(e)) ecs::sync_ammo_with_inventory(*ws, *inv);
        ws->aiming = in.aim_held;
        if (in.reload_pressed) ecs::reload_weapon(*ws);

        // Rounds stop on level geometry: the physics world's ray. Its hits
        // carry no normal, so two more rays a hand's width apart give one.
        auto* pw = play.GetPhysicsWorld();
        const ecs::WorldRayFn world = [pw](const glm::vec3& o, const glm::vec3& d, float max_t, float& t, glm::vec3& n) {
            if (!pw) return false;
            const auto hit = pw->raycast(o, d, max_t);
            if (!hit.hit) return false;
            t = hit.distance;
            const glm::vec3 up = std::fabs(d.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            const glm::vec3 sx = glm::normalize(glm::cross(d, up)) * 0.05f, sy = glm::normalize(glm::cross(sx, d)) * 0.05f;
            const auto h1 = pw->raycast(o + sx, d, max_t + 1.0f), h2 = pw->raycast(o + sy, d, max_t + 1.0f);
            n = -d;
            if (h1.hit && h2.hit) {
                glm::vec3 c = glm::cross(h1.position - hit.position, h2.position - hit.position);
                if (glm::dot(c, c) > 1e-10f) {
                    c = glm::normalize(c);
                    n = glm::dot(c, d) < 0.0f ? c : -c;
                }
            }
            return true;
        };
        std::vector<ecs::ShotTrace> shots;
        const glm::vec3 fwd = glm::normalize(cam_fwd);
        const glm::vec3 origin = cam_pos + fwd * 0.3f;
        const int fired = ecs::trigger_weapon(w, e, origin, fwd, *ws, in.fire_pressed, in.fire_held, &bridge.events(),
                                              ++seed_, &world, &shots);
        if (fired > 0) {
            // The muzzle sits a little right of and below the eye.
            const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
            const glm::vec3 muzzle = cam_pos + fwd * 0.8f + right * 0.18f - glm::vec3(0, 0.15f, 0);
            fx.add(shots, *wd, muzzle);
            for (const auto& s : shots)
                if (s.hit == ecs::ShotTrace::Hit::Actor) hud_.hit_marker = 0.15f;
        }
        // Recoil: degrees to the same path as mouse movement (radians / sensitivity).
        float kp = 0.0f, ky = 0.0f;
        ecs::take_recoil(*ws, kp, ky);
        if (kp != 0.0f || ky != 0.0f) {
            const float per_px = 0.0025f;
            // Mouse Y down looks down, so an upward kick is a negative Y delta.
            play.OnMouseDelta(glm::radians(ky) / per_px, -glm::radians(kp) / per_px);
        }

        hud_.armed = true;
        hud_.spread_deg = ecs::current_spread(*wd, *ws);
        hud_.view_zoom = 1.0f + (wd->ads_fov_scale - 1.0f) * ws->aim;
        hud_.aiming = ws->aiming;
        hud_.ammo = ws->ammo_in_mag;
        hud_.reserve = ws->reserve;
        hud_.reloading = ws->reloading > 0.0f;
        hud_.name = wd->name.c_str();
    }

private:
    Hud      hud_;
    uint64_t seed_ = 1;
};

}  // namespace schizo::editor
