#pragma once
// ============================================================================
// melee_play.h -- melee combat in Play (0.9.1).
// ============================================================================
//
// The engine's melee (ecs/gameplay_melee.h) decides; this shows and moves it:
//
//   - the player's fighter comes from the blade in its hand (a weapon with a
//     `moveset`): fire is the light attack, aim the heavy (held: charged),
//     guard held, parry, the dash key the dodge, lock_on a target; an NPC's
//     from its Melee Fighter (its `weapon`, else its own moveset), its
//     presses from its AI (npc_agents.h);
//   - every fighter's frame, then what the blades did: hit-stop (both stand
//     still a moment), kept for the host to show (sparks, blood, a shake);
//   - each fighter's body: its moves played on the whole body with their root
//     motion, its stance and its guard on the upper body (the legs walk on);
//   - after the body is posed: the root motion (world) that carries it, and
//     the blade where its hand holds it (the weapon's grip x its hilt and tip);
//   - its turn: at once to the way a move began (a roll's way), else toward
//     whom its move tracks; a body that cannot be turned (first person) rolls
//     that way all the same (its root motion turned);
//   - lock-on: the foe nearest the middle of the view, kept while it stands
//     and is near.

#include "ecs/world.h"
#include "ecs/components.h"
#include "ecs/gameplay_attributes.h"
#include "ecs/gameplay_combat.h"
#include "ecs/gameplay_melee.h"
#include "ecs/gameplay_tags.h"
#include "ecs/gameplay_weapons.h"
#include "imported_skinned_actor.h"
#include "input/action_map.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace schizo::editor {

class MeleePlay {
public:
    /// The angle (radians about +Y) that turns `from` toward `to` (both flattened).
    static float yaw_between(glm::vec3 from, glm::vec3 to) {
        from.y = 0.0f;
        to.y = 0.0f;
        if (glm::length(from) < 1e-4f || glm::length(to) < 1e-4f) return 0.0f;
        from = glm::normalize(from);
        to = glm::normalize(to);
        return std::atan2(from.z * to.x - from.x * to.z, glm::dot(from, to));
    }

    // ---- the player ---------------------------------------------------------------------
    /// The player's fighter from the weapon in its hand: one with a moveset gives it one
    /// (its damage and type), another takes the one it gave away. True while drawn.
    bool arm_player(ecs::World& w, ecs::Entity p, const std::string& weapon_id) {
        player_ = p;
        player_weapon_ = weapon_id;
        const ecs::WeaponDef* wd = weapon_id.empty() ? nullptr : ecs::find_weapon(weapon_id);
        const bool drawn = wd && !wd->moveset.empty() && ecs::find_moveset(wd->moveset) != nullptr && w.valid(p);
        if (drawn) {
            if (!w.has<ecs::MeleeFighter>(p)) { w.add<ecs::MeleeFighter>(p, ecs::MeleeFighter{}); gave_player_ = true; }
            auto& f = w.get<ecs::MeleeFighter>(p);
            if (f.moveset != wd->moveset) {
                const float r = f.blade_radius;
                f = ecs::MeleeFighter{};
                f.moveset = wd->moveset;
                f.blade_radius = r;
            }
            f.damage = wd->damage;
            f.damage_type = wd->damage_type;
        } else if (gave_player_ && w.valid(p) && w.has<ecs::MeleeFighter>(p)) {
            w.remove<ecs::MeleeFighter>(p);
            gave_player_ = false;
            lock_ = ecs::null_entity;
        } else if (!drawn && w.valid(p) && w.has<ecs::MeleeFighter>(p) && !gave_player_) {
            // (one authored on the player: a blade sheathed, it stands)
            auto& f = w.get<ecs::MeleeFighter>(p);
            if (f.act != ecs::MeleeFighter::Act::Ready) { f.act = ecs::MeleeFighter::Act::Ready; f.move = -1; f.clip.clear(); }
        }
        player_drawn_ = drawn;
        return drawn;
    }
    bool player_drawn() const { return player_drawn_; }

