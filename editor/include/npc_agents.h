#pragma once

// ============================================================================
// EditorNpcAgents — perception → decision → movement, on the real navmesh.
//
// Every agent (an entity with an enabled NpcAgentComponent) runs, each frame:
//
// PERCEPTION. It sees its target inside its sight cone and range when nothing
//   solid is in between (the physics world: terrain, solid trees and rocks,
//   walls); it hears it close by (40% of its sight range) whichever way it
//   faces, and gunfire within its hearing range (notify_noise); being hurt
//   tells it too. Noticing the target alerts the others within its
//   alert_radius -- a camp fights together.
//
// DECISION, one mode at a time:
//   Patrol  walk to random points around where it started, pausing between.
//   Chase   go for the target -- or, out of sight, to where it was last
//           seen; there it Searches.
//   Search  look about that place for a few seconds, then Return.
//   Return  walk home (also when a chase took it beyond its leash_radius).
//   Fighting is part of Chase: a melee agent closes to its attack range; a
//   ranged one (attack_range above 5, or keep_distance) keeps its distance,
//   backs off when the target comes too close and side-steps while its
//   attack recovers. It attacks only what it can see.
//
// THE ATTACK. With `ability` set (an ability block in the project's .items),
//   it winds up for the ability's cast time -- standing, facing the target,
//   its wind-up effect showing -- then the ability goes off from its chest at
//   where the target will be (ahead of it, for something that flies), through
//   the ability system: damage, reach, team (it does not hurt its own side),
//   and something thrown or spat flies (tick_ability_shots). Without one, the
//   AbilitySet slot `ability_index` is used at once, as before.
//
// MOVEMENT. Paths come from the baked navmesh. The physics world keeps it out
//   of things the navmesh does not know -- it steers round trees and rocks
//   ahead of it and slides along what it touches -- and on the ground. Agents
//   keep apart from each other. Hurt, it staggers back for a moment.
//
// Without a navmesh it stands still; without a physics world it neither
// checks line of sight nor avoids anything (headless checks).
// ============================================================================

#include "ai/navmesh.h"
#include "ai/path_follow.h"
#include "ecs/gameplay_abilities.h"     // try_activate_ability (the AbilitySet path)
#include "ecs/gameplay_ability_cast.h"  // AbilityDef: an agent's attack as data
#include "ecs/gameplay_stealth.h"       // can_see: one perception rule, not two
#include "ecs/gameplay_tags.h"          // state.dead
#include "ecs/gameplay_weapons.h"       // WorldRayFn
#include "ecs_bridge.h"                 // OOP entity -> ECS entity, for casting
#include "entity.h"
#include "play_effects.h"               // the wind-up and the attack, seen
#include "scene.h"
#include "transform.h"

#include <utility>   // std::as_const: reads must not stamp the entity

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <vector>

namespace schizo::editor {

class EditorNpcAgents {
public:
    enum class Mode : uint8_t { Patrol, Chase, Search, Return };

    /// Tagged state.dead by the life system (ecs/gameplay_life.h).
    static bool dead(EcsSceneBridge& bridge, const schizo::scene::Transform* tf) {
        const uint32_t id = bridge.ecs_entity_id(tf);
        if (id == kNoEcsEntity) return false;
        const auto* tags = bridge.world().try_get<schizo::ecs::GameplayTags>(static_cast<schizo::ecs::Entity>(id));
        return tags && tags->has("state.dead");
    }

    /// A loud noise (a shot): agents within their hearing range of it come to look.
    void notify_noise(const glm::vec3& at) { noises_.push_back(at); }

