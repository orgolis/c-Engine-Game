#pragma once

// ============================================================================
// C5 · Casting abilities -- cast time, channelling, targeting, a hotbar.
// ============================================================================
//
// gameplay_abilities.h has the primitive (cost, cooldown, tag gates, effects
// applied at once). This is what a player casts:
//
//   AbilityDef   -- data (`ability <id>` blocks in .items files): cost,
//                   cooldown, tags, a cast time before it goes off (optionally
//                   broken by moving), or a channel that keeps going off while
//                   the key is held; who it reaches (the caster, the one aimed
//                   at, everyone around the caster, everyone around the aimed
//                   point); damage and effects for them and for the caster;
//                   a VFX name handed out with its events.
//   AbilityBar   -- the authorable component: which ability is on which slot
//                   (keys 1..8 in Play), plus the cooldowns and the cast now.
//   step_ability_bar -- one frame: start, wind up, fire, channel, cool down.
//
// An aimed or area-at-aim ability with a `speed` flies (AbilityShots on the
// caster, moved by tick_ability_shots): it falls by its `arc`, and takes
// effect where it meets someone (not of the caster's team) or the world -- an
// area one goes off there too, or where its flight ends. Without a speed it
// takes effect at once, as before.
//
// Every stage publishes a gameplay event (ability.cast, ability.fire,
// ability.hit, ability.interrupted, ability.end) carrying the ability id; the
// AbilityFired list says where each one went off -- the hooks for effects,
// sounds and scripts. Checked by tools/gameplay_check.
//
// Since 0.8.68: an ability puts statuses on who it reaches (`inflict burning 2`)
// or on its caster (`self_status hasted`) -- gameplay_status.h; `target cone`
// reaches those in front within `radius` and `angle`; `zone 6 1` leaves its
// area behind for 6 s, going off again every second (`zone_hits allies`: on the
// caster's side instead); `labels` are free words a game's rules look for
// (gameplay_perks.h).

#include "world.h"
#include "gameplay_abilities.h"
#include "gameplay_attributes.h"
#include "gameplay_combat.h"
#include "gameplay_damage.h"
#include "gameplay_effects.h"
#include "gameplay_events.h"
#include "gameplay_tags.h"
#include "gameplay_weapons.h"   // hitscan_target, WorldRayFn
#include "gameplay_skills.h"    // learning a skill grants abilities
#include "gameplay_status.h"    // what abilities put on who they reach
#include "authorable_components.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <cstdint>
#include <string>
#include <vector>

