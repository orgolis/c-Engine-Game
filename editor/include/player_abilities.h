#pragma once

// ============================================================================
// The player's abilities in Play (C5): keys 1..8 cast what is on the player's
// Ability Bar, a burst shows where each one goes off, and the HUD shows the
// bar with its cooldowns and the cast in progress.
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
                const glm::vec3& cam_fwd, const Input& in, ShotEffects& fx) {
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
                aim_from_view(play, cam_pos, cam_fwd, d->range, world, ci.origin, ci.aim);
                ci.caster_pos = player->GetTransform()->GetWorldPosition();
                const ecs::AbilityFired f = ecs::ability_detail::fire(w, e, *d, -1, ci, &world, &bridge.events());
                spdlog::debug("[abilities] {} went off at ({:.1f}, {:.1f}, {:.1f}), reached {}", aid, f.point.x, f.point.y, f.point.z, f.targets);
                ShotEffects::Effect burst;
                burst.kind = ShotEffects::Effect::Kind::Flash;
                burst.a = burst.b = f.point;
                burst.color = glm::vec3(1.0f, 0.6f, 0.25f);
                burst.life = 0.35f;
                burst.glow = 3.0f;
                burst.size = d->targeting == ecs::AbilityTargeting::Aimed ? 0.35f : std::max(0.3f, d->radius * 0.5f);
                fx.push(burst);
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
        aim_from_view(play, cam_pos, cam_fwd, range, world, ci.origin, ci.aim);
        ci.caster_pos = player->GetTransform()->GetWorldPosition();
        const auto& mst = play.GetPlayerMovementState();
        ci.moving = glm::length(glm::vec2(mst.velocity.x, mst.velocity.z)) > 0.5f;
        std::vector<ecs::AbilityFired> fired;
        ecs::step_ability_bar(w, e, *bar, ci, &world, &bridge.events(), dt, &fired);

        // A burst where each one went off: a beam to an aimed point, a flash
        // the size of an area.
        for (const auto& f : fired) {
            const ecs::AbilityDef* d = ecs::find_ability_def(f.id);
            if (!d) continue;
            const glm::vec3 col(0.35f, 0.75f, 1.0f);
            ShotEffects::Effect burst;
            burst.kind = ShotEffects::Effect::Kind::Flash;
            burst.a = burst.b = f.point;
            burst.color = col;
            burst.life = 0.3f;
            burst.glow = 2.0f;
            switch (d->targeting) {
                case ecs::AbilityTargeting::Self:
                    burst.a = burst.b = f.point + glm::vec3(0.0f, 0.2f, 0.0f);
                    burst.size = 0.6f;
                    break;
                case ecs::AbilityTargeting::Aimed: {
                    ShotEffects::Effect beam;
                    beam.kind = ShotEffects::Effect::Kind::Tracer;
                    beam.a = ci.origin;
                    beam.b = f.point;
                    beam.color = col;
                    beam.size = 0.06f;
                    beam.life = 0.12f;
                    fx.push(beam);
                    burst.size = 0.35f;
                    break;
                }
                default:
                    burst.size = std::max(0.3f, d->radius * 0.5f);
                    break;
            }
            fx.push(burst);
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

private:
    Hud hud_;
};

}  // namespace schizo::editor