    /// Tick every agent entity against `nav`. Does nothing without a baked
    /// navmesh — an agent with nowhere to walk should stand still, not
    /// teleport in straight lines, because that would hide a missing bake.
    void update(const std::shared_ptr<schizo::scene::Scene>& scene,
                const schizo::ai::NavMesh& nav,
                float dt,
                EcsSceneBridge* bridge = nullptr,
                const schizo::ecs::WorldRayFn* world = nullptr,
                PlayEffects* fx = nullptr) {
        chasing_  = 0;
        active_   = 0;
        attacked_ = 0;
        alerted_  = 0;
        sources_.clear();
        const std::vector<glm::vec3> noises = std::move(noises_);
        noises_.clear();
        if (!scene || nav.empty() || dt <= 0.0f) return;
        const bool has_world = world && *world;

        // Everyone's position first: separation, and calling the others in.
        struct Seen { uint32_t id; glm::vec3 pos; float alert_radius; };
        std::vector<Seen> others;
        for (const auto& ent : scene->GetEntities()) {
            if (!ent || !ent->IsActive()) continue;
            const auto* ag = std::as_const(*ent).GetNpcAgentComponent();
            if (!ag || !ag->enabled || !ent->GetTransform()) continue;
            if (bridge && dead(*bridge, ent->GetTransform())) continue;
            others.push_back({ent->GetId(), ent->GetTransform()->GetWorldPosition(), ag->alert_radius});
        }
        std::vector<std::pair<glm::vec3, glm::vec3>> calls;   // (who noticed, where the target is)

        std::unordered_map<uint32_t, bool> seen;
        for (const auto& ent : scene->GetEntities()) {
            if (!ent || !ent->IsActive()) continue;
            const auto* ag = std::as_const(*ent).GetNpcAgentComponent();
            if (!ag || !ag->enabled) continue;
            auto* tf = ent->GetTransform();
            if (!tf) continue;
            if (bridge && dead(*bridge, tf)) continue;   // the dead do nothing

            const uint32_t id = ent->GetId();
            seen[id] = true;
            glm::vec3 pos = tf->GetWorldPosition();
            Agent& st = state(id, pos, ag->patrol_radius);

            // Where the ground is under it: once, so it keeps that height over the ground.
            if (has_world && !st.ground_known) {
                float g = 0.0f;
                if (ground_below(*world, pos, g)) { st.ground_offset = std::clamp(pos.y - g, 0.0f, 3.0f); }
                st.ground_known = true;
            }

            // --- perception ------------------------------------------------
            glm::vec3 target_pos(0.0f);
            bool has_target = false;
            schizo::scene::Entity* target_ent = nullptr;
            if (auto tgt = scene->GetEntityByName(ag->target_name)) {
                if (tgt->IsActive() && tgt.get() != ent.get() && !(bridge && dead(*bridge, tgt->GetTransform()))) {
                    if (auto* ttf = tgt->GetTransform()) {
                        target_pos = ttf->GetWorldPosition();
                        has_target = true;
                        target_ent = tgt.get();
                    }
                }
            }
            const glm::vec3 fwd = glm::normalize(glm::vec3(tf->GetWorldMatrix() * glm::vec4(0, 0, 1, 0)));
            const glm::vec3 eye = pos + glm::vec3(0.0f, 0.6f, 0.0f);
            const float dist = has_target ? glm::length(target_pos - pos) : 1e9f;
            const bool clear_line = has_target && (!has_world || line_clear(*world, eye, target_pos + glm::vec3(0.0f, 0.3f, 0.0f)));
            const bool in_view = has_target && clear_line &&
                                 schizo::ecs::can_see(pos, fwd, ag->sight_fov_deg, ag->sight_range, target_pos);
            const bool heard = has_target && dist <= ag->sight_range * 0.4f;
            bool hurt = false;
            schizo::ecs::Entity me = schizo::ecs::null_entity;
            if (bridge) {
                const uint32_t mid = bridge->ecs_entity_id(tf);
                if (mid != kNoEcsEntity) {
                    me = static_cast<schizo::ecs::Entity>(mid);
                    if (const auto* a = bridge->world().try_get<schizo::ecs::AttributeSet>(me)) {
                        const float h = a->get("Health", -1.0f);
                        hurt = st.last_health >= 0.0f && h < st.last_health;
                        st.last_health = h;
                    }
                }
            }
            bool noise = false;
            if (has_target)
                for (const auto& n : noises) noise = noise || glm::length(n - pos) <= ag->hearing_range;
            st.ignore = std::max(0.0f, st.ignore - dt);
            const bool sensed = has_target && (in_view || heard || hurt || noise) && (st.ignore <= 0.0f || hurt);
            const bool was_aware = st.mode == Mode::Chase;
            if (sensed) {
                st.last_seen = target_pos;
                st.since_seen = 0.0f;
                if (st.mode != Mode::Chase) st.last_goal = glm::vec3(1e9f);
                st.mode = Mode::Chase;
                if (!was_aware) calls.push_back({pos, target_pos});
            } else {
                st.since_seen += dt;
            }
            if (hurt) {
                st.stagger = 0.22f;
                if (has_target) {
                    glm::vec3 away = pos - target_pos; away.y = 0.0f;
                    st.knock = glm::length(away) > 1e-3f ? glm::normalize(away) * 2.5f : glm::vec3(0.0f);
                }
            }
            // A chase taken too far: give up and go home (and do not turn round at once).
            if (st.mode == Mode::Chase && glm::length(glm::vec2(pos.x - st.spawn.x, pos.z - st.spawn.z)) > ag->leash_radius) {
                st.mode = Mode::Return;
                st.ignore = 4.0f;
                st.windup = 0.0f;
                st.last_goal = glm::vec3(1e9f);
            }
            const bool visible = has_target && st.mode == Mode::Chase && (in_view || heard || (hurt && clear_line));
            if (st.mode == Mode::Chase) ++alerted_;

            // --- the attack -------------------------------------------------
            const schizo::ecs::AbilityDef* def = ag->ability.empty() ? nullptr : schizo::ecs::find_ability_def(ag->ability);
            const float keep = ag->keep_distance >= 0.0f ? ag->keep_distance : (ag->attack_range > 5.0f ? ag->attack_range * 0.6f : 0.0f);
            const float range_xz = [&] { glm::vec3 d = target_pos - pos; d.y = 0.0f; return glm::length(d); }();
            const bool in_range = visible && ag->attack_range > 0.0f && range_xz <= ag->attack_range && clear_line;
            st.cooldown = std::max(0.0f, st.cooldown - dt);
            const glm::vec3 target_vel = st.prev_target_known && dt > 0.0f ? (target_pos - st.prev_target) / dt : glm::vec3(0.0f);
            st.prev_target = target_pos;
            st.prev_target_known = has_target;
            bool attacking = false;
            if (def && bridge && me != schizo::ecs::null_entity) {
                const glm::vec3 hand = eye + fwd * 0.45f;
                if (st.windup > 0.0f) {
                    attacking = true;
                    st.windup -= dt;
                    if (fx) fx->ability_windup(*def, hand, dt);
                    if (st.windup <= 0.0f) {
                        st.windup = 0.0f;
                        // Where the target will be when it gets there.
                        glm::vec3 aim_at = target_pos + glm::vec3(0.0f, 0.2f, 0.0f);
                        if (def->speed > 0.0f) aim_at += target_vel * (glm::length(aim_at - hand) / def->speed);
                        if (def->arc > 0.0f && def->speed > 0.0f) {
                            const float tt = glm::length(aim_at - hand) / def->speed;
                            aim_at.y += 0.5f * def->arc * tt * tt;   // aim above, it falls on the way
                        }
                        schizo::ecs::AbilityCastInput ci;
                        ci.origin = hand;
                        ci.aim = glm::normalize(aim_at - hand);
                        ci.caster_pos = pos;
                        const schizo::ecs::AbilityFired f =
                            schizo::ecs::ability_detail::fire(bridge->world(), me, *def, -1, ci, world, &bridge->events());
                        if (fx) fx->ability_fired(*def, f, hand);
                        if (f.on_actor || f.targets > 0) sources_.push_back(pos);
                        st.cooldown = std::max(0.2f, def->cooldown);
                        ++attacked_;
                    }
                } else if (in_range && st.cooldown <= 0.0f && st.stagger <= 0.0f) {
                    st.windup = std::max(0.05f, def->cast_time > 0.0f ? def->cast_time : 0.3f);
                    attacking = true;
                }
            } else if (in_range && bridge && me != schizo::ecs::null_entity) {
                // The AbilitySet path: at once, gated by the ability's own cooldown.
                attacking = true;
                schizo::ecs::Entity target_id = schizo::ecs::null_entity;
                if (target_ent)
                    if (const uint32_t t = bridge->ecs_entity_id(target_ent->GetTransform()); t != kNoEcsEntity)
                        target_id = static_cast<schizo::ecs::Entity>(t);
                if (schizo::ecs::try_activate_ability(bridge->world(), me, static_cast<size_t>(ag->ability_index), target_id)) {
                    ++attacked_;
                    sources_.push_back(pos);
                }
            }

            // --- where to go -------------------------------------------------
            glm::vec3 goal = pos;
            float speed = ag->move_speed;
            bool  hold = false;   // stand (a wind-up, a stagger, a pause on patrol)
            switch (st.mode) {
                case Mode::Chase:
                    if (visible) {
                        glm::vec3 to = target_pos - pos; to.y = 0.0f;
                        const float d = glm::length(to);
                        const glm::vec3 dir = d > 1e-3f ? to / d : fwd;
                        if (keep <= 0.0f) {
                            goal = target_pos;                       // melee: close in
                            if (in_range && d <= ag->attack_range * 0.8f) hold = true;
                        } else if (d < keep * 0.7f) {
                            goal = pos - dir * 4.0f;                 // too close: back off
                        } else if (d > ag->attack_range * 0.95f || !clear_line) {
                            goal = target_pos;                       // too far or blocked: come on
                        } else {
                            // In its band: side-step while the attack recovers.
                            st.strafe_timer -= dt;
                            if (st.strafe_timer <= 0.0f) { st.strafe = -st.strafe; st.strafe_timer = 1.2f + rnd01(id) * 1.6f; }
                            const glm::vec3 side(dir.z * st.strafe, 0.0f, -dir.x * st.strafe);
                            goal = pos + side * 3.0f;
                            speed *= 0.6f;
                        }
                    } else {
                        goal = st.last_seen;                         // where it was last seen
                        glm::vec3 d = st.last_seen - pos; d.y = 0.0f;
                        if (glm::length(d) < 1.5f || st.since_seen > 12.0f) {
                            st.mode = Mode::Search;
                            st.search_left = 4.0f + rnd01(id) * 2.0f;
                            st.last_goal = glm::vec3(1e9f);
                        }
                    }
                    break;
                case Mode::Search:
                    st.search_left -= dt;
                    speed *= 0.5f;
                    if (st.search_left <= 0.0f) {
                        st.mode = Mode::Return;
                        st.last_goal = glm::vec3(1e9f);
                    } else {
                        if (st.follower.arrived() || !st.follower.active()) {
                            const float a = rnd01(id + static_cast<uint32_t>(st.search_left * 97.0f)) * 6.2831853f;
                            st.wander = st.last_seen + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * 4.0f;
                            st.last_goal = glm::vec3(1e9f);
                        }
                        goal = st.wander;
                    }
                    break;
                case Mode::Return: {
                    goal = st.spawn;
                    speed *= 0.65f;
                    glm::vec3 d = st.spawn - pos; d.y = 0.0f;
                    if (glm::length(d) < 1.5f) { st.mode = Mode::Patrol; st.pause = 1.0f; st.last_goal = glm::vec3(1e9f); }
                    break;
                }
                case Mode::Patrol:
                    speed *= 0.5f;
                    if (st.pause > 0.0f) { st.pause -= dt; hold = true; }
                    goal = st.patrol_goal;
                    break;
            }
            if (attacking && def) hold = true;   // a wind-up is stood still for: it can be seen coming
            if (st.stagger > 0.0f) { st.stagger -= dt; hold = true; }
            if (st.mode == Mode::Chase && !in_range) ++chasing_;   // closing in (or going to where it was seen)

            // --- move along the navmesh, round what is in the way ------------
            if (!st.follower.active() || glm::length(goal - st.last_goal) > 1.0f) {
                std::vector<glm::vec3> path;
                if (nav.find_path(pos, goal, path) && !path.empty()) {
                    st.follower.set_path(std::move(path));
                    st.last_goal = goal;
                }
            }
            glm::vec3 vel(0.0f);
            if (!hold) {
                const auto steer = st.follower.steer(pos, speed);
                if (steer.moving) vel = steer.velocity;
            }
            if (st.stagger > 0.0f) vel += st.knock * (st.stagger / 0.22f);
            vel.y = 0.0f;
            // Apart from the others.
            for (const auto& o : others) {
                if (o.id == id) continue;
                glm::vec3 d = pos - o.pos; d.y = 0.0f;
                const float l = glm::length(d);
                if (l > 1e-3f && l < 1.4f) vel += d / l * (1.4f - l) * 2.5f;
            }
            const float vlen = glm::length(vel);
            if (has_world && vlen > 0.05f) vel = avoid(*world, st, pos, vel, vlen);
            glm::vec3 next = pos + vel * dt;
            if (has_world && vlen > 0.05f) {
                // Slide along what it would walk into.
                const glm::vec3 knee = pos + glm::vec3(0.0f, -st.ground_offset + 0.5f, 0.0f);
                const glm::vec3 step = next - pos;
                const float sl = glm::length(step);
                float t = 0.0f;
                glm::vec3 n(0.0f);
                if (sl > 1e-5f && (*world)(knee, step / sl, sl + 0.35f, t, n) && std::fabs(n.y) < 0.7f) {   // not the ground
                    glm::vec3 nh(n.x, 0.0f, n.z);
                    if (glm::length(nh) > 1e-3f) {
                        nh = glm::normalize(nh);
                        const glm::vec3 slide = step - nh * std::min(0.0f, glm::dot(step, nh));
                        next = pos + slide;
                    }
                }
            }
            if (has_world) {
                float g = 0.0f;
                if (ground_below(*world, next, g)) {
                    const float want = g + st.ground_offset;
                    next.y = next.y + (want - next.y) * std::min(1.0f, 12.0f * dt);
                }
            }
            if (glm::length(next - pos) > 1e-5f) {
                tf->SetLocalPosition(tf->GetLocalPosition() + (next - pos));
                ++active_;
            }
            // Face the target while fighting, else the way it walks.
            {
                glm::vec3 look = (visible && (in_range || attacking || hold)) ? (target_pos - pos) : vel;
                if (st.mode == Mode::Search && glm::length(vel) < 0.1f) look = fwd;
                look.y = 0.0f;
                if (glm::dot(look, look) > 1e-4f) {
                    const glm::quat want = glm::angleAxis(std::atan2(look.x, look.z), glm::vec3(0.0f, 1.0f, 0.0f));
                    const glm::quat now = tf->GetWorldRotation();
                    tf->SetWorldRotation(glm::slerp(now, want, std::min(1.0f, (attacking ? 12.0f : 7.0f) * dt)));
                }
            }
            if (st.mode == Mode::Patrol && st.follower.arrived() && st.pause <= 0.0f) {
                advance_patrol(st, ag->patrol_radius, id);
                st.pause = 0.8f + rnd01(id + st.patrol_leg * 31u) * 2.2f;
            }
        }

        // A camp fights together: those near one that noticed come too.
        for (const auto& [from, where] : calls)
            for (const auto& o : others) {
                if (glm::length(o.pos - from) > o.alert_radius) continue;
                auto it = agents_.find(o.id);
                if (it == agents_.end()) continue;
                Agent& a = *it->second;
                if (a.mode != Mode::Chase && a.ignore <= 0.0f) {
                    a.mode = Mode::Chase;
                    a.last_seen = where;
                    a.since_seen = 0.0f;
                    a.last_goal = glm::vec3(1e9f);
                }
            }

        for (auto it = agents_.begin(); it != agents_.end(); )
            it = seen.count(it->first) ? std::next(it) : agents_.erase(it);
    }