    /// The player's presses (and how it moves) as a fighter's input.
    ecs::MeleeInput player_input(const input::ActionStates* a, const glm::vec3& move_dir, bool running, bool airborne) const {
        ecs::MeleeInput in;
        in.move_dir = move_dir;
        in.moving = glm::length(move_dir) > 0.2f;
        in.running = running;
        in.airborne = airborne;
        in.target = lock_;
        if (!a) return in;
        in.light = a->pressed("fire");
        in.heavy = a->pressed("aim");
        in.heavy_held = a->down("aim");
        in.guard = a->down("guard");
        in.parry = a->pressed("parry");
        in.dodge = a->pressed("dash");
        return in;
    }

    // ---- lock-on ----------------------------------------------------------------------------
    /// lock_on pressed: the standing foe within 20 m and 45 degrees of the view nearest its
    /// middle, or let go of the one held. Kept while it stands within 30 m.
    void update_lock(ecs::World& w, bool pressed, const glm::vec3& eye, const glm::vec3& view) {
        if (lock_ != ecs::null_entity && !standing(w, lock_)) lock_ = ecs::null_entity;
        if (lock_ != ecs::null_entity && w.valid(player_))
            if (const auto* pt = w.try_get<ecs::Transform>(player_))
                if (const auto* lt = w.try_get<ecs::Transform>(lock_); !lt || glm::length(lt->position - pt->position) > 30.0f) lock_ = ecs::null_entity;
        if (!player_drawn_ || !standing(w, player_)) { lock_ = ecs::null_entity; return; }   // (sheathed, or down)
        if (!pressed) return;
        if (lock_ != ecs::null_entity) { lock_ = ecs::null_entity; return; }
        const std::string team = team_of(w, player_);
        float best = 1e9f;
        const glm::vec3 v = glm::length(view) > 1e-4f ? glm::normalize(view) : glm::vec3(0.0f, 0.0f, -1.0f);
        w.each<ecs::CombatActor, ecs::Transform>([&](ecs::Entity e, ecs::CombatActor& ca, ecs::Transform& t) {
            if (e == player_ || !standing(w, e) || (!team.empty() && team_of(w, e) == team)) return;
            if (ecs::damage_owner(w, e) != e) return;   // a part of someone (a hitbox): its owner is the one
            const glm::vec3 c = ecs::combat_hurtbox(ca, t.position).center;
            const glm::vec3 to = c - eye;
            const float d = glm::length(to);
            if (d > 20.0f || d < 0.3f) return;
            const float cosang = glm::dot(to / d, v);
            if (cosang < std::cos(glm::radians(45.0f))) return;
            const float score = (1.0f - cosang) * 20.0f + d * 0.1f;   // the middle of the view first
            if (score < best) { best = score; lock_ = e; }
        });
    }
    ecs::Entity lock_target() const { return lock_; }
    /// Where the lock-on marker goes (the target's chest), false when none.
    bool lock_point(ecs::World& w, glm::vec3& out) const {
        if (lock_ == ecs::null_entity || !w.valid(lock_)) return false;
        const auto* t = w.try_get<ecs::Transform>(lock_);
        if (!t) return false;
        out = t->position + glm::vec3(0.0f, 1.2f, 0.0f);
        if (const auto* ca = w.try_get<ecs::CombatActor>(lock_)) out = ecs::combat_hurtbox(*ca, t->position).center;
        return true;
    }

