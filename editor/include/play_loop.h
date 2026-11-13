#pragma once

// ============================================================================
// The game loop's glue in Play (D0): death, loot on the ground, interacting,
// and the player coming back.
// ============================================================================
//
//   - ecs::tick_life decides who died; here the dead get their consequences:
//     loot becomes pickups on the ground (small glowing spheres), a body is
//     hidden after its corpse time, the player respawns -- from the last
//     quick save when there is one, else where Play started.
//   - E interacts with the nearest interactable in range (a pickup goes into
//     the inventory); the HUD shows what E would do. A used-up pickup
//     disappears.
//   - Stop puts back what Play hid. (Entities Play created, and the gameplay
//     components Play changed, are put back by Begin/EndPlayMode in main.)

#include "ecs/gameplay_interaction.h"
#include "ecs/gameplay_items.h"
#include "ecs/gameplay_life.h"
#include "ecs_bridge.h"
#include "entity.h"
#include "entity_factory.h"
#include "play_save.h"
#include "scene.h"
#include "scene_playback_manager.h"

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace schizo::editor {

class PlayGameLoop {
public:
    struct Hud {
        std::string prompt;          // "E  Pick up Health Potion", empty when nothing is in reach
        bool        player_dead = false;
    };
    const Hud& hud() const { return hud_; }

    void update(float dt, ScenePlaybackManager& play, EcsSceneBridge& bridge,
                const std::shared_ptr<schizo::scene::Scene>& scene, bool interact_pressed) {
        namespace ecs = schizo::ecs;
        hud_.prompt.clear();
        if (!scene || !play.IsPlaying() || play.IsPaused()) return;
        ecs::World& w = bridge.world();
        auto* player = play.GetPlayerEntity();
        const uint32_t pid = player ? bridge.ecs_entity_id(player->GetTransform()) : kNoEcsEntity;
        const ecs::Entity pe = static_cast<ecs::Entity>(pid);
        const bool have_player = pid != kNoEcsEntity && w.valid(pe);
        if (player && !start_known_) {
            start_known_ = true;
            start_pos_ = player->GetTransform()->GetWorldPosition();
            start_rot_ = player->GetTransform()->GetWorldRotation();
        }

        // Loot spawned last frame gets its gameplay side once the bridge has it.
        for (size_t i = 0; i < pending_loot_.size();) {
            auto ent = scene->GetEntityById(pending_loot_[i].first);
            const uint32_t id = ent ? bridge.ecs_entity_id(ent->GetTransform()) : kNoEcsEntity;
            if (!ent) { pending_loot_.erase(pending_loot_.begin() + i); continue; }
            if (id == kNoEcsEntity || !w.valid(static_cast<ecs::Entity>(id))) { ++i; continue; }
            const auto le = static_cast<ecs::Entity>(id);
            const ecs::ItemInstance& it = pending_loot_[i].second;
            const ecs::ItemDef* d = ecs::find_item(it.def_id);
            ecs::Interactable ia;
            ia.prompt = "Pick up " + (d ? d->name : it.def_id) + (it.quantity > 1 ? " x" + std::to_string(it.quantity) : "");
            ia.event = "pickup.loot";
            ia.range = 1.8f;
            ia.consume_on_use = true;
            w.add<ecs::Interactable>(le, ia);
            w.add<ecs::Pickup>(le, ecs::Pickup{it.def_id, it.quantity});
            pending_loot_.erase(pending_loot_.begin() + i);
        }

        // Life and death.
        std::vector<ecs::LifeEvent> events;
        ecs::tick_life(w, dt, &bridge.events(), rng_, events);
        for (const auto& ev : events) {
            auto ent = scene_entity_of(bridge, scene, ev.entity);
            const bool is_player = have_player && ev.entity == pe;
            switch (ev.kind) {
                case ecs::LifeEvent::Kind::Died:
                    if (is_player) { hud_.player_dead = true; break; }
                    if (ent) drop_loot(scene, ent->GetTransform()->GetWorldPosition(), ev.loot);
                    break;
                case ecs::LifeEvent::Kind::Removed:
                    if (ent && ent->IsActive()) {
                        ent->SetActive(false);
                        play.RemoveBodyForEntity(ent->GetId());
                        hidden_.push_back(ent->GetId());
                    }
                    break;
                case ecs::LifeEvent::Kind::Respawn:
                    if (is_player) {
                        hud_.player_dead = false;
                        if (!load_play("quick", play, bridge)) play.TeleportPlayer(start_pos_, start_rot_);
                        // A save made while hurt still comes back alive.
                        if (auto* m = w.try_get<ecs::Mortal>(pe))
                            if (auto* a = w.try_get<ecs::AttributeSet>(pe)) {
                                const auto* h = a->find(m->health);
                                if (h && h->current <= 0.0f) ecs::revive(w, pe, *m, *a);
                            }
                    }
                    break;
            }
        }

        // Interacting: the nearest interactable in reach of the player.
        if (have_player && !hud_.player_dead) {
            const glm::vec3 at = player->GetTransform()->GetWorldPosition();
            ecs::Entity best = ecs::null_entity;
            float best_d2 = 0.0f;
            w.each<ecs::Interactable>([&](ecs::Entity e, ecs::Interactable& it) {
                if (!it.enabled || e == pe) return;
                auto ent = scene_entity_of(bridge, scene, e);
                if (!ent || !ent->IsActive()) return;
                const glm::vec3 d = ent->GetTransform()->GetWorldPosition() - at;
                const float d2 = glm::dot(d, d);
                if (d2 <= it.range * it.range && (best == ecs::null_entity || d2 < best_d2)) { best = e; best_d2 = d2; }
            });
            if (best != ecs::null_entity) {
                hud_.prompt = "E  " + w.get<ecs::Interactable>(best).prompt;
                if (interact_pressed && ecs::interact(w, pe, best, &bridge.events())) {
                    const auto* it = w.try_get<ecs::Interactable>(best);
                    if (it && !it->enabled)
                        if (auto ent = scene_entity_of(bridge, scene, best); ent && ent->IsActive()) {
                            ent->SetActive(false);
                            play.RemoveBodyForEntity(ent->GetId());
                            hidden_.push_back(ent->GetId());
                        }
                }
            }
        }
    }

    /// Stop: everything Play hid is shown again; the next Play starts afresh.
    void end(const std::shared_ptr<schizo::scene::Scene>& scene) {
        if (scene)
            for (uint32_t id : hidden_)
                if (auto e = scene->GetEntityById(id)) e->SetActive(true);
        hidden_.clear();
        pending_loot_.clear();
        start_known_ = false;
        hud_ = Hud{};
        ids_.clear();
    }

private:
    // ECS entity -> scene entity, by the bridge's own mapping (memoised; the
    // bridge's ids are stable while the scene entity lives).
    std::shared_ptr<schizo::scene::Entity> scene_entity_of(EcsSceneBridge& bridge, const std::shared_ptr<schizo::scene::Scene>& scene,
                                                           schizo::ecs::Entity e) {
        const uint32_t key = static_cast<uint32_t>(e);
        if (auto it = ids_.find(key); it != ids_.end()) {
            auto ent = scene->GetEntityById(it->second);
            if (ent && bridge.ecs_entity_id(ent->GetTransform()) == key) return ent;
        }
        for (const auto& ent : scene->GetEntities())
            if (ent && bridge.ecs_entity_id(ent->GetTransform()) == key) {
                ids_[key] = ent->GetId();
                return ent;
            }
        return nullptr;
    }

    void drop_loot(const std::shared_ptr<schizo::scene::Scene>& scene, const glm::vec3& where,
                   const std::vector<schizo::ecs::ItemInstance>& loot) {
        for (size_t i = 0; i < loot.size(); ++i) {
            const float a = static_cast<float>(i) * 2.399f;   // spread around the body
            const glm::vec3 p = where + glm::vec3(std::cos(a) * 0.7f, 0.0f, std::sin(a) * 0.7f);
            auto ent = schizo::scene::EntityFactory::CreateSphere(scene, "loot_" + loot[i].def_id, 0.18f,
                                                                  glm::vec4(1.0f, 0.82f, 0.25f, 1.0f));
            if (!ent) continue;
            ent->GetTransform()->SetWorldPosition(p);
            pending_loot_.push_back({ent->GetId(), loot[i]});
        }
    }

    Hud hud_;
    schizo::ecs::Rng rng_{0x5eed1234u};
    std::vector<uint32_t> hidden_;
    std::vector<std::pair<uint32_t, schizo::ecs::ItemInstance>> pending_loot_;
    std::unordered_map<uint32_t, uint32_t> ids_;
    bool      start_known_ = false;
    glm::vec3 start_pos_{0.0f};
    glm::quat start_rot_{1.0f, 0.0f, 0.0f, 0.0f};
};

}  // namespace schizo::editor
