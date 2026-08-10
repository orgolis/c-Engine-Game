#pragma once

// ============================================================================
// EditorNpcAgents — perception → decision → movement, on the real navmesh.
//
// This is the piece that makes the other Phase 3 work observable. The navmesh
// bake (3.4) produced walkable geometry nothing walked on; the behaviour trees,
// blackboard and path follower were verified and driven by nothing; the vision
// cone existed as an ECS component no decision consumed. Each was complete and
// inert.
//
// It was scheduled after those items, but leaving them connected to nothing is
// how a phase ends with five green rows and no visible behaviour — so it is
// pulled forward to finish them.
//
// THE TREE, deliberately small:
//
//   Selector
//     ├── Sequence [ can see target? → in attack range? → use ability ]
//     ├── Sequence [ can see target? → chase it ]
//     └── Patrol (walk a loop around the spawn point)
//
// Selector order is the priority: chasing pre-empts patrolling every tick,
// which is what makes the agent look reactive rather than committed. The BT
// library's own Running-child memory handles resuming a partial patrol leg.
//
// PATHS COME FROM THE BAKED NAVMESH. Not straight lines — that is the whole
// point. If the navmesh has a hole in it, the agent walks around it, and it
// does so because the geometry says to rather than because a demo hard-coded an
// obstacle.
// ============================================================================

#include "ai/behavior_tree.h"
#include "ai/navmesh.h"
#include "ai/path_follow.h"
#include "ecs/gameplay_abilities.h" // try_activate_ability: the AI picks WHEN, not WHAT
#include "ecs/gameplay_stealth.h"   // can_see: one perception rule, not two
#include "ecs_bridge.h"             // OOP entity -> ECS entity, for casting
#include "entity.h"
#include "scene.h"
#include "transform.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace schizo::editor {

class EditorNpcAgents {
public:
    /// Tick every agent entity against `nav`. Does nothing without a baked
    /// navmesh — an agent with nowhere to walk should stand still, not
    /// teleport in straight lines, because that would hide a missing bake.
    void update(const std::shared_ptr<schizo::scene::Scene>& scene,
                const schizo::ai::NavMesh& nav,
                float dt,
                EcsSceneBridge* bridge = nullptr) {
        chasing_  = 0;
        active_   = 0;
        attacked_ = 0;
        if (!scene || nav.empty() || dt <= 0.0f) return;

        std::unordered_map<uint32_t, bool> seen;
        for (const auto& ent : scene->GetEntities()) {
            if (!ent || !ent->IsActive()) continue;
            auto* ag = ent->GetNpcAgentComponent();
            if (!ag || !ag->enabled) continue;
            auto* tf = ent->GetTransform();
            if (!tf) continue;

            const uint32_t id = ent->GetId();
            seen[id] = true;
            auto& st = state(id, tf->GetWorldPosition());

            // --- perception ------------------------------------------------
            const glm::vec3 pos = tf->GetWorldPosition();
            glm::vec3 target_pos(0.0f);
            bool has_target = false;
            if (auto tgt = scene->GetEntityByName(ag->target_name)) {
                if (tgt->IsActive() && tgt.get() != ent.get()) {
                    if (auto* ttf = tgt->GetTransform()) {
                        target_pos = ttf->GetWorldPosition();
                        has_target = true;
                    }
                }
            }
            // Forward comes from the agent's own rotation, so turning the
            // entity in the editor really does change what it can see.
            const glm::vec3 fwd = glm::normalize(
                glm::vec3(tf->GetWorldMatrix() * glm::vec4(0, 0, 1, 0)));
            const bool visible = has_target &&
                schizo::ecs::can_see(pos, fwd, ag->sight_fov_deg, ag->sight_range, target_pos);

            const float range_xz = [&]{
                glm::vec3 d = target_pos - pos; d.y = 0.0f;
                return glm::length(d);
            }();
            // Attacking is opt-in: attack_range 0 means "this agent has nothing
            // to do on arrival", which keeps a plain patroller a patroller.
            const bool in_range = visible && ag->attack_range > 0.0f &&
                                  range_xz <= ag->attack_range;

            st.bb.set<bool>("can_see", visible);
            st.bb.set<bool>("in_range", in_range);
            st.bb.set<glm::vec3>("target", target_pos);
            st.bb.set<glm::vec3>("self", pos);

            // --- decide ----------------------------------------------------
            st.chase_requested  = false;
            st.attack_requested = false;
            st.tree->tick(st.bb, dt);
            if (st.chase_requested) ++chasing_;

            // Fire the ability through the ECS. The AI decides WHEN; the
            // ability data on the entity decides what happens -- cost, cooldown
            // and effects are all the ability system's business, so combat
            // design stays out of the agent.
            if (st.attack_requested && bridge) {
                const uint32_t caster_id = bridge->ecs_entity_id(tf);
                uint32_t target_id = kNoEcsEntity;
                if (auto tgt = scene->GetEntityByName(ag->target_name))
                    if (auto* ttf = tgt->GetTransform())
                        target_id = bridge->ecs_entity_id(ttf);
                if (caster_id != kNoEcsEntity) {
                    auto& w = bridge->world();
                    const bool fired = schizo::ecs::try_activate_ability(
                        w, static_cast<schizo::ecs::Entity>(caster_id),
                        static_cast<size_t>(ag->ability_index),
                        target_id == kNoEcsEntity ? schizo::ecs::null_entity
                                                  : static_cast<schizo::ecs::Entity>(target_id));
                    if (fired) ++attacked_;
                }
            }

            // --- move along the navmesh ------------------------------------
            const glm::vec3 goal = st.chase_requested ? target_pos : st.patrol_goal;
            // Re-path when the goal moved meaningfully or the current path ran
            // out. Re-pathing every frame would be correct and wasteful; never
            // re-pathing would make a chase follow a stale route.
            if (!st.follower.active() ||
                glm::length(goal - st.last_goal) > 1.0f) {
                std::vector<glm::vec3> path;
                if (nav.find_path(pos, goal, path) && !path.empty()) {
                    st.follower.set_path(std::move(path));
                    st.last_goal = goal;
                }
            }

            const auto steer = st.follower.steer(pos, ag->move_speed);
            if (steer.moving) {
                tf->SetLocalPosition(tf->GetLocalPosition() + steer.velocity * dt);
                ++active_;
            }
            if (st.follower.arrived() && !st.chase_requested) advance_patrol(st, ag->patrol_radius);
        }

        for (auto it = agents_.begin(); it != agents_.end(); )
            it = seen.count(it->first) ? std::next(it) : agents_.erase(it);
    }