    // ---- turning --------------------------------------------------------------------------
    /// How far (radians about +Y) a fighter facing `fwd` turns this frame: at once to the
    /// way its move began, else toward whom its move tracks. `can_turn` false (a first-
    /// person body): it does not turn, a dodge's root motion goes that way instead.
    float turn_of(ecs::World& w, ecs::Entity e, const glm::vec3& fwd, float dt, bool can_turn) {
        auto* f = w.valid(e) ? w.try_get<ecs::MeleeFighter>(e) : nullptr;
        if (!f) return 0.0f;
        if (glm::length(f->turn_to) > 0.1f) {
            const float a = yaw_between(fwd, f->turn_to);
            f->turn_to = glm::vec3(0.0f);
            if (can_turn) return a;
            if (const ecs::MovesetDef* ms = ecs::find_moveset(f->moveset);
                ms && f->move >= 0 && f->move < static_cast<int>(ms->moves.size()) &&
                ms->moves[static_cast<size_t>(f->move)].kind == ecs::MeleeMove::Kind::Dodge)
                motion_turn_[e] = {f->move, a};
        }
        if (!can_turn || f->face == ecs::null_entity || !w.valid(f->face)) return 0.0f;
        const auto* me = w.try_get<ecs::Transform>(e);
        const auto* it = w.try_get<ecs::Transform>(f->face);
        if (!me || !it) return 0.0f;
        const float a = yaw_between(fwd, it->position - me->position);
        const float step = glm::radians(f->face_rate) * dt;
        return std::clamp(a, -step, step);
    }
    /// A move plays (the controls do not move the player).
    bool player_in_move(ecs::World& w) const {
        const auto* f = w.valid(player_) ? w.try_get<ecs::MeleeFighter>(player_) : nullptr;
        return f && player_drawn_ && (f->act == ecs::MeleeFighter::Act::Move || f->act == ecs::MeleeFighter::Act::Charging);
    }
    /// The player's root motion since the last call (world).
    glm::vec3 take_player_root_motion() { return take_root_motion(player_); }
    /// A fighter's root motion since the last call (world): an NPC's, for the host to move it.
    glm::vec3 take_root_motion(ecs::Entity e) {
        auto it = root_.find(e);
        if (it == root_.end()) return glm::vec3(0.0f);
        const glm::vec3 d = it->second;
        root_.erase(it);
        return d;
    }

    // ---- the frame ------------------------------------------------------------------------
    /// Every standing fighter's frame (the player's input; an NPC's from `npc_input`),
    /// then what the blades did: hit-stop for both, kept for take_feedback().
    template <class NpcInput>
    void step(ecs::World& w, ecs::GameplayEventBus* bus, float dt, const ecs::MeleeInput& player_in, NpcInput&& npc_input) {
        for (auto it = hitstop_.begin(); it != hitstop_.end();) {
            it->second -= dt;
            if (it->second <= 0.0f) it = hitstop_.erase(it);
            else ++it;
        }
        std::vector<std::pair<ecs::Entity, ecs::MeleeInput>> ins;
        w.each<ecs::MeleeFighter>([&](ecs::Entity e, ecs::MeleeFighter& f) {
            // An NPC's weapon: its moves, its damage.
            if (!f.weapon.empty())
                if (const ecs::WeaponDef* wd = ecs::find_weapon(f.weapon); wd && !wd->moveset.empty()) {
                    if (f.moveset != wd->moveset) { f.moveset = wd->moveset; f.act = ecs::MeleeFighter::Act::Ready; f.move = -1; f.clip.clear(); }
                    f.damage = wd->damage;
                    f.damage_type = wd->damage_type;
                }
            if (e == player_ && !player_drawn_) return;
            if (!standing(w, e)) {   // the dead fight no more
                f.act = ecs::MeleeFighter::Act::Ready;
                f.move = -1;
                f.clip.clear();
                return;
            }
            ins.push_back({e, e == player_ ? player_in : npc_input(e)});
        });
        for (auto& [e, in] : ins)
            if (auto* f = w.try_get<ecs::MeleeFighter>(e)) ecs::step_melee(w, e, *f, in, hitstop_.count(e) ? 0.0f : dt, bus);
        auto& hits = ecs::melee_hits(w).list;
        for (const auto& h : hits) {
            using K = ecs::MeleeHitInfo::Kind;
            float stop = 0.0f;
            switch (h.kind) {
                case K::Hit:         stop = h.poise >= 40.0f ? 0.10f : 0.065f; break;
                case K::Riposte:     stop = 0.16f; break;
                case K::Blocked:     stop = 0.05f; break;
                case K::GuardBroken: stop = 0.12f; break;
                case K::Parried:     stop = 0.12f; break;
                default: break;
            }
            if (stop > 0.0f) {
                hitstop_[h.attacker] = std::max(hitstop_[h.attacker], stop);
                hitstop_[h.target] = std::max(hitstop_[h.target], stop);
            }
            feedback_.push_back(h);
        }
        hits.clear();
        // A dodge turned without its body (first person) lasts as long as the dodge.
        for (auto it = motion_turn_.begin(); it != motion_turn_.end();) {
            const auto* f = w.valid(it->first) ? w.try_get<ecs::MeleeFighter>(it->first) : nullptr;
            if (!f || f->act != ecs::MeleeFighter::Act::Move || f->move != it->second.move) it = motion_turn_.erase(it);
            else ++it;
        }
    }
    /// What the blades did since the last call (to show: sparks, blood, a shake).
    std::vector<ecs::MeleeHitInfo> take_feedback() { std::vector<ecs::MeleeHitInfo> f; f.swap(feedback_); return f; }
    /// 0 while it is held still by a hit (its body's clip waits too), else 1.
    float time_scale(ecs::Entity e) const { return hitstop_.count(e) ? 0.0f : 1.0f; }
    /// The weapon a fighter swings (the player's in hand, an NPC's own).
    std::string weapon_of(ecs::World& w, ecs::Entity e) const {
        if (e == player_) return player_weapon_;
        const auto* f = w.valid(e) ? w.try_get<ecs::MeleeFighter>(e) : nullptr;
        return f ? f->weapon : std::string();
    }
    /// It fights with moves now (the player: with a blade drawn).
    bool fighting(ecs::World& w, ecs::Entity e) const {
        const auto* f = w.valid(e) ? w.try_get<ecs::MeleeFighter>(e) : nullptr;
        if (!f || f->moveset.empty() || !ecs::find_moveset(f->moveset)) return false;
        return e != player_ || player_drawn_;
    }