namespace schizo::ecs {

enum class AbilityTargeting : uint8_t { Self, Aimed, AreaAroundSelf, AreaAtAim, Cone };

inline const char* ability_targeting_name(AbilityTargeting t) {
    switch (t) {
        case AbilityTargeting::Self:           return "self";
        case AbilityTargeting::Aimed:          return "aimed";
        case AbilityTargeting::AreaAroundSelf: return "area";
        case AbilityTargeting::AreaAtAim:      return "area_at_aim";
        case AbilityTargeting::Cone:           return "cone";
    }
    return "self";
}
inline AbilityTargeting parse_ability_targeting(const std::string& s) {
    if (s == "aimed")       return AbilityTargeting::Aimed;
    if (s == "area")        return AbilityTargeting::AreaAroundSelf;
    if (s == "area_at_aim") return AbilityTargeting::AreaAtAim;
    if (s == "cone")        return AbilityTargeting::Cone;
    return AbilityTargeting::Self;
}

// One change an ability makes to an attribute: at once, or for a while (and
// undone after).
struct AbilityEffect {
    std::string attribute;
    float       amount = 0.0f;
    float       duration = 0.0f;   // 0: at once
};

inline GameplayEffect to_gameplay_effect(const AbilityEffect& e, const std::string& name) {
    if (e.duration <= 0.0f) return make_instant(name, e.attribute, e.amount);
    GameplayEffect fx;
    fx.name = "ability:" + name + ":" + e.attribute;
    fx.duration_type = EffectDurationType::Duration;
    fx.duration = e.duration;
    fx.revert_on_end = true;
    fx.refresh_existing = true;
    fx.modifiers.push_back({e.attribute, EffectOp::Add, e.amount});
    return fx;
}

struct AbilityDef {
    std::string id, name;
    std::string cost_attribute;            // "" = free
    float       cost = 0.0f;
    float       cooldown = 0.0f;
    std::vector<std::string> require_tags, block_tags;
    float       cast_time = 0.0f;          // wind-up before it goes off
    bool        cast_while_moving = true;  // false: moving breaks the cast
    float       channel_time = 0.0f;       // > 0: goes off every period while held, this long at most
    float       channel_period = 0.5f;
    AbilityTargeting targeting = AbilityTargeting::Self;
    float       range = 25.0f;             // aimed: how far
    float       radius = 3.0f;             // areas
    float       damage = 0.0f;             // to each one reached
    std::string damage_type = "magic";
    std::vector<AbilityEffect> self_effects, target_effects;
    std::string vfx;                       // handed out with the events, for effects to pick up
    float       speed = 0.0f;              // > 0: aimed / area-at-aim abilities fly at this speed (m/s)
    float       arc = 0.0f;                // a flying one falls by this (m/s^2): thrown things
    float       spread = 0.0f;             // aimed: degrees it may go off its aim (a cone; 0: dead on)
    float       knockdown = 0.0f;          // > 0: characters it leaves alive within this (m) are knocked down (a blast)
    std::string about;                     // what it does, in a sentence
    // Moves the caster as it goes off (the player): `move blink 10` -- there at
    // once, up to 10 m along the aim, stopping short of walls; `move dash 12`
    // -- a 12 m dash along the aim; `move leap 9 7` -- thrown up at 9 m/s and
    // forward at 7.
    enum class Move : uint8_t { None, Blink, Dash, Leap };
    Move        move = Move::None;
    float       move_a = 0.0f, move_b = 0.0f;
    // Elemental powers (0.8.68).
    struct StatusPut { std::string id; int stacks = 1; };
    std::vector<StatusPut> inflict;        // on each one reached
    std::vector<StatusPut> self_statuses;  // on the caster
    float       angle = 90.0f;             // cone: how wide (degrees)
    float       zone_time = 0.0f;          // > 0: its area stays this long, going off every zone_period
    float       zone_period = 1.0f;
    enum class ZoneHits : uint8_t { Enemies, Allies, All };
    ZoneHits    zone_hits = ZoneHits::Enemies;
    std::vector<std::string> labels;       // free words the game's rules can look for
    bool has_label(const std::string& l) const { return std::find(labels.begin(), labels.end(), l) != labels.end(); }
};

inline std::vector<AbilityDef>& ability_def_registry() { static std::vector<AbilityDef> r; return r; }
inline void register_ability_def(const AbilityDef& d) {
    for (auto& e : ability_def_registry()) if (e.id == d.id) { e = d; return; }
    ability_def_registry().push_back(d);
}
inline const AbilityDef* find_ability_def(const std::string& id) {
    for (const auto& e : ability_def_registry()) if (e.id == id) return &e;
    return nullptr;
}

struct AbilityBar {
    std::vector<std::string> slots;        // ability ids, slot i on key i+1
    int max_slots = 0;                     // > 0: this many slots; abilities learned beyond them are known, to equip
    std::vector<std::string> known;        // every ability it has (on the bar or not)
    // Runtime (not saved)
    std::vector<float> cooldown;           // per slot, seconds left
    int   casting = -1;                    // the slot being cast or channelled
    float cast_timer = 0.0f;               // wind-up done so far
    bool  channelling = false;
    float channel_timer = 0.0f, tick_timer = 0.0f;
    std::vector<std::pair<std::string, float>> resting_cooldown;   // abilities taken off the bar, cooling down still
};

struct AbilityCastInput {
    int       pressed = -1;                // slot pressed this frame
    bool      held = false;                // the casting slot's key is down
    glm::vec3 origin{0.0f};                // where aimed abilities start (the eye)
    glm::vec3 aim{0.0f, 0.0f, -1.0f};      // unit
    glm::vec3 caster_pos{0.0f};            // where areas around the caster centre
    bool      moving = false;
};

struct AbilityFired {
    int         slot = -1;
    std::string id, vfx;
    glm::vec3   point{0.0f};               // where it went off (where it was launched, for a flying one)
    int         targets = 0;
    glm::vec3   origin{0.0f}, dir{0.0f, 0.0f, -1.0f};   // where it started and which way
    bool        launched = false;          // flying: it takes effect later (tick_ability_shots)
    bool        on_actor = false;          // aimed: it reached someone
    AbilityDef::Move move = AbilityDef::Move::None;   // the caster to move (the host does it)
    float       move_a = 0.0f, move_b = 0.0f;
};

/// Abilities in flight, on their caster.
struct AbilityShot {
    std::string id;
    glm::vec3   pos{0.0f}, vel{0.0f};
    float       life = 0.0f;               // seconds left
};
struct AbilityShots { std::vector<AbilityShot> shots; };

/// Where a flying ability took effect (for its effect), and one moved this frame (its trail).
struct AbilityImpact {
    std::string id;
    Entity      caster = null_entity;
    glm::vec3   point{0.0f}, normal{0.0f, 1.0f, 0.0f};
    int         targets = 0;
    bool        on_actor = false;
};
struct AbilityFlight { std::string id; glm::vec3 from{0.0f}, to{0.0f}; };

namespace ability_detail {
inline void publish(GameplayEventBus* bus, const char* name, Entity caster, Entity target, float value, const std::string& id) {
    if (!bus) return;
    GameplayEvent ev; ev.name = name; ev.instigator = caster; ev.target = target; ev.value = value; ev.param = id;
    bus->publish(ev);
}
inline bool affordable(World& w, Entity caster, const AbilityDef& d) {
    if (d.cost_attribute.empty() || d.cost <= 0.0f) return true;
    const AttributeSet* a = w.try_get<AttributeSet>(caster);
    return a && a->get(d.cost_attribute) >= d.cost;
}
inline void pay(World& w, Entity caster, const AbilityDef& d) {
    if (d.cost_attribute.empty() || d.cost <= 0.0f) return;
    if (AttributeSet* a = w.try_get<AttributeSet>(caster)) a->set(d.cost_attribute, a->get(d.cost_attribute) - d.cost);
}
inline bool tags_allow(World& w, Entity caster, const AbilityDef& d) {
    if (const auto* attrs = w.try_get<AttributeSet>(caster))
        if (const auto* health = attrs->find("Health"); health && health->current <= 0.0f) return false;
    const GameplayTags* tags = w.try_get<GameplayTags>(caster);
    if (tags && (tags->has("status.stunned") || tags->has("state.stunned"))) return false;
    for (const auto& t : d.require_tags) if (!tags || !tags->has(t)) return false;
    if (tags) for (const auto& t : d.block_tags) if (tags->has(t)) return false;
    return true;
}
/// The entity's team: its first `team.*` tag ("" when none).
inline std::string team_of(World& w, Entity e) {
    if (!w.valid(e)) return {};
    if (const GameplayTags* t = w.try_get<GameplayTags>(e))
        for (const auto& s : t->tags)
            if (s.rfind("team.", 0) == 0) return s;
    return {};
}
inline bool in_area(World& w, Entity e, const Transform& t, const glm::vec3& centre, float radius) {
    glm::vec3 point = t.position;
    if (const auto* ca = w.try_get<CombatActor>(e)) {
        const auto sphere = combat_hurtbox(*ca, t.position);
        point = sphere.center;
        radius += sphere.radius;
    }
    const auto delta = point - centre;
    return glm::dot(delta, delta) <= radius * radius;
}
/// On the caster's side: the caster, and those of its team.
inline bool ally_of(World& w, Entity caster, Entity e) {
    if (e == caster) return true;
    const std::string team = team_of(w, caster);
    return !team.empty() && team_of(w, e) == team;
}
/// In a cone from `from` along `dir`: within `reach` and half `angle` of it.
inline bool in_cone(World& w, Entity e, const Transform& t, const glm::vec3& from, const glm::vec3& dir, float reach, float angle) {
    if (!in_area(w, e, t, from, reach)) return false;
    glm::vec3 point = t.position;
    if (const auto* ca = w.try_get<CombatActor>(e)) point = combat_hurtbox(*ca, t.position).center;
    glm::vec3 to = point - from;
    to.y = 0.0f;
    glm::vec3 fwd(dir.x, 0.0f, dir.z);
    if (glm::length(to) < 0.8f) return true;   // close enough to touch
    if (glm::length(fwd) < 1e-4f) return true;
    return glm::dot(glm::normalize(to), glm::normalize(fwd)) >= std::cos(glm::radians(angle * 0.5f));
}
/// What an ability does to one it reached: damage, effects, statuses.
inline float affect(World& w, Entity caster, const AbilityDef& d, Entity e, float dmg, GameplayEventBus* bus) {
    float dealt = 0.0f;
    if (d.damage > 0.0f) {
        DamageInfo di{dmg, d.damage_type, "Health", caster};
        di.source = d.id;
        dealt = apply_damage(w, e, di);
    }
    const Entity owner = damage_owner(w, e);
    if (owner != null_entity) {
        for (const auto& fx : d.target_effects) apply_effect(w, owner, to_gameplay_effect(fx, d.id));
        for (const auto& st : d.inflict) apply_status(w, owner, st.id, st.stacks, caster, bus);
    }
    publish(bus, "ability.hit", caster, e, dealt, d.id);
    return dealt;
}
}  // namespace ability_detail

/// An ability's area left behind (`zone`): it goes off again every period.
struct AbilityZone {
    std::string id;
    glm::vec3   pos{0.0f};
    float       remaining = 0.0f, timer = 0.0f;
};
struct AbilityZones { std::vector<AbilityZone> zones; };

namespace ability_detail {
inline void leave_zone(World& w, Entity caster, const AbilityDef& d, const glm::vec3& at) {
    if (d.zone_time <= 0.0f) return;
    if (!w.has<AbilityZones>(caster)) w.add<AbilityZones>(caster, AbilityZones{});
    w.get<AbilityZones>(caster).zones.push_back({d.id, at, d.zone_time, 0.0f});
}

/// The nearest hurtbox along a ray that is not the caster's nor its team's.
inline Entity target_along(World& w, Entity caster, const glm::vec3& origin, const glm::vec3& dir, float max_t, float& out_t) {
    const std::string team = team_of(w, caster);
    Entity best = null_entity;
    float best_t = max_t;
    w.each<CombatActor, Transform>([&](Entity e, CombatActor& ca, Transform& t) {
        if (e == caster) return;
        if (!team.empty() && team_of(w, e) == team) return;
        const HitSphere hb = combat_hurtbox(ca, t.position);
        float tt = 0.0f;
        if (ray_sphere(origin, dir, hb.center, hb.radius, best_t, tt) && tt < best_t) { best_t = tt; best = e; }
    });
    out_t = best_t;
    return best;
}
/// Apply the ability to who it reached: `hit` (aimed), or everyone around
/// `centre` (areas). Returns how many.
inline int reach_targets(World& w, Entity caster, const AbilityDef& d, const glm::vec3& centre, Entity hit, GameplayEventBus* bus) {
    std::vector<Entity> hits;
    if (d.targeting == AbilityTargeting::Aimed) {
        if (hit != null_entity) hits.push_back(hit);
    } else if (d.targeting != AbilityTargeting::Self) {
        const std::string team = team_of(w, caster);
        w.each<Transform>([&](Entity e, Transform& t) {
            if (e == caster) return;
            if (!w.has<CombatActor>(e) && !w.has<AttributeSet>(e)) return;
            if (!team.empty() && team_of(w, e) == team) return;   // not its own side
            if (in_area(w, e, t, centre, d.radius)) hits.push_back(e);
        });
    }
    if (d.targeting != AbilityTargeting::Aimed) unique_damage_owners(w, hits);
    const float dmg = d.damage * damage_bonus(w.try_get<AttributeSet>(caster), "AbilityDamage");
    for (Entity e : hits) affect(w, caster, d, e, dmg, bus);
    if (d.targeting != AbilityTargeting::Aimed) leave_zone(w, caster, d, centre);
    return static_cast<int>(hits.size());
}

// The ability goes off once: reach its targets and apply everything -- or,
// with a speed, set off in flight.
/// `dir` turned by up to `deg` degrees, evenly over the cone (a spread).
inline glm::vec3 spread_aim(const glm::vec3& dir, float deg, uint64_t& state) {
    if (deg <= 0.0f) return dir;
    auto next = [&state] {
        uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return static_cast<float>((z ^ (z >> 31)) >> 40) / 16777216.0f;
    };
    const float r = std::sqrt(next()) * glm::radians(deg), a = next() * 6.2831853f;
    const glm::vec3 side = glm::normalize(glm::cross(dir, std::fabs(dir.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
    const glm::vec3 up = glm::cross(side, dir);
    return glm::normalize(dir + (side * std::cos(a) + up * std::sin(a)) * std::tan(r));
}
inline uint64_t& ability_spread_state() { static uint64_t s = 0x2545F4914F6CDD1DULL; return s; }

inline AbilityFired fire(World& w, Entity caster, const AbilityDef& d, int slot, const AbilityCastInput& in,
                         const WorldRayFn* world, GameplayEventBus* bus) {
    AbilityFired f;
    f.slot = slot; f.id = d.id; f.vfx = d.vfx;
    f.origin = in.origin;
    // Where it goes: the aim, off by up to its spread.
    glm::vec3 aim0 = glm::length(in.aim) > 1e-5f ? glm::normalize(in.aim) : glm::vec3(0.0f, 0.0f, -1.0f);
    if (d.spread > 0.0f && (d.targeting == AbilityTargeting::Aimed || d.targeting == AbilityTargeting::AreaAtAim))
        aim0 = spread_aim(aim0, d.spread, ability_spread_state());
    f.dir = aim0;
    if (d.speed > 0.0f && (d.targeting == AbilityTargeting::Aimed || d.targeting == AbilityTargeting::AreaAtAim)) {
        AbilityShot s;
        s.id = d.id;
        s.pos = in.origin;
        s.vel = f.dir * d.speed;
        s.life = d.arc > 0.0f ? 8.0f : std::max(0.05f, d.range / d.speed);
        if (!w.has<AbilityShots>(caster)) w.add<AbilityShots>(caster, AbilityShots{});
        w.get<AbilityShots>(caster).shots.push_back(s);
        for (const auto& e : d.self_effects) apply_effect(w, caster, to_gameplay_effect(e, d.name));
        for (const auto& st : d.self_statuses) apply_status(w, caster, st.id, st.stacks, caster, bus);
        f.point = in.origin;
        f.launched = true;
        publish(bus, "ability.fire", caster, null_entity, 0.0f, d.id);
        return f;
    }
    std::vector<Entity> hits;
    glm::vec3 centre = in.caster_pos;
    const glm::vec3 aim = aim0;
    float wall_t = d.range;
    glm::vec3 n(0.0f, 1.0f, 0.0f);
    const bool wall = (d.targeting == AbilityTargeting::Aimed || d.targeting == AbilityTargeting::AreaAtAim) &&
                      world && *world && (*world)(in.origin, aim, d.range, wall_t, n);
    switch (d.targeting) {
        case AbilityTargeting::Self:
            f.point = in.caster_pos;
            break;
        case AbilityTargeting::Cone: {
            // Those in front: from the caster, the way it aims (flat). A dash makes it a
            // lunge: it reaches as far as the dash carries the caster, and its radius on.
            const float reach = d.radius + (d.move == AbilityDef::Move::Dash ? std::max(0.0f, d.move_a) : 0.0f);
            f.point = in.caster_pos + glm::vec3(aim.x, 0.0f, aim.z) * (reach - d.radius * 0.5f);
            const std::string team = team_of(w, caster);
            w.each<Transform>([&](Entity e, Transform& t) {
                if (e == caster) return;
                if (!w.has<CombatActor>(e) && !w.has<AttributeSet>(e)) return;
                if (!team.empty() && team_of(w, e) == team) return;
                if (in_cone(w, e, t, in.caster_pos, aim, reach, d.angle)) hits.push_back(e);
            });
            break;
        }
        case AbilityTargeting::Aimed: {
            float t = 0.0f;
            const Entity hit = target_along(w, caster, in.origin, aim, wall ? wall_t : d.range, t);
            if (hit != null_entity) { hits.push_back(hit); f.point = in.origin + aim * t; f.on_actor = true; }
            else f.point = in.origin + aim * (wall ? wall_t : d.range);
            break;
        }
        case AbilityTargeting::AreaAtAim:
            centre = in.origin + aim * (wall ? wall_t : d.range);
            if (!wall && world && *world) {
                // Aimed at nothing (the sky): it comes down on the ground below.
                float down_t = 0.0f;
                glm::vec3 dn(0.0f, 1.0f, 0.0f);
                if ((*world)(centre, glm::vec3(0.0f, -1.0f, 0.0f), 200.0f, down_t, dn)) centre.y -= down_t;
            }
            [[fallthrough]];
        case AbilityTargeting::AreaAroundSelf: {
            f.point = centre;
            const std::string team = team_of(w, caster);
            w.each<Transform>([&](Entity e, Transform& t) {
                if (e == caster) return;
                if (!w.has<CombatActor>(e) && !w.has<AttributeSet>(e)) return;
                if (!team.empty() && team_of(w, e) == team) return;   // not its own side
                if (in_area(w, e, t, centre, d.radius)) hits.push_back(e);
            });
            break;
        }
    }
    for (const auto& e : d.self_effects) apply_effect(w, caster, to_gameplay_effect(e, d.name));
    for (const auto& st : d.self_statuses) apply_status(w, caster, st.id, st.stacks, caster, bus);
    f.move = d.move;
    f.move_a = d.move_a;
    f.move_b = d.move_b;
    if (d.targeting != AbilityTargeting::Aimed) unique_damage_owners(w, hits);
    const float dmg = d.damage * damage_bonus(w.try_get<AttributeSet>(caster), "AbilityDamage");
    for (Entity e : hits) affect(w, caster, d, e, dmg, bus);
    // An area (or the caster's own place) stays: a zone.
    if (d.targeting == AbilityTargeting::AreaAtAim || d.targeting == AbilityTargeting::AreaAroundSelf ||
        d.targeting == AbilityTargeting::Self)
        leave_zone(w, caster, d, f.point);
    f.targets = static_cast<int>(hits.size());
    publish(bus, "ability.fire", caster, null_entity, static_cast<float>(f.targets), d.id);
    return f;
}
}  // namespace ability_detail

/// Move the abilities in flight: each falls by its arc and takes effect where
/// it meets someone or the world (an area one also where its flight ends).
/// `impacts`: where each took effect; `flights`: the path each flew this frame.
inline void tick_ability_shots(World& w, float dt, const WorldRayFn* world, GameplayEventBus* bus,
                               std::vector<AbilityImpact>* impacts = nullptr, std::vector<AbilityFlight>* flights = nullptr) {
    std::vector<Entity> casters;
    w.each<AbilityShots>([&](Entity e, AbilityShots& s) { if (!s.shots.empty()) casters.push_back(e); });
    for (Entity caster : casters) {
        if (!w.valid(caster) || !w.has<AbilityShots>(caster)) continue;
        std::vector<AbilityShot> shots = std::move(w.get<AbilityShots>(caster).shots);
        std::vector<AbilityShot> keep;
        for (AbilityShot& s : shots) {
            const AbilityDef* d = find_ability_def(s.id);
            if (!d) continue;
            s.vel.y -= d->arc * dt;
            const glm::vec3 step = s.vel * dt;
            const float len = glm::length(step);
            const glm::vec3 dir = len > 1e-6f ? step / len : glm::vec3(0.0f, -1.0f, 0.0f);
            float tw = len;
            glm::vec3 n(0.0f, 1.0f, 0.0f);
            const bool wall = world && *world && len > 1e-6f && (*world)(s.pos, dir, len, tw, n);
            float ta = wall ? tw : len;
            const Entity hit = len > 1e-6f ? ability_detail::target_along(w, caster, s.pos, dir, ta, ta) : null_entity;
            s.life -= dt;
            const bool area = d->targeting == AbilityTargeting::AreaAtAim;
            if (hit != null_entity || wall || (s.life <= 0.0f && area)) {
                AbilityImpact im;
                im.id = d->id;
                im.caster = caster;
                im.on_actor = hit != null_entity;
                im.point = hit != null_entity ? s.pos + dir * ta : wall ? s.pos + dir * tw : s.pos + step;
                im.normal = hit != null_entity ? -dir : wall ? n : glm::vec3(0.0f, 1.0f, 0.0f);
                if (flights) flights->push_back({d->id, s.pos, im.point});
                im.targets = ability_detail::reach_targets(w, caster, *d, im.point, hit, bus);
                if (impacts) impacts->push_back(im);
                continue;
            }
            if (flights) flights->push_back({d->id, s.pos, s.pos + step});
            s.pos += step;
            if (s.life > 0.0f) keep.push_back(s);
        }
        if (w.valid(caster) && w.has<AbilityShots>(caster)) {
            auto& now = w.get<AbilityShots>(caster).shots;   // launched while this ran
            keep.insert(keep.end(), now.begin(), now.end());
            now = std::move(keep);
        }
    }
}

/// The zones abilities left (`zone`): each goes off again every period until its
/// time is up -- on the caster's enemies (damage, effects, statuses), or with
/// `zone_hits allies` on its side (effects and statuses: a rift that heals).
inline void tick_ability_zones(World& w, float dt, GameplayEventBus* bus) {
    std::vector<Entity> casters;
    w.each<AbilityZones>([&](Entity e, AbilityZones& z) { if (!z.zones.empty()) casters.push_back(e); });
    for (Entity caster : casters) {
        if (!w.valid(caster) || !w.has<AbilityZones>(caster)) continue;
        std::vector<AbilityZone> zones = std::move(w.get<AbilityZones>(caster).zones);
        std::vector<AbilityZone> keep;
        for (AbilityZone& z : zones) {
            const AbilityDef* d = find_ability_def(z.id);
            if (!d) continue;
            z.timer += dt;
            z.remaining -= dt;
            const float period = std::max(0.1f, d->zone_period);
            while (z.timer >= period) {
                z.timer -= period;
                std::vector<Entity> in;
                w.each<Transform>([&](Entity e, Transform& t) {
                    if (!w.has<CombatActor>(e) && !w.has<AttributeSet>(e)) return;
                    if (w.has<DamageReceiver>(e)) return;   // its owner counts once
                    const bool ally = ability_detail::ally_of(w, caster, e);
                    if (d->zone_hits == AbilityDef::ZoneHits::Enemies && ally) return;
                    if (d->zone_hits == AbilityDef::ZoneHits::Allies && !ally) return;
                    if (ability_detail::in_area(w, e, t, z.pos, d->radius)) in.push_back(e);
                });
                const float dmg = d->damage * damage_bonus(w.try_get<AttributeSet>(caster), "AbilityDamage");
                for (Entity e : in) {
                    if (!w.valid(e)) continue;
                    if (ability_detail::ally_of(w, caster, e)) {
                        for (const auto& fx : d->target_effects) apply_effect(w, e, to_gameplay_effect(fx, d->id));
                        for (const auto& st : d->inflict) apply_status(w, e, st.id, st.stacks, caster, bus);
                    } else {
                        ability_detail::affect(w, caster, *d, e, dmg, bus);
                    }
                }
            }
            if (z.remaining > 0.0f) keep.push_back(z);
        }
        if (w.valid(caster) && w.has<AbilityZones>(caster)) {
            auto& now = w.get<AbilityZones>(caster).zones;   // left while this ran
            keep.insert(keep.end(), now.begin(), now.end());
            now = std::move(keep);
        }
    }
}

/// Can the slot be started now (cooldown, tags, cost, nothing else casting)?
inline bool ability_slot_ready(World& w, Entity caster, const AbilityBar& bar, int slot) {
    if (slot < 0 || slot >= static_cast<int>(bar.slots.size()) || bar.casting >= 0) return false;
    const AbilityDef* d = find_ability_def(bar.slots[slot]);
    if (!d) return false;
    if (slot < static_cast<int>(bar.cooldown.size()) && bar.cooldown[slot] > 0.0f) return false;
    return ability_detail::tags_allow(w, caster, *d) && ability_detail::affordable(w, caster, *d);
}

/// One frame of the bar. Returns how many times an ability went off.
inline int step_ability_bar(World& w, Entity caster, AbilityBar& bar, const AbilityCastInput& in,
                            const WorldRayFn* world, GameplayEventBus* bus, float dt,
                            std::vector<AbilityFired>* fired = nullptr) {
    using namespace ability_detail;
    bar.cooldown.resize(bar.slots.size(), 0.0f);
    for (float& c : bar.cooldown) c = std::max(0.0f, c - dt);
    int count = 0;
    // Abilities resting off the bar cool down too.
    for (auto& rc : bar.resting_cooldown) rc.second = std::max(0.0f, rc.second - dt);
    bar.resting_cooldown.erase(std::remove_if(bar.resting_cooldown.begin(), bar.resting_cooldown.end(),
                                              [](const auto& rc) { return rc.second <= 0.0f; }),
                               bar.resting_cooldown.end());
    // CooldownReduction (a bonus stat): % shorter, at most 75.
    const float cdr = std::clamp(bonus_stat(w.try_get<AttributeSet>(caster), "CooldownReduction"), 0.0f, 75.0f);
    auto start_cooldown = [&](int slot, const AbilityDef& d) {
        if (slot < static_cast<int>(bar.cooldown.size())) bar.cooldown[slot] = d.cooldown * (1.0f - cdr / 100.0f);
    };
    auto end_cast = [&] { bar.casting = -1; bar.cast_timer = 0.0f; bar.channelling = false; bar.channel_timer = 0.0f; bar.tick_timer = 0.0f; };
    auto go = [&](int slot, const AbilityDef& d) {
        AbilityFired f = fire(w, caster, d, slot, in, world, bus);
        ++count;
        if (fired) fired->push_back(f);
    };

    // Start.
    if (in.pressed >= 0 && ability_slot_ready(w, caster, bar, in.pressed)) {
        const AbilityDef& d = *find_ability_def(bar.slots[in.pressed]);
        bar.casting = in.pressed;
        bar.cast_timer = 0.0f;
        publish(bus, "ability.cast", caster, null_entity, d.cast_time, d.id);
    }
    if (bar.casting < 0) return count;
    const AbilityDef* dp = bar.casting < static_cast<int>(bar.slots.size()) ? find_ability_def(bar.slots[bar.casting]) : nullptr;
    if (!dp) { end_cast(); return count; }
    const AbilityDef& d = *dp;
    const int slot = bar.casting;

    // Stuns and changing requirements interrupt channels as well as wind-ups.
    if (!tags_allow(w, caster, d)) {
        if (bar.channelling) start_cooldown(slot, d);
        publish(bus, "ability.interrupted", caster, null_entity, bar.cast_timer, d.id);
        end_cast();
        return count;
    }

    if (!bar.channelling) {
        // Wind-up. Moving breaks a cast that needs standing still.
        if (!d.cast_while_moving && in.moving) {
            publish(bus, "ability.interrupted", caster, null_entity, bar.cast_timer, d.id);
            end_cast();
            return count;
        }
        bar.cast_timer += dt;
        if (bar.cast_timer + 1e-6f < d.cast_time) return count;
        if (!tags_allow(w, caster, d) || !affordable(w, caster, d)) {
            publish(bus, "ability.interrupted", caster, null_entity, bar.cast_timer, d.id);
            end_cast();
            return count;
        }
        pay(w, caster, d);
        if (d.channel_time <= 0.0f) {
            go(slot, d);
            start_cooldown(slot, d);
            publish(bus, "ability.end", caster, null_entity, 0.0f, d.id);
            end_cast();
            return count;
        }
        bar.channelling = true;
        bar.channel_timer = 0.0f;
        bar.tick_timer = 0.0f;
        go(slot, d);   // the first pulse as the channel begins
        return count;
    }
    // Releasing must stop before another pulse can be emitted this frame.
    if (!in.held) {
        start_cooldown(slot, d);
        publish(bus, "ability.end", caster, null_entity, bar.channel_timer, d.id);
        end_cast();
        return count;
    }
    // Channel: a pulse every period while the key is held.
    bar.channel_timer += dt;
    bar.tick_timer += dt;
    const float period = std::max(0.02f, d.channel_period);
    while (bar.tick_timer >= period && bar.channel_timer <= d.channel_time + 1e-6f) {
        bar.tick_timer -= period;
        go(slot, d);
    }
    if (!in.held || bar.channel_timer >= d.channel_time) {
        start_cooldown(slot, d);
        publish(bus, "ability.end", caster, null_entity, bar.channel_timer, d.id);
        end_cast();
    }
    return count;
}

// ---- data: `ability <id>` blocks ------------------------------------------------
//   ability fireball
//     name Fireball
//     cost Mana 20
//     cooldown 4
//     cast 0.6              # wind-up in seconds
//     still                 # moving breaks the cast
//     channel 3 0.5         # keep going while held: at most 3 s, a pulse every 0.5 s
//     target aimed          # self | aimed | area | area_at_aim
//     range 30
//     radius 3
//     damage 40 fire
//     self Health 10        # to the caster: attribute amount [seconds]
//     effect Speed -2 3     # to each one reached: attribute amount [seconds]
//     require state.alive
//     block state.stunned
//     vfx fireball_burst    # its effects: fireball_burst_cast, _trail, _hit ... (vfx/effects.h)
//     speed 40              # aimed / area_at_aim: it flies (m/s) and takes effect where it lands
//     arc 9.8               # a flying one falls (thrown things)
//     inflict burning 2     # a status on each one reached (gameplay_status.h), stacks
//     self_status hasted    # one on the caster
//     target cone           # those in front within `radius`, `angle` wide:
//     angle 100
//     zone 6 1              # its area stays 6 s, going off every 1 s
//     zone_hits allies      # ...on the caster's side instead (enemies | allies | all)
//     labels fire thrown    # free words the game's rules can look for (perks)
//   end
inline std::vector<AbilityDef> parse_abilities_text(const std::string& text) {
    std::vector<AbilityDef> out;
    AbilityDef cur; bool have = false;
    auto flush = [&] { if (have && !cur.id.empty()) out.push_back(cur); cur = AbilityDef{}; have = false; };
    std::istringstream in(text);
    std::string line;
    auto rest_of = [](std::istringstream& ls) {
        std::string r; std::getline(ls, r);
        size_t a = 0, b = r.size();
        while (a < b && std::isspace(static_cast<unsigned char>(r[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(r[b - 1]))) --b;
        return r.substr(a, b - a);
    };
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#') continue;
        if (key == "ability")       { flush(); ls >> cur.id; cur.name = cur.id; have = true; }
        else if (key == "item" || key == "weapon" || key == "skill" || key == "status" || key == "perk") { flush(); }
        else if (!have)             { continue; }
        else if (key == "end")      { flush(); }
        else if (key == "name")     { cur.name = rest_of(ls); }
        else if (key == "cost")     { ls >> cur.cost_attribute >> cur.cost; }
        else if (key == "cooldown") { ls >> cur.cooldown; }
        else if (key == "cast")     { ls >> cur.cast_time; }
        else if (key == "still")    { cur.cast_while_moving = false; }
        else if (key == "channel")  { ls >> cur.channel_time; float p; if (ls >> p) cur.channel_period = p; }
        else if (key == "target")   { std::string v; ls >> v; cur.targeting = parse_ability_targeting(v); }
        else if (key == "range")    { ls >> cur.range; }
        else if (key == "radius")   { ls >> cur.radius; }
        else if (key == "damage")   { ls >> cur.damage; std::string ty; if (ls >> ty) cur.damage_type = ty; }
        else if (key == "self" || key == "effect") {
            AbilityEffect e; ls >> e.attribute >> e.amount; float dur; if (ls >> dur) e.duration = dur;
            if (!e.attribute.empty()) (key == "self" ? cur.self_effects : cur.target_effects).push_back(e);
        }
        else if (key == "require")  { std::string t; ls >> t; if (!t.empty()) cur.require_tags.push_back(t); }
        else if (key == "block")    { std::string t; ls >> t; if (!t.empty()) cur.block_tags.push_back(t); }
        else if (key == "vfx")      { ls >> cur.vfx; }
        else if (key == "speed")    { ls >> cur.speed; }
        else if (key == "arc")      { ls >> cur.arc; }
        else if (key == "spread")   { ls >> cur.spread; }
        else if (key == "knockdown") { ls >> cur.knockdown; }
        else if (key == "about")    { cur.about = rest_of(ls); }
        else if (key == "inflict" || key == "self_status") {
            AbilityDef::StatusPut p; ls >> p.id; if (!(ls >> p.stacks)) p.stacks = 1;
            if (!p.id.empty()) (key == "inflict" ? cur.inflict : cur.self_statuses).push_back(p);
        }
        else if (key == "angle")    { ls >> cur.angle; }
        else if (key == "zone")     { ls >> cur.zone_time; float p; if (ls >> p) cur.zone_period = p; }
        else if (key == "zone_hits") {
            std::string v; ls >> v;
            cur.zone_hits = v == "allies" ? AbilityDef::ZoneHits::Allies : v == "all" ? AbilityDef::ZoneHits::All : AbilityDef::ZoneHits::Enemies;
        }
        else if (key == "labels" || key == "label") { for (std::string w; ls >> w;) { if (w[0] == '#') break; cur.labels.push_back(w); } }
        else if (key == "move")     {
            std::string k;
            ls >> k >> cur.move_a;
            if (!(ls >> cur.move_b)) cur.move_b = 0.0f;
            cur.move = k == "blink" ? AbilityDef::Move::Blink : k == "dash" ? AbilityDef::Move::Dash
                     : k == "leap" ? AbilityDef::Move::Leap : AbilityDef::Move::None;
        }
    }
    flush();
    return out;
}

// ---- skills that grant abilities -------------------------------------------------
/// Put an ability on `e`'s bar: in the first empty slot, else a new one (up to
/// 8). False when it is already there or the bar is full.
/// The bar's slot count: max_slots, else 8.
inline int ability_slot_count(const AbilityBar& bar) { return bar.max_slots > 0 ? std::min(bar.max_slots, 8) : 8; }
/// Every ability `bar` has, on it or not (known, and the slots of a bar from
/// before there was a known list).
inline std::vector<std::string> known_abilities(const AbilityBar& bar) {
    std::vector<std::string> out = bar.known;
    for (const auto& s : bar.slots)
        if (!s.empty() && std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
    return out;
}
/// Give an ability: known from now on, and on the bar when it has room.
inline bool give_ability(World& w, Entity e, const std::string& id) {
    if (!w.has<AbilityBar>(e)) w.add<AbilityBar>(e, AbilityBar{});
    AbilityBar& bar = w.get<AbilityBar>(e);
    if (std::find(bar.known.begin(), bar.known.end(), id) == bar.known.end()) bar.known.push_back(id);
    for (const auto& s : bar.slots) if (s == id) return false;
    for (auto& s : bar.slots) if (s.empty()) { s = id; return true; }
    if (static_cast<int>(bar.slots.size()) >= ability_slot_count(bar)) return true;   // known, to equip
    bar.slots.push_back(id);
    return true;
}
/// Put a known ability in a slot (0-based). Already in another slot: the two
/// change places. What it pushes out rests, its cooldown running on. False
/// when it is not known or the slot is past the bar's slots.
inline bool equip_ability(World& w, Entity e, const std::string& id, int slot) {
    if (!w.has<AbilityBar>(e)) return false;
    AbilityBar& bar = w.get<AbilityBar>(e);
    const std::vector<std::string> known = known_abilities(bar);
    if (slot < 0 || slot >= ability_slot_count(bar) || std::find(known.begin(), known.end(), id) == known.end()) return false;
    if (bar.casting >= 0) return false;   // not while casting
    if (static_cast<int>(bar.slots.size()) <= slot) bar.slots.resize(static_cast<size_t>(slot) + 1);
    bar.cooldown.resize(bar.slots.size(), 0.0f);
    if (std::find(bar.known.begin(), bar.known.end(), id) == bar.known.end()) bar.known.push_back(id);
    for (size_t i = 0; i < bar.slots.size(); ++i)
        if (bar.slots[i] == id && static_cast<int>(i) != slot) {
            std::swap(bar.slots[i], bar.slots[static_cast<size_t>(slot)]);
            std::swap(bar.cooldown[i], bar.cooldown[static_cast<size_t>(slot)]);
            return true;
        }
    if (bar.slots[static_cast<size_t>(slot)] == id) return true;
    // The one taken off rests with its cooldown; the one put on brings its own.
    const std::string out = bar.slots[static_cast<size_t>(slot)];
    if (!out.empty() && bar.cooldown[static_cast<size_t>(slot)] > 0.0f)
        bar.resting_cooldown.push_back({out, bar.cooldown[static_cast<size_t>(slot)]});
    float cd = 0.0f;
    for (auto it = bar.resting_cooldown.begin(); it != bar.resting_cooldown.end(); ++it)
        if (it->first == id) { cd = it->second; bar.resting_cooldown.erase(it); break; }
    bar.slots[static_cast<size_t>(slot)] = id;
    bar.cooldown[static_cast<size_t>(slot)] = cd;
    return true;
}
/// Empty a slot (0-based): what was in it stays known, resting with its cooldown.
/// False when the slot is past the bar's slots or a cast is under way.
inline bool clear_ability_slot(World& w, Entity e, int slot) {
    if (!w.has<AbilityBar>(e)) return false;
    AbilityBar& bar = w.get<AbilityBar>(e);
    if (slot < 0 || slot >= ability_slot_count(bar) || bar.casting >= 0) return false;
    if (static_cast<int>(bar.slots.size()) <= slot) return true;   // empty already
    bar.cooldown.resize(bar.slots.size(), 0.0f);
    const size_t i = static_cast<size_t>(slot);
    const std::string out = bar.slots[i];
    if (out.empty()) return true;
    if (std::find(bar.known.begin(), bar.known.end(), out) == bar.known.end()) bar.known.push_back(out);
    if (bar.cooldown[i] > 0.0f) bar.resting_cooldown.push_back({out, bar.cooldown[i]});
    bar.slots[i].clear();
    bar.cooldown[i] = 0.0f;
    return true;
}
/// Learn a skill (unlock_skill: points, prerequisites, grants, tags) and put
/// the abilities it gives on the bar.
inline bool learn_skill(World& w, Entity e, const std::string& node_id, GameplayEventBus* bus = nullptr) {
    if (!unlock_skill(w, e, node_id, bus)) return false;
    if (const SkillTree* t = w.try_get<SkillTree>(e))
        if (const SkillNode* n = find_node(*t, node_id))
            for (const auto& a : n->abilities) give_ability(w, e, a);
    return true;
}
/// Give `e` the tree with this id from the data files (and what learning
/// needs: unlocked skills, a progression). False when no such tree exists.
inline bool set_skill_tree(World& w, Entity e, const std::string& tree) {
    SkillTree t = skill_tree_named(tree);
    if (t.nodes.empty()) return false;
    if (w.has<SkillTree>(e)) w.get<SkillTree>(e) = t; else w.add<SkillTree>(e, t);
    if (!w.has<UnlockedSkills>(e)) w.add<UnlockedSkills>(e, UnlockedSkills{});
    if (!w.has<Progression>(e)) w.add<Progression>(e, Progression{});
    return true;
}

// ---- the Ability Bar component ----------------------------------------------------
inline void serialize_ability_bar(const void* obj, std::vector<uint8_t>& out) {
    const auto& b = *static_cast<const AbilityBar*>(obj);
    detail::put_i32(out, 2);
    detail::put_str_vec(out, b.slots);
    detail::put_i32(out, b.max_slots);
    detail::put_str_vec(out, b.known);
}
inline void deserialize_ability_bar(void* obj, const uint8_t* data, size_t n) {
    auto& b = *static_cast<AbilityBar*>(obj);
    b = AbilityBar{};
    const uint8_t* p = data; const uint8_t* e = data + n;
    int32_t ver = 0;
    if (!detail::get_i32(p, e, ver) || ver < 1) return;
    detail::get_str_vec(p, e, b.slots);
    if (ver < 2) return;
    int32_t ms = 0;
    if (detail::get_i32(p, e, ms)) b.max_slots = ms;
    detail::get_str_vec(p, e, b.known);
}
inline void register_ability_cast_components() {
    register_authorable(make_authorable_custom<AbilityBar>("Ability Bar", &serialize_ability_bar, &deserialize_ability_bar));
}

}  // namespace schizo::ecs
