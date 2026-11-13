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
// Every stage publishes a gameplay event (ability.cast, ability.fire,
// ability.hit, ability.interrupted, ability.end) carrying the ability id; the
// AbilityFired list says where each one went off -- the hooks for effects,
// sounds and scripts. Checked by tools/gameplay_check.

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

enum class AbilityTargeting : uint8_t { Self, Aimed, AreaAroundSelf, AreaAtAim };

inline const char* ability_targeting_name(AbilityTargeting t) {
    switch (t) {
        case AbilityTargeting::Self:           return "self";
        case AbilityTargeting::Aimed:          return "aimed";
        case AbilityTargeting::AreaAroundSelf: return "area";
        case AbilityTargeting::AreaAtAim:      return "area_at_aim";
    }
    return "self";
}
inline AbilityTargeting parse_ability_targeting(const std::string& s) {
    if (s == "aimed")       return AbilityTargeting::Aimed;
    if (s == "area")        return AbilityTargeting::AreaAroundSelf;
    if (s == "area_at_aim") return AbilityTargeting::AreaAtAim;
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
    fx.name = name;
    fx.duration_type = EffectDurationType::Duration;
    fx.duration = e.duration;
    fx.revert_on_end = true;
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
    // Runtime (not saved)
    std::vector<float> cooldown;           // per slot, seconds left
    int   casting = -1;                    // the slot being cast or channelled
    float cast_timer = 0.0f;               // wind-up done so far
    bool  channelling = false;
    float channel_timer = 0.0f, tick_timer = 0.0f;
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
    glm::vec3   point{0.0f};               // where it went off
    int         targets = 0;
};

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
    const GameplayTags* tags = w.try_get<GameplayTags>(caster);
    for (const auto& t : d.require_tags) if (!tags || !tags->has(t)) return false;
    if (tags) for (const auto& t : d.block_tags) if (tags->has(t)) return false;
    return true;
}
// The ability goes off once: reach its targets and apply everything.
inline AbilityFired fire(World& w, Entity caster, const AbilityDef& d, int slot, const AbilityCastInput& in,
                         const WorldRayFn* world, GameplayEventBus* bus) {
    AbilityFired f;
    f.slot = slot; f.id = d.id; f.vfx = d.vfx;
    std::vector<Entity> hits;
    glm::vec3 centre = in.caster_pos;
    const glm::vec3 aim = glm::length(in.aim) > 1e-5f ? glm::normalize(in.aim) : glm::vec3(0.0f, 0.0f, -1.0f);
    float wall_t = d.range;
    glm::vec3 n(0.0f, 1.0f, 0.0f);
    const bool wall = (d.targeting == AbilityTargeting::Aimed || d.targeting == AbilityTargeting::AreaAtAim) &&
                      world && *world && (*world)(in.origin, aim, d.range, wall_t, n);
    switch (d.targeting) {
        case AbilityTargeting::Self:
            f.point = in.caster_pos;
            break;
        case AbilityTargeting::Aimed: {
            float t = 0.0f;
            const Entity hit = hitscan_target(w, caster, in.origin, aim, wall ? wall_t : d.range, t);
            if (hit != null_entity) { hits.push_back(hit); f.point = in.origin + aim * t; }
            else f.point = in.origin + aim * (wall ? wall_t : d.range);
            break;
        }
        case AbilityTargeting::AreaAtAim:
            centre = in.origin + aim * (wall ? wall_t : d.range);
            [[fallthrough]];
        case AbilityTargeting::AreaAroundSelf: {
            f.point = centre;
            const float r2 = d.radius * d.radius;
            w.each<Transform>([&](Entity e, Transform& t) {
                if (e == caster) return;
                if (!w.has<CombatActor>(e) && !w.has<AttributeSet>(e)) return;
                const glm::vec3 dd = t.position - centre;
                if (glm::dot(dd, dd) <= r2) hits.push_back(e);
            });
            break;
        }
    }
    for (const auto& e : d.self_effects) apply_effect(w, caster, to_gameplay_effect(e, d.name));
    for (Entity e : hits) {
        float dealt = 0.0f;
        if (d.damage > 0.0f) dealt = apply_damage(w, e, DamageInfo{d.damage, d.damage_type});
        for (const auto& fx : d.target_effects) apply_effect(w, e, to_gameplay_effect(fx, d.name));
        publish(bus, "ability.hit", caster, e, dealt, d.id);
    }
    f.targets = static_cast<int>(hits.size());
    publish(bus, "ability.fire", caster, null_entity, static_cast<float>(f.targets), d.id);
    return f;
}
}  // namespace ability_detail

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
    auto start_cooldown = [&](int slot, const AbilityDef& d) {
        if (slot < static_cast<int>(bar.cooldown.size())) bar.cooldown[slot] = d.cooldown;
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
//     vfx fireball_burst
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
        else if (key == "item" || key == "weapon") { flush(); }
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
    }
    flush();
    return out;
}

// ---- skills that grant abilities -------------------------------------------------
/// Put an ability on `e`'s bar: in the first empty slot, else a new one (up to
/// 8). False when it is already there or the bar is full.
inline bool give_ability(World& w, Entity e, const std::string& id) {
    if (!w.has<AbilityBar>(e)) w.add<AbilityBar>(e, AbilityBar{});
    AbilityBar& bar = w.get<AbilityBar>(e);
    for (const auto& s : bar.slots) if (s == id) return false;
    for (auto& s : bar.slots) if (s.empty()) { s = id; return true; }
    if (bar.slots.size() >= 8) return false;
    bar.slots.push_back(id);
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
    detail::put_i32(out, 1);
    detail::put_str_vec(out, b.slots);
}
inline void deserialize_ability_bar(void* obj, const uint8_t* data, size_t n) {
    auto& b = *static_cast<AbilityBar*>(obj);
    b = AbilityBar{};
    const uint8_t* p = data; const uint8_t* e = data + n;
    int32_t ver = 0;
    if (!detail::get_i32(p, e, ver) || ver < 1) return;
    detail::get_str_vec(p, e, b.slots);
}
inline void register_ability_cast_components() {
    register_authorable(make_authorable_custom<AbilityBar>("Ability Bar", &serialize_ability_bar, &deserialize_ability_bar));
}

}  // namespace schizo::ecs
