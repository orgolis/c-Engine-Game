#pragma once

// ============================================================================
// The player's abilities in Play (C5): keys 1..8 cast what is on the player's
// Ability Bar, their effects (play_effects.h) show the wind-up at the hand, the
// cast, a beam or a flight and where each lands, and the HUD shows the bar with
// its cooldowns and the cast in progress.
// ============================================================================
//
// The rules are ecs/gameplay_ability_cast.h (checked by gameplay_check); this
// is input, the camera's aim, level geometry and the effects.

#include "ecs/gameplay_ability_cast.h"
#include "ecs/gameplay_movement.h"
#include "ecs_bridge.h"
#include "player_weapon.h"           // ShotEffects, physics_world_ray
#include "scene_playback_manager.h"
#include "physics/jolt_physics.h"

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace schizo::editor {

class PlayerAbilities {
public:
    struct Input {
        int  pressed = -1;    // slot whose key went down this frame
        bool held[8] = {};    // which slot keys are down
    };
    struct Slot {
        std::string name;
        float cooldown = 0.0f, cooldown_total = 0.0f;
        bool  ready = false;
    };
    struct Hud {
        bool  shown = false;
        std::vector<Slot> slots;
        int   casting = -1;
        float cast_progress = 0.0f;   // 0..1, wind-up or channel
        bool  channelling = false;
        std::string cast_name;
    };
    const Hud& hud() const { return hud_; }

    void update(float dt, ScenePlaybackManager& play, EcsSceneBridge& bridge, const glm::vec3& cam_pos,
                const glm::vec3& cam_fwd, const Input& in, ShotEffects& /*fx*/, PlayEffects* pfx = nullptr) {
        namespace ecs = schizo::ecs;
        hud_.shown = false;
        auto* player = play.GetPlayerEntity();
        if (!player || !play.IsPlaying() || play.IsPaused()) return;
        const uint32_t id = bridge.ecs_entity_id(player->GetTransform());
        ecs::World& w = bridge.world();
        const ecs::Entity e = static_cast<ecs::Entity>(id);
        if (id == kNoEcsEntity || !w.valid(e)) return;
        // Abilities asked for by items (`casts`): they go off at once, from the view.
        if (auto* req = w.try_get<ecs::AbilityRequests>(e); req && !req->ids.empty()) {
            const std::vector<std::string> ids = std::move(req->ids);
            req->ids.clear();
            const ecs::WorldRayFn world = physics_world_ray(play.GetPhysicsWorld());
            for (const auto& aid : ids) {
                const ecs::AbilityDef* d = ecs::find_ability_def(aid);
                if (!d) continue;
                ecs::AbilityCastInput ci;
                aim_from_hand(play, w, e, cam_pos, cam_fwd, d->range, world, ci.origin, ci.aim);
                ci.caster_pos = player->GetTransform()->GetWorldPosition();
                const ecs::AbilityFired f = ecs::ability_detail::fire(w, e, *d, -1, ci, &world, &bridge.events());
                spdlog::debug("[abilities] {} went off at ({:.1f}, {:.1f}, {:.1f}), reached {}", aid, f.point.x, f.point.y, f.point.z, f.targets);
                if (pfx) pfx->ability_fired(*d, f, ci.origin);
            }
        }
        auto* bar = w.try_get<ecs::AbilityBar>(e);
        if (!bar || bar->slots.empty()) return;

        ecs::AbilityCastInput ci;
        ci.pressed = in.pressed;
        ci.held = bar->casting >= 0 && bar->casting < 8 && in.held[bar->casting];
        const ecs::WorldRayFn world = physics_world_ray(play.GetPhysicsWorld());
        float range = 25.0f;
        for (const auto& id : bar->slots)
            if (const ecs::AbilityDef* d = ecs::find_ability_def(id)) range = std::max(range, d->range);
        aim_from_hand(play, w, e, cam_pos, cam_fwd, range, world, ci.origin, ci.aim);
        ci.caster_pos = player->GetTransform()->GetWorldPosition();
        const auto& mst = play.GetPlayerMovementState();
        ci.moving = glm::length(glm::vec2(mst.velocity.x, mst.velocity.z)) > 0.5f;
        std::vector<ecs::AbilityFired> fired;
        ecs::step_ability_bar(w, e, *bar, ci, &world, &bridge.events(), dt, &fired);

        // Its effects: the wind-up at the hand while it is cast, then where
        // each one went off.
        if (pfx && bar->casting >= 0 && !bar->channelling && bar->casting < static_cast<int>(bar->slots.size()))
            if (const ecs::AbilityDef* d = ecs::find_ability_def(bar->slots[bar->casting])) pfx->ability_windup(*d, ci.origin, dt);
        for (const auto& f : fired) {
            const ecs::AbilityDef* d = ecs::find_ability_def(f.id);
            if (d && pfx) pfx->ability_fired(*d, f, ci.origin);
        }

        // HUD
        hud_.shown = true;
        hud_.slots.resize(bar->slots.size());
        for (size_t i = 0; i < bar->slots.size(); ++i) {
            const ecs::AbilityDef* d = ecs::find_ability_def(bar->slots[i]);
            Slot& s = hud_.slots[i];
            s.name = d ? d->name : bar->slots[i] + "?";
            s.cooldown = i < bar->cooldown.size() ? bar->cooldown[i] : 0.0f;
            s.cooldown_total = d ? d->cooldown : 0.0f;
            s.ready = bar->casting < 0 && ecs::ability_slot_ready(w, e, *bar, static_cast<int>(i));
        }
        hud_.casting = bar->casting;
        hud_.channelling = bar->channelling;
        hud_.cast_progress = 0.0f;
        hud_.cast_name.clear();
        if (bar->casting >= 0 && bar->casting < static_cast<int>(bar->slots.size())) {
            if (const ecs::AbilityDef* d = ecs::find_ability_def(bar->slots[bar->casting])) {
                hud_.cast_name = d->name;
                hud_.cast_progress = bar->channelling
                    ? 1.0f - std::clamp(bar->channel_timer / std::max(1e-3f, d->channel_time), 0.0f, 1.0f)
                    : std::clamp(bar->cast_timer / std::max(1e-3f, d->cast_time), 0.0f, 1.0f);
            }
        }
    }

    /// Where a cast leaves from and which way: in first person the right hand,
    /// towards what the crosshair is on (someone, or the world); in third
    /// person as the gun aims (aim_from_view).
    static void aim_from_hand(ScenePlaybackManager& play, schizo::ecs::World& w, schizo::ecs::Entity self, const glm::vec3& cam_pos,
                              const glm::vec3& cam_fwd, float range, const schizo::ecs::WorldRayFn& world, glm::vec3& origin,
                              glm::vec3& dir) {
        aim_from_view(play, cam_pos, cam_fwd, range, world, origin, dir);
        const glm::vec3 fwd = glm::normalize(cam_fwd);
        if (glm::length(origin - (cam_pos + fwd * 0.3f)) > 0.05f) return;   // third person: as the gun does
        float t = range;
        glm::vec3 n(0.0f);
        if (!(world && world(cam_pos, fwd, range, t, n))) t = range;
        float ta = t;
        if (schizo::ecs::ability_detail::target_along(w, self, cam_pos, fwd, t, ta) != schizo::ecs::null_entity) t = ta;
        const glm::vec3 target = cam_pos + fwd * t;
        glm::vec3 right = glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f));
        right = glm::length(right) > 1e-4f ? glm::normalize(right) : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 up = glm::cross(right, fwd);
        origin = cam_pos + fwd * 0.55f + right * 0.24f - up * 0.2f;
        const glm::vec3 to = target - origin;
        dir = glm::length(to) > 1e-3f ? glm::normalize(to) : fwd;
    }

private:
    Hud hud_;
};

}  // namespace schizo::editor