    size_t agent_count()  const { return agents_.size(); }
    size_t chasing()      const { return chasing_; }
    size_t attacks_this_frame() const { return attacked_; }
    size_t moving()       const { return active_; }
    void   clear() { agents_.clear(); chasing_ = active_ = attacked_ = 0; }

    /// Re-express every agent's cached world-space state after a floating-origin
    /// rebase.
    ///
    /// A rebase moves the entities but not the numbers agents remember about
    /// them. Each of these is a separate way for an agent to go wrong, and none
    /// of them reports an error:
    ///   * spawn/patrol_goal -- the patrol route stays anchored to the old
    ///     origin, so the agent walks back toward where it used to live;
    ///   * last_goal -- compared against the new goal to decide whether to
    ///     repath, so a stale value suppresses the repath that would have
    ///     corrected everything else;
    ///   * the follower's path -- waypoints steering toward the old world.
    /// The caller must translate the NavMesh by the same shift.
    void apply_origin_shift(const glm::vec3& d) {
        if (d == glm::vec3(0.0f)) return;
        for (auto& kv : agents_) {
            Agent& a = *kv.second;
            a.spawn       += d;
            a.patrol_goal += d;
            if (a.last_goal.x < 1e8f) a.last_goal += d;   // leave the "never pathed" sentinel alone
            a.follower.translate(d);
        }
    }

private:
    struct Agent {
        schizo::ai::Blackboard              bb;
        bool attack_requested = false;
        std::unique_ptr<schizo::ai::BtNode> tree;
        schizo::ai::PathFollower            follower;
        glm::vec3 spawn{0.0f};
        glm::vec3 patrol_goal{0.0f};
        glm::vec3 last_goal{1e9f};     // forces a path on the first tick
        int       patrol_leg = 0;
        bool      chase_requested = false;
    };

    Agent& state(uint32_t id, const glm::vec3& pos) {
        auto it = agents_.find(id);
        if (it != agents_.end()) return *it->second;

        auto a = std::make_unique<Agent>();
        a->spawn = pos;
        Agent* raw = a.get();
        advance_patrol(*raw, 8.0f);

        // Attack sits ABOVE chase: an agent already in range should swing
        // rather than keep closing. When the ability is on cooldown the action
        // returns Failure, so the Selector falls through to chase and the agent
        // keeps position instead of freezing mid-fight.
        auto attack = std::make_unique<schizo::ai::Sequence>();
        attack->add(std::make_unique<schizo::ai::Condition>(
            [](schizo::ai::Blackboard& bb) { return bb.get<bool>("in_range", false); }));
        attack->add(std::make_unique<schizo::ai::Action>(
            [raw](schizo::ai::Blackboard&, float) {
                raw->attack_requested = true;
                return schizo::ai::BtStatus::Running;
            }));

        // Chase pre-empts patrol because it is first in the Selector.
        auto chase = std::make_unique<schizo::ai::Sequence>();
        chase->add(std::make_unique<schizo::ai::Condition>(
            [](schizo::ai::Blackboard& bb) { return bb.get<bool>("can_see", false); }));
        chase->add(std::make_unique<schizo::ai::Action>(
            [raw](schizo::ai::Blackboard&, float) {
                raw->chase_requested = true;
                return schizo::ai::BtStatus::Running;   // chasing never "completes"
            }));

        auto root = std::make_unique<schizo::ai::Selector>();
        root->add(std::move(attack));
        root->add(std::move(chase));
        root->add(std::make_unique<schizo::ai::Action>(
            [](schizo::ai::Blackboard&, float) { return schizo::ai::BtStatus::Running; }));
        a->tree = std::move(root);

        agents_[id] = std::move(a);
        return *raw;
    }

    // A square loop around the spawn, so patrolling is visibly a route rather
    // than a jitter around one point.
    static void advance_patrol(Agent& a, float r) {
        static const glm::vec3 dirs[4] = {{1,0,0}, {0,0,1}, {-1,0,0}, {0,0,-1}};
        a.patrol_leg = (a.patrol_leg + 1) % 4;
        a.patrol_goal = a.spawn + dirs[a.patrol_leg] * r;
        a.last_goal = glm::vec3(1e9f);   // force a re-path to the new leg
    }

    std::unordered_map<uint32_t, std::unique_ptr<Agent>> agents_;
    size_t chasing_ = 0, active_ = 0, attacked_ = 0;
};

}  // namespace schizo::editor
