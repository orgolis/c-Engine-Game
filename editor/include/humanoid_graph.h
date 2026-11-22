#pragma once
// ============================================================================
// humanoid_graph.h -- the animation graph a gameplay-driven character runs when
// it names no graph of its own, built from its Clips (`idle=...,walk=...`):
//
//   Base layer (the whole body)
//     Idle, Locomotion (a 2D blend space over the body's velocity in the model:
//     each walk, run, backpedal and strafe at the velocity its clip was measured
//     at -- in sync by the feet), CrouchIdle, CrouchWalk, Jump, Fall, Land, Dead.
//   Upper layer (the upper-body bone's subtree; the whole body without one)
//     None, Aim, MeleeReady.
//   Slots: "upper" (shots, swings, reloads, hits) and "full".
//
// Parameters (set by gameplay each frame): vx, vz (m/s in the model: +Z ahead,
// +X its left), speed, moving, airborne, jumped, landing, crouched, dead,
// aiming, melee, loco_rate, crouch_rate.
//
// Pure (anim_graph_runtime.h): checked by tools/animgraphrt_check.
// ============================================================================
#include "anim_graph_runtime.h"

#include <functional>
#include <memory>
#include <vector>

namespace schizo::editor {

struct HumanoidClips {
    int idle = -1, walk = -1, run = -1, crouch = -1, crouch_walk = -1, aim = -1;
    int walk_back = -1, strafe_left = -1, strafe_right = -1, jump = -1, fall = -1, land = -1;
    int melee_idle = -1, run_back = -1, strafe_run_left = -1, strafe_run_right = -1;
    int death = -1;
};

struct HumanoidGraph {
    std::shared_ptr<schizo::renderer::animgraph::GraphDef> def;
    // Parameters.
    int vx = -1, vz = -1, speed = -1, moving = -1, airborne = -1, jumped = -1, landing = -1, crouched = -1;
    int dead = -1, aiming = -1, melee = -1, loco_rate = -1, crouch_rate = -1;
    // States of the base layer, and slots.
    int s_idle = -1, s_loco = -1, s_crouch = -1, s_crouch_walk = -1, s_jump = -1, s_fall = -1, s_land = -1, s_dead = -1;
    int slot_upper = -1, slot_full = -1;
    // The locomotion space's samples at walking pace (one each way): below their
    // speed the body's velocity is taken out to them and the clips slow down.
    std::vector<int> walk_ring;
    bool sides = false, back = false;   // whether it has sideways / backwards clips
};

/// `measured(clip, fallback)`: the velocity (m/s, model x and z) a locomotion clip
/// was made for -- its measurement, else `fallback`. `upper_body`: whether the
/// model has an upper-body bone (its name) for the upper layer and slot.
inline HumanoidGraph build_humanoid_graph(const HumanoidClips& c, const std::function<glm::vec2(int, glm::vec2)>& measured,
                                          const std::string& upper_body) {
    using namespace schizo::renderer::animgraph;
    HumanoidGraph h;
    auto def = std::make_shared<GraphDef>();
    h.vx = def->add_param("vx", ParamType::Float);
    h.vz = def->add_param("vz", ParamType::Float);
    h.speed = def->add_param("speed", ParamType::Float);
    h.moving = def->add_param("moving", ParamType::Bool);
    h.airborne = def->add_param("airborne", ParamType::Bool);
    h.jumped = def->add_param("jumped", ParamType::Bool);
    h.landing = def->add_param("landing", ParamType::Bool);
    h.crouched = def->add_param("crouched", ParamType::Bool);
    h.dead = def->add_param("dead", ParamType::Bool);
    h.aiming = def->add_param("aiming", ParamType::Bool);
    h.melee = def->add_param("melee", ParamType::Bool);
    h.loco_rate = def->add_param("loco_rate", ParamType::Float, 1.0f);
    h.crouch_rate = def->add_param("crouch_rate", ParamType::Float, 1.0f);
    const int feet = def->sync_group("feet");
    auto cond = [](int p, Op op, float t = 0.0f) { return Condition{p, op, t}; };

    // ---- base layer ----
    LayerDef base;
    base.name = "Base";
    auto add_state = [&](LayerDef& l, StateDef s) { l.states.push_back(std::move(s)); return static_cast<int>(l.states.size()) - 1; };
    auto clip_state = [&](const char* name, int clip, bool loop, int rate_param = -1, int sync = -1) {
        StateDef s;
        s.name = name;
        s.kind = clip >= 0 ? NodeKind::Clip : NodeKind::None;
        s.clip = clip;
        s.loop = loop;
        s.rate_param = rate_param;
        s.sync = sync;
        return s;
    };
    const int idle = c.idle >= 0 ? c.idle : (c.walk >= 0 ? c.walk : 0);
    h.s_idle = add_state(base, clip_state("Idle", idle, true));

    // Locomotion: the samples where their clips were measured to go.
    StateDef loco;
    loco.name = "Locomotion";
    loco.kind = NodeKind::Blend2D;
    loco.param_x = h.vx;
    loco.param_y = h.vz;
    loco.radial = true;
    loco.rate_param = h.loco_rate;
    loco.sync = feet;
    std::vector<int> used;
    auto sample = [&](int clip, glm::vec2 fallback, bool ring) {
        if (clip < 0) return;
        for (int u : used) if (u == clip) return;
        used.push_back(clip);
        loco.space.samples.push_back({clip, measured(clip, fallback), 1.0f, true});
        if (ring) h.walk_ring.push_back(static_cast<int>(loco.space.samples.size()) - 1);
    };
    loco.space.samples.push_back({-1, glm::vec2(0.0f), 1.0f});   // the middle (keeps the origin inside the space)
    const int walk = c.walk >= 0 ? c.walk : c.run;
    sample(walk, {0.0f, 1.6f}, true);
    sample(c.run, {0.0f, 4.5f}, c.walk < 0);
    sample(c.walk_back, {0.0f, -1.4f}, true);
    sample(c.run_back, {0.0f, -2.6f}, c.walk_back < 0);
    sample(c.strafe_left, {1.6f, 0.0f}, true);
    sample(c.strafe_right, {-1.6f, 0.0f}, true);
    sample(c.strafe_run_left, {2.6f, 0.0f}, c.strafe_left < 0);
    sample(c.strafe_run_right, {-2.6f, 0.0f}, c.strafe_right < 0);
    h.sides = (c.strafe_left >= 0 || c.strafe_run_left >= 0) && (c.strafe_right >= 0 || c.strafe_run_right >= 0);
    h.back = c.walk_back >= 0 || c.run_back >= 0;
    loco.space.build();
    h.s_loco = walk >= 0 ? add_state(base, loco) : add_state(base, clip_state("Locomotion", idle, true));

    const int crouch = c.crouch >= 0 ? c.crouch : idle;
    const int crouch_walk = c.crouch_walk >= 0 ? c.crouch_walk : walk;
    h.s_crouch = add_state(base, clip_state("CrouchIdle", crouch, true));
    h.s_crouch_walk = add_state(base, clip_state("CrouchWalk", crouch_walk >= 0 ? crouch_walk : crouch, true, h.crouch_rate, feet));
    h.s_jump = c.jump >= 0 ? add_state(base, clip_state("Jump", c.jump, false)) : -1;
    h.s_fall = c.fall >= 0 ? add_state(base, clip_state("Fall", c.fall, true)) : -1;
    h.s_land = c.land >= 0 ? add_state(base, clip_state("Land", c.land, false)) : -1;
    h.s_dead = c.death >= 0 ? add_state(base, clip_state("Dead", c.death, false)) : -1;

    auto go = [&](LayerDef& l, int from, int to, std::vector<Condition> when, float blend, float exit = -1.0f) {
        if (to < 0 || (from < -1)) return;
        l.transitions.push_back({from, to, std::move(when), blend, exit});
    };
    // Dead first (from anywhere), and back to standing (a respawn).
    if (h.s_dead >= 0) {
        go(base, -1, h.s_dead, {cond(h.dead, Op::True)}, 0.12f);
        go(base, h.s_dead, h.s_idle, {cond(h.dead, Op::False)}, 0.0f);
    }
    // Into the air from the ground: a jump's clip once, else falling.
    const int air_in = h.s_jump >= 0 ? h.s_jump : h.s_fall;
    for (int from : {h.s_idle, h.s_loco, h.s_crouch, h.s_crouch_walk, h.s_land}) {
        if (from < 0) continue;
        if (h.s_jump >= 0) go(base, from, h.s_jump, {cond(h.airborne, Op::True), cond(h.jumped, Op::True)}, 0.1f);
        if (h.s_fall >= 0) go(base, from, h.s_fall, {cond(h.airborne, Op::True)}, 0.2f);
        else if (air_in >= 0) go(base, from, air_in, {cond(h.airborne, Op::True)}, 0.2f);
    }
    if (h.s_jump >= 0 && h.s_fall >= 0) go(base, h.s_jump, h.s_fall, {cond(h.airborne, Op::True)}, 0.15f, 1.0f);
    // Down again: a landing after a drop (standing), else straight on.
    for (int from : {h.s_jump, h.s_fall}) {
        if (from < 0) continue;
        if (h.s_land >= 0) go(base, from, h.s_land, {cond(h.airborne, Op::False), cond(h.landing, Op::True)}, 0.1f);
        go(base, from, h.s_crouch_walk, {cond(h.airborne, Op::False), cond(h.crouched, Op::True), cond(h.moving, Op::True)}, 0.2f);
        go(base, from, h.s_crouch, {cond(h.airborne, Op::False), cond(h.crouched, Op::True)}, 0.2f);
        go(base, from, h.s_loco, {cond(h.airborne, Op::False), cond(h.moving, Op::True)}, 0.2f);
        go(base, from, h.s_idle, {cond(h.airborne, Op::False)}, 0.2f);
    }
    if (h.s_land >= 0) {
        go(base, h.s_land, h.s_loco, {cond(h.moving, Op::True), cond(h.crouched, Op::False)}, 0.2f);
        go(base, h.s_land, h.s_crouch_walk, {cond(h.moving, Op::True), cond(h.crouched, Op::True)}, 0.2f);
        go(base, h.s_land, h.s_crouch, {cond(h.crouched, Op::True)}, 0.2f, 1.0f);
        go(base, h.s_land, h.s_idle, {}, 0.2f, 1.0f);
    }
    // On the ground: standing, moving, crouched (crouch walk and walk in step).
    go(base, h.s_idle, h.s_crouch, {cond(h.crouched, Op::True)}, 0.25f);
    go(base, h.s_idle, h.s_loco, {cond(h.moving, Op::True)}, 0.2f);
    go(base, h.s_loco, h.s_crouch_walk, {cond(h.crouched, Op::True), cond(h.moving, Op::True)}, 0.25f);
    go(base, h.s_loco, h.s_crouch, {cond(h.crouched, Op::True)}, 0.25f);
    go(base, h.s_loco, h.s_idle, {cond(h.moving, Op::False)}, 0.25f);
    go(base, h.s_crouch, h.s_idle, {cond(h.crouched, Op::False)}, 0.25f);
    go(base, h.s_crouch, h.s_crouch_walk, {cond(h.moving, Op::True)}, 0.2f);
    go(base, h.s_crouch_walk, h.s_loco, {cond(h.crouched, Op::False), cond(h.moving, Op::True)}, 0.25f);
    go(base, h.s_crouch_walk, h.s_idle, {cond(h.crouched, Op::False)}, 0.25f);
    go(base, h.s_crouch_walk, h.s_crouch, {cond(h.moving, Op::False)}, 0.25f);
    def->layers.push_back(std::move(base));

    // ---- upper layer: a gun (or a blade) held ready while aiming ----
    LayerDef upper;
    upper.name = "Upper";
    upper.mask_bone = upper_body;
    StateDef none;
    none.name = "None";
    none.kind = NodeKind::None;
    const int u_none = add_state(upper, none);
    const int u_aim = c.aim >= 0 ? add_state(upper, clip_state("Aim", c.aim, true)) : -1;
    const int u_melee = c.melee_idle >= 0 ? add_state(upper, clip_state("MeleeReady", c.melee_idle, true)) : (u_aim >= 0 ? u_aim : -1);
    go(upper, -1, u_none, {cond(h.dead, Op::True)}, 0.1f);
    go(upper, -1, u_none, {cond(h.aiming, Op::False)}, 0.2f);
    if (upper_body.empty()) {
        // No upper body of its own: held ready only standing still (the whole body).
        go(upper, -1, u_none, {cond(h.moving, Op::True)}, 0.2f);
        go(upper, -1, u_none, {cond(h.airborne, Op::True)}, 0.2f);
        go(upper, -1, u_none, {cond(h.crouched, Op::True)}, 0.2f);
    }
    auto ready_when = [&](bool melee) {
        std::vector<Condition> w = {cond(h.aiming, Op::True), cond(h.melee, melee ? Op::True : Op::False), cond(h.dead, Op::False)};
        if (upper_body.empty()) {   // (the same as what sends it back, the other way round)
            w.push_back(cond(h.moving, Op::False));
            w.push_back(cond(h.airborne, Op::False));
            w.push_back(cond(h.crouched, Op::False));
        }
        return w;
    };
    if (u_aim >= 0) go(upper, -1, u_aim, ready_when(false), 0.15f);
    if (u_melee >= 0 && u_melee != u_aim) go(upper, -1, u_melee, ready_when(true), 0.15f);
    def->layers.push_back(std::move(upper));

    def->slots.push_back({"upper", upper_body});
    def->slots.push_back({"full", ""});
    h.slot_upper = 0;
    h.slot_full = 1;
    h.def = def;
    return h;
}

/// The parameters gameplay sets, found by name in any graph (a graph that lacks
/// one just is not told it), and its locomotion space: a 2D state of the first
/// layer over vx and vz.
struct GameplayParams {
    int vx = -1, vz = -1, speed = -1, moving = -1, airborne = -1, jumped = -1, landing = -1, crouched = -1;
    int dead = -1, aiming = -1, melee = -1;
    int loco_state = -1;
    static GameplayParams find(const schizo::renderer::animgraph::GraphDef& def) {
        GameplayParams p;
        p.vx = def.param("vx"); p.vz = def.param("vz"); p.speed = def.param("speed"); p.moving = def.param("moving");
        p.airborne = def.param("airborne"); p.jumped = def.param("jumped"); p.landing = def.param("landing");
        p.crouched = def.param("crouched"); p.dead = def.param("dead"); p.aiming = def.param("aiming"); p.melee = def.param("melee");
        if (!def.layers.empty())
            for (size_t i = 0; i < def.layers[0].states.size(); ++i) {
                const auto& sd = def.layers[0].states[i];
                if (sd.kind == schizo::renderer::animgraph::NodeKind::Blend2D && sd.param_x == p.vx && sd.param_y == p.vz && p.vx >= 0) {
                    p.loco_state = static_cast<int>(i);
                    break;
                }
            }
        return p;
    }
};

/// Where in a locomotion space (samples at the velocities their clips move at,
/// model x and z) to look for velocity `v`: below the walking pace of its way, out
/// to that pace (`*slow` < 1: how much slower the clips go); a way it has no clips
/// for (more than 60 degrees off them), turned onto the nearest it has (`*turn`:
/// radians the legs turn toward where it really goes, at most 50 degrees). The
/// walking pace of a way: its slowest clip there (none slower within 20 degrees).
inline glm::vec2 loco_query(const schizo::renderer::animgraph::BlendSpace& space, glm::vec2 v, float* slow, float* turn) {
    *slow = 1.0f;
    *turn = 0.0f;
    const float speed = glm::length(v);
    if (speed < 1e-4f || space.samples.empty()) return v;
    glm::vec2 d = v / speed;
    float best = 10.0f;
    glm::vec2 nearest = d;
    std::vector<glm::vec2> ring;
    for (const auto& s : space.samples) {
        if (s.clip < 0 || glm::length(s.at) < 1e-4f) continue;
        const glm::vec2 u = glm::normalize(s.at);
        const float a = std::acos(std::clamp(glm::dot(u, d), -1.0f, 1.0f));
        if (a < best) { best = a; nearest = u; }
        bool slowest = true;
        for (const auto& o : space.samples)
            if (o.clip >= 0 && &o != &s && glm::length(o.at) > 1e-4f && glm::dot(glm::normalize(o.at), u) > 0.94f && glm::length(o.at) < glm::length(s.at) - 1e-4f)
                slowest = false;
        if (slowest) ring.push_back(s.at);
    }
    if (best > 3.14159265f / 3.0f) {
        const float signed_a = std::atan2(d.x * nearest.y - d.y * nearest.x, glm::dot(d, nearest));
        *turn = std::clamp(signed_a, -0.8727f, 0.8727f);
        d = nearest;
    }
    // Where the ray meets the ring: on a walking sample, or between two neighbouring ones.
    float at = 0.0f;
    for (size_t i = 0; i < ring.size(); ++i) {
        const glm::vec2 A = ring[i];
        if (glm::dot(glm::normalize(A), d) > 0.9999f) at = std::max(at, glm::length(A));
        for (size_t j = i + 1; j < ring.size(); ++j) {
            const glm::vec2 B = ring[j];
            if (glm::dot(glm::normalize(A), glm::normalize(B)) <= -0.2f) continue;   // (not across the middle)
            const glm::vec2 e = B - A;
            const float den = d.x * (-e.y) - d.y * (-e.x);
            if (std::abs(den) < 1e-9f) continue;
            const float s = (A.x * (-e.y) - A.y * (-e.x)) / den;
            const float u = (d.x * A.y - d.y * A.x) / den;
            if (s > 1e-4f && u >= -1e-4f && u <= 1.0f + 1e-4f) at = std::max(at, s);
        }
    }
    if (at > 1e-4f && speed < at) {
        *slow = speed / at;
        return d * at;
    }
    return d * speed;
}

/// The humanoid graph's locomotion space (loco_query).
inline glm::vec2 humanoid_loco_query(const HumanoidGraph& h, glm::vec2 v, float* slow, float* turn) {
    *slow = 1.0f;
    *turn = 0.0f;
    if (!h.def || h.s_loco < 0) return v;
    const auto& sd = h.def->layers[0].states[static_cast<size_t>(h.s_loco)];
    if (sd.kind != schizo::renderer::animgraph::NodeKind::Blend2D) return v;
    return loco_query(sd.space, v, slow, turn);
}

}  // namespace schizo::editor