    // ---- its body ---------------------------------------------------------------------------
    /// Play what the fighter says on its body: a move on the whole body (its root motion
    /// with it), the stance or the guard on the upper body (the legs walk on). False when
    /// it does not fight with moves (or is dead): the body's own clips as before.
    bool drive_body(ecs::World& w, ecs::Entity owner, ImportedSkinnedActor& actor, uint32_t body_id, bool dead) {
        auto* f = w.valid(owner) ? w.try_get<ecs::MeleeFighter>(owner) : nullptr;
        if (dead || !f || !fighting(w, owner)) {
            if (auto it = bodies_.find(body_id); it != bodies_.end()) {
                if (!dead) actor.stop_montage(it->second.upper ? "upper" : "full", 0.2f);
                bodies_.erase(it);
            }
            return false;
        }
        Body& b = bodies_[body_id];
        const bool upper = f->act == ecs::MeleeFighter::Act::Ready || f->act == ecs::MeleeFighter::Act::Guarding;
        const int c = actor.find_clip(f->clip);
        if (c >= 0) f->clip_length = actor.clip_duration(static_cast<size_t>(c));
        if (f->clip_fresh || f->clip != b.clip || upper != b.upper) {
            if (c >= 0) {
                if (upper) {
                    if (!b.upper && !b.clip.empty()) actor.stop_montage("full", 0.2f);
                    actor.play_montage(c, "upper", 1.0f, 0.15f, 0.2f, true, false);
                } else {
                    if (b.upper && !b.clip.empty()) actor.stop_montage("upper", 0.1f);
                    actor.play_montage(c, "full", f->clip_rate, 0.08f, 0.2f, f->clip_loop, f->root_motion);
                }
            } else if (!f->clip.empty() && missing_.insert(actor.name() + "|" + f->clip).second) {
                spdlog::warn("[melee] '{}' has no clip '{}' (its moveset '{}')", actor.name(), f->clip, f->moveset);
            }
            static const bool log_bodies = std::getenv("GWS_TEST_LOG_MELEE") != nullptr;
            if (log_bodies)
                spdlog::info("[test] melee body {}: '{}' (clip {}) on {}{}{}", body_id, f->clip, c, upper ? "upper (looping)" : "full",
                             f->root_motion && !upper ? ", root motion" : "", actor.has_graph() ? "" : " -- no graph");
            b.clip = f->clip;
            b.upper = upper;
        }
        return true;
    }
    /// After the body is posed (its model set): its root motion (world) and its blade.
    void after_pose(ecs::World& w, ecs::Entity owner, ImportedSkinnedActor& actor) {
        auto* f = w.valid(owner) ? w.try_get<ecs::MeleeFighter>(owner) : nullptr;
        if (!f || !fighting(w, owner)) return;
        f->forward = glm::vec3(actor.model() * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));   // a character model faces +Z
        glm::vec3 d = actor.take_root_motion();
        if (d != glm::vec3(0.0f)) {
            d = glm::vec3(actor.model() * glm::vec4(d, 0.0f));
            if (auto mt = motion_turn_.find(owner); mt != motion_turn_.end())
                d = glm::angleAxis(mt->second.angle, glm::vec3(0.0f, 1.0f, 0.0f)) * d;
            root_[owner] += d;
        }
        const std::string wid = weapon_of(w, owner);
        const ecs::WeaponDef* wd = wid.empty() ? nullptr : ecs::find_weapon(wid);
        if (!wd) return;
        const int bone = actor.find_bone(wd->grip_bone.empty() ? std::string("weapon_socket") : wd->grip_bone);
        if (bone < 0) return;
        const glm::mat4 grip = glm::translate(glm::mat4(1.0f), wd->grip_pos) * glm::mat4_cast(glm::quat(glm::radians(wd->grip_rot)));
        const glm::mat4 m = actor.bone_world(bone) * grip;
        f->blade_a = glm::vec3(m * glm::vec4(wd->blade_hilt, 1.0f));
        f->blade_b = glm::vec3(m * glm::vec4(wd->blade_tip, 1.0f));
        f->blade = true;
    }

    void reset() {
        bodies_.clear();
        root_.clear();
        hitstop_.clear();
        feedback_.clear();
        motion_turn_.clear();
        lock_ = ecs::null_entity;
        player_ = ecs::null_entity;
        player_weapon_.clear();
        gave_player_ = player_drawn_ = false;
    }