    size_t agent_count()  const { return agents_.size(); }
    size_t chasing()      const { return chasing_; }
    size_t attacks_this_frame() const { return attacked_; }
    size_t moving()       const { return active_; }
    size_t alerted()      const { return alerted_; }
    /// Where the agents that hit their target this frame stood (a HUD's hit direction).
    const std::vector<glm::vec3>& attack_sources() const { return sources_; }
    /// An agent's mode (Patrol when it is not an agent).
    Mode mode_of(uint32_t id) const {
        auto it = agents_.find(id);
        return it == agents_.end() ? Mode::Patrol : it->second->mode;
    }
    void   clear() { agents_.clear(); noises_.clear(); sources_.clear(); chasing_ = active_ = attacked_ = alerted_ = 0; }

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
    ///   * the follower's path -- waypoints steering toward the old world;
    ///   * where the target was last seen, and where it searches.
    /// The caller must translate the NavMesh by the same shift.
    void apply_origin_shift(const glm::vec3& d) {
        if (d == glm::vec3(0.0f)) return;
        for (auto& kv : agents_) {
            Agent& a = *kv.second;
            a.spawn       += d;
            a.patrol_goal += d;
            a.last_seen   += d;
            a.wander      += d;
            a.prev_target += d;
            if (a.last_goal.x < 1e8f) a.last_goal += d;   // leave the "never pathed" sentinel alone
            a.follower.translate(d);
        }
        for (auto& n : noises_) n += d;
    }

private:
    struct Agent {
        schizo::ai::PathFollower follower;
        Mode      mode = Mode::Patrol;
        glm::vec3 spawn{0.0f};
        glm::vec3 patrol_goal{0.0f};
        glm::vec3 last_goal{1e9f};     // forces a path on the first tick
        glm::vec3 last_seen{0.0f}, wander{0.0f}, prev_target{0.0f};
        bool      prev_target_known = false;
        int       patrol_leg = 0;
        float     since_seen = 0.0f, search_left = 0.0f, pause = 0.0f, ignore = 0.0f;
        float     cooldown = 0.0f, windup = 0.0f, stagger = 0.0f;
        glm::vec3 knock{0.0f};
        float     strafe = 1.0f, strafe_timer = 0.0f;
        float     last_health = -1.0f;   // to notice being hurt
        float     ground_offset = 0.0f;  // its height over the ground
        bool      ground_known = false;
        float     avoid_side = 1.0f;     // which way it went round the last thing in its way
    };

    Agent& state(uint32_t id, const glm::vec3& pos, float patrol_radius) {
        auto it = agents_.find(id);
        if (it != agents_.end()) return *it->second;
        auto a = std::make_unique<Agent>();
        a->spawn = pos;
        a->strafe = (id & 1u) ? 1.0f : -1.0f;
        Agent* raw = a.get();
        advance_patrol(*raw, patrol_radius, id);
        agents_[id] = std::move(a);
        return *raw;
    }

    // A random point around the spawn (a stable sequence per agent), so
    // patrolling is visibly a walk about the camp.
    static void advance_patrol(Agent& a, float r, uint32_t id) {
        a.patrol_leg = a.patrol_leg + 1;
        const float ang = rnd01(id * 7919u + static_cast<uint32_t>(a.patrol_leg) * 104729u) * 6.2831853f;
        const float rr = r * (0.4f + 0.6f * rnd01(id * 31u + static_cast<uint32_t>(a.patrol_leg) * 613u));
        a.patrol_goal = a.spawn + glm::vec3(std::cos(ang), 0.0f, std::sin(ang)) * rr;
        a.last_goal = glm::vec3(1e9f);   // force a re-path to the new leg
    }
    static float rnd01(uint32_t h) {
        h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
        return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x1000000);
    }
    static bool line_clear(const schizo::ecs::WorldRayFn& world, const glm::vec3& from, const glm::vec3& to) {
        const glm::vec3 d = to - from;
        const float l = glm::length(d);
        if (l < 0.5f) return true;
        float t = 0.0f;
        glm::vec3 n(0.0f);
        return !world(from, d / l, l - 0.4f, t, n);
    }
    static bool ground_below(const schizo::ecs::WorldRayFn& world, const glm::vec3& p, float& ground_y) {
        float t = 0.0f;
        glm::vec3 n(0.0f);
        if (!world(p + glm::vec3(0.0f, 2.5f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 8.0f, t, n)) return false;
        ground_y = p.y + 2.5f - t;
        return true;
    }
    /// Look ahead along `vel`; when something solid is there, turn to the
    /// nearest way past it (keeping to the side it chose last).
    glm::vec3 avoid(const schizo::ecs::WorldRayFn& world, Agent& st, const glm::vec3& pos, const glm::vec3& vel, float speed) {
        const glm::vec3 knee = pos + glm::vec3(0.0f, -st.ground_offset + 0.6f, 0.0f);
        const float ahead = 1.0f + speed * 0.35f;
        const glm::vec3 dir = vel / speed;
        auto blocked = [&](const glm::vec3& d) {
            float t = 0.0f;
            glm::vec3 n(0.0f);
            if (!world(knee, d, ahead, t, n)) return false;
            return std::fabs(n.y) < 0.7f;   // ground ahead (a slope) is not in the way
        };
        if (!blocked(dir)) return vel;
        static const float angles[] = {25.0f, 50.0f, 75.0f, 100.0f, 130.0f};
        for (float a : angles)
            for (float side : {st.avoid_side, -st.avoid_side}) {
                const float r = glm::radians(a * side);
                const glm::vec3 d(dir.x * std::cos(r) - dir.z * std::sin(r), 0.0f, dir.x * std::sin(r) + dir.z * std::cos(r));
                if (!blocked(d)) { st.avoid_side = side; return d * speed; }
            }
        return vel * 0.2f;   // boxed in: edge forward and let the slide work it out
    }

    std::unordered_map<uint32_t, std::unique_ptr<Agent>> agents_;
    std::vector<glm::vec3> noises_, sources_;
    size_t chasing_ = 0, active_ = 0, attacked_ = 0, alerted_ = 0;
};

}  // namespace schizo::editor