private:
    struct Body { std::string clip; bool upper = false; };
    struct MotionTurn { int move = -1; float angle = 0.0f; };
    static std::string team_of(ecs::World& w, ecs::Entity e) {
        if (!w.valid(e)) return {};
        if (const auto* t = w.try_get<ecs::GameplayTags>(e))
            for (const auto& s : t->tags) if (s.rfind("team.", 0) == 0) return s;
        return {};
    }
    static bool standing(ecs::World& w, ecs::Entity e) {
        if (!w.valid(e)) return false;
        if (const auto* t = w.try_get<ecs::GameplayTags>(e); t && t->has("state.dead")) return false;
        const auto* a = w.try_get<ecs::AttributeSet>(e);
        const auto* h = a ? a->find("Health") : nullptr;
        return !h || h->current > 0.0f;
    }

    ecs::Entity player_ = ecs::null_entity, lock_ = ecs::null_entity;
    std::string player_weapon_;
    bool gave_player_ = false, player_drawn_ = false;
    std::unordered_map<uint32_t, Body> bodies_;
    std::unordered_map<ecs::Entity, glm::vec3> root_;
    std::unordered_map<ecs::Entity, float> hitstop_;
    std::unordered_map<ecs::Entity, MotionTurn> motion_turn_;
    std::vector<ecs::MeleeHitInfo> feedback_;
    std::unordered_set<std::string> missing_;
};

}  // namespace schizo::editor
