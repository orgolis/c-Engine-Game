#pragma once
// ============================================================================
// gameplay_melee.h -- melee combat driven by animation (0.9.1).
// ============================================================================
//
// A weapon's MOVESET (data: `moveset` blocks in .items files) says what its
// wielder can do and when: attacks that chain into one another, a heavy that
// can be held to charge, running / rolling / jumping attacks, a dodge roll and
// a backstep with invulnerable frames, a guard, a parry and the riposte after
// it, and the reactions (staggers, a blocked hit, a broken guard, being
// parried). Every timing is in seconds of the move's clip, so the hits land
// where the animation swings.
//
// A MeleeFighter steps through it each frame from its inputs (a player's
// buttons, an NPC's choices): presses are buffered and taken at the move's
// cancel point, stamina is spent and comes back after a pause, poise breaks
// into a stagger, a guard blocks from the front at a stamina cost (and breaks
// when it runs out), a parry window turns the attacker's swing into an opening
// for a riposte. Its blade is swept through the world: the host sets the
// blade's segment each frame (from the hand bone, as the animation holds it);
// without one, a swing reaches in an arc in front.
//
// What it does not do: play the clips or move the character. It says which
// clip to play (`clip`, a fresh start, its rate, root motion) and whom to face;
// the host (Play) plays it full-body and moves the character by the clip's
// root motion. Pure ECS: checked by tools/gameplay_check.
//
//   moveset longsword
//     name Longsword
//     stance Sword_Idle                # its clip while drawn, standing
//     stamina_regen 40                 # a second, after...
//     stamina_delay 0.5                # ...this long without spending
//     attack light1                    # an attack: its id
//       input light                    # light | heavy | run_light | run_heavy | roll_light | jump_light | riposte
//       clip Sword_Light_1
//       rate 1.15                      # its clip's speed
//       hit 0.22 0.34                  # the blade hurts (clip seconds); several lines, several hits
//       cancel 0.42                    # from here a buffered press starts the next move
//       finish 0.70                    # from here it is over (back to the stance)
//       damage 1.0                     # times the weapon's damage
//       stamina 12                     # spent as it starts
//       poise 14                       # poise damage it does
//       hyperarmor 0.10 0.34           # it is not staggered while this lasts
//       track 360                      # turns toward its target, degrees a second, until its first hit
//       reach 2.4                      # without a blade: everyone this near...
//       arc 120                        # ...in this many degrees in front, at its first hit
//     attack light2
//       input light
//       after light1                   # a chain: pressed again in light1's cancel window
//       ...
//     attack heavy1
//       input heavy
//       charge Sword_Heavy_Hold 1.0 1.6 heavy1_release 0.3   # held: this clip up to 1 s, damage up
//                                      # to x1.6; let go, the attack named (it starts from the held
//                                      # pose -- a clip of its own), else this one. The last number:
//                                      # still held this far into the move (its wind-up), it becomes
//                                      # the charge -- a tap swings the move as it is (0: held at once)
//       ...
//     dodge roll                       # roll (moving) | backstep (standing)
//       clip Roll
//       iframes 0.05 0.38
//       cancel 0.45                    # a roll attack from here
//       finish 0.62
//       stamina 16
//     guard                            # held
//       clip Sword_Guard
//       absorb 0.8                     # the share of a blocked hit's damage it stops
//       stability 45                   # how little of a hit's poise damage it costs in stamina (0..100)
//       angle 170                      # in front, degrees
//     parry
//       clip Parry
//       window 0.04 0.22               # a hit landing now is parried
//       finish 0.60
//       stamina 10
//     react stagger    Stagger_Light   # reactions: stagger | stagger_heavy | guard_hit | guard_break |
//     react guard_hit  Guard_Hit       #   parried | riposted -- the clip, and how long it lasts (its
//     react parried    Parried 1.4     #   clip's length when left out)
//   end

#include "world.h"
#include "components.h"
#include "authorable_components.h"
#include "component_serialize.h"
#include "gameplay_attributes.h"
#include "gameplay_combat.h"          // CombatActor: hurtbox and poise
#include "gameplay_damage.h"
#include "gameplay_events.h"
#include "gameplay_tags.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace schizo::ecs {

// ---- data ------------------------------------------------------------------------------

struct MeleeWindow {
    float from = -1.0f, to = -1.0f;
    bool on(float t) const { return from >= 0.0f && t >= from && t < to; }
};

struct MeleeMove {
    enum class Kind : uint8_t { Attack, Dodge, Guard, Parry, React };
    Kind        kind = Kind::Attack;
    std::string id;                    // attacks and dodges; a reaction: its kind (stagger, parried ...)
    std::string input;                 // attacks: light | heavy | run_light | ...; dodges: roll | backstep
    std::string after;                 // a chain: the attack it follows
    std::string clip;
    float       rate = 1.0f;
    std::vector<MeleeWindow> hits;
    float       cancel = -1.0f;        // <0: at its end
    float       end = -1.0f;           // <0: its clip's length (the host tells it: clip_length)
    float       length = -1.0f;        // a reaction's time (react <kind> <clip> [seconds])
    float       damage = 1.0f;
    float       stamina = 0.0f;
    float       poise = 10.0f;
    MeleeWindow hyperarmor, iframes, window;
    float       track = 0.0f;
    float       reach = 2.2f, arc = 120.0f;
    std::string charge_clip;           // a heavy held
    float       charge_max = 0.0f, charge_damage = 1.0f;
    std::string charge_release;        // the attack it lets go into ("" itself)
    float       charge_from = 0.0f;    // held this far into the move: the charge (0: held as it is pressed)
    float       absorb = 0.75f, stability = 40.0f, angle = 170.0f;   // guard
};

struct MovesetDef {
    std::string id, name, stance;
    float stamina_regen = 40.0f, stamina_delay = 0.5f;
    float poise_regen = 30.0f, poise_delay = 2.0f;
    float buffer = 0.35f;              // how long a press waits for its move's cancel point
    float riposte_reach = 2.4f;
    std::vector<MeleeMove> moves;

    int find(MeleeMove::Kind k, const std::string& input, const std::string& after = "") const {
        for (size_t i = 0; i < moves.size(); ++i) {
            const auto& m = moves[i];
            if (m.kind != k) continue;
            if (k == MeleeMove::Kind::Attack || k == MeleeMove::Kind::Dodge) {
                if (m.input == input && m.after == after) return static_cast<int>(i);
            } else if (k == MeleeMove::Kind::React) {
                if (m.id == input) return static_cast<int>(i);
            } else {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

inline std::vector<MovesetDef>& moveset_registry() { static std::vector<MovesetDef> r; return r; }
inline void register_moveset(const MovesetDef& d) {
    for (auto& e : moveset_registry()) if (e.id == d.id) { e = d; return; }
    moveset_registry().push_back(d);
}
inline const MovesetDef* find_moveset(const std::string& id) {
    for (const auto& e : moveset_registry()) if (e.id == id) return &e;
    return nullptr;
}

/// The `moveset` blocks in a text (other blocks are left alone).
inline std::vector<MovesetDef> parse_movesets_text(const std::string& text) {
    std::vector<MovesetDef> out;
    MovesetDef cur;
    bool have = false, other = false;   // other: inside an item / weapon / ability ... block
    MeleeMove* mv = nullptr;
    auto flush = [&] {
        if (have && !cur.id.empty()) out.push_back(cur);
        cur = MovesetDef{};
        have = false;
        mv = nullptr;
    };
    auto window = [](std::istringstream& ls, MeleeWindow& w) { ls >> w.from >> w.to; if (w.to < w.from) std::swap(w.from, w.to); };
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#') continue;
        // A weapon's own `moveset <id>` line names one; it does not start one.
        if (key == "moveset" && !other) { flush(); ls >> cur.id; cur.name = cur.id; have = true; continue; }
        if (key == "item" || key == "weapon" || key == "ability" || key == "skill" || key == "status" || key == "perk" ||
            key == "effect") {
            if (have) flush();
            other = true;
            continue;
        }
        if (!have) { if (key == "end") other = false; continue; }
        if (key == "end") { flush(); continue; }
        // A move's header.
        if (key == "attack" || key == "dodge" || key == "guard" || key == "parry") {
            MeleeMove m;
            m.kind = key == "attack" ? MeleeMove::Kind::Attack : key == "dodge" ? MeleeMove::Kind::Dodge
                   : key == "guard" ? MeleeMove::Kind::Guard : MeleeMove::Kind::Parry;
            ls >> m.id;
            if (m.kind == MeleeMove::Kind::Dodge) m.input = m.id;   // dodge roll | dodge backstep
            if (m.kind == MeleeMove::Kind::Guard || m.kind == MeleeMove::Kind::Parry) m.id = key;
            if (m.kind == MeleeMove::Kind::Attack) m.input = "light";
            cur.moves.push_back(m);
            mv = &cur.moves.back();
            continue;
        }
        if (key == "react") {
            MeleeMove m;
            m.kind = MeleeMove::Kind::React;
            ls >> m.id >> m.clip;
            float s = 0.0f;
            if (ls >> s) m.length = s;
            cur.moves.push_back(m);
            mv = nullptr;
            continue;
        }
        // The moveset's own lines.
        if (!mv) {
            if (key == "name") { std::getline(ls >> std::ws, cur.name); }
            else if (key == "stance") ls >> cur.stance;
            else if (key == "stamina_regen") ls >> cur.stamina_regen;
            else if (key == "stamina_delay") ls >> cur.stamina_delay;
            else if (key == "poise_regen") ls >> cur.poise_regen;
            else if (key == "poise_delay") ls >> cur.poise_delay;
            else if (key == "buffer") ls >> cur.buffer;
            else if (key == "riposte_reach") ls >> cur.riposte_reach;
            continue;
        }
        // A move's lines.
        if (key == "input") ls >> mv->input;
        else if (key == "after") ls >> mv->after;
        else if (key == "clip") ls >> mv->clip;
        else if (key == "rate") ls >> mv->rate;
        else if (key == "hit") { MeleeWindow w; window(ls, w); mv->hits.push_back(w); }
        else if (key == "cancel") ls >> mv->cancel;
        else if (key == "finish") ls >> mv->end;   // (not `end`: that closes a block)
        else if (key == "damage") ls >> mv->damage;
        else if (key == "stamina") ls >> mv->stamina;
        else if (key == "poise") ls >> mv->poise;
        else if (key == "hyperarmor") window(ls, mv->hyperarmor);
        else if (key == "iframes") window(ls, mv->iframes);
        else if (key == "window") window(ls, mv->window);
        else if (key == "track") ls >> mv->track;
        else if (key == "reach") ls >> mv->reach;
        else if (key == "arc") ls >> mv->arc;
        else if (key == "charge") {
            ls >> mv->charge_clip >> mv->charge_max >> mv->charge_damage;
            // then the attack it lets go into and/or when holding becomes the charge, either order
            for (std::string t; ls >> t;) {
                if (t[0] == '#') break;
                char* end = nullptr;
                const float v = std::strtof(t.c_str(), &end);
                if (end && *end == '\0') mv->charge_from = std::max(0.0f, v);
                else mv->charge_release = t;
            }
        }
        else if (key == "absorb") ls >> mv->absorb;
        else if (key == "stability") ls >> mv->stability;
        else if (key == "angle") ls >> mv->angle;
    }
    flush();
    return out;
}

// ---- the fighter -----------------------------------------------------------------------

/// What a fighter is asked to do this frame (presses are the frame they happen).
struct MeleeInput {
    bool light = false, heavy = false, heavy_held = false, dodge = false, parry = false;
    bool guard = false;                // held
    bool running = false, airborne = false, moving = false;
    glm::vec3 move_dir{0.0f};          // where it means to go (a roll's way), world, flat
    Entity target = null_entity;       // locked on
};

struct MeleeFighter {
    // ---- authored ----
    std::string moveset;               // its moves (the host sets a held weapon's)
    float       damage = 30.0f;        // a swing's base damage (the weapon's)
    std::string damage_type = "physical";
    float       blade_radius = 0.08f;
    std::string weapon;                // an item weapon it wields (an NPC): its moveset, damage and blade
    float       aggression = 0.6f;     // an NPC: 0..1, how soon and how long it presses an attack
    float       defense = 0.35f;       // an NPC: 0..1, how often it guards or dodges a swing coming
    // ---- set by the host each frame ----
    glm::vec3   blade_a{0.0f}, blade_b{0.0f};   // hilt and tip, world
    bool        blade = false;                  // the segment is set this frame
    glm::vec3   forward{0.0f};                  // where its body faces (world); zero: its Transform's +Z
    float       clip_length = -1.0f;            // the playing clip's length (seconds), when known
    // ---- runtime ----
    enum class Act : uint8_t { Ready, Move, Charging, Guarding };
    Act         act = Act::Ready;
    int         move = -1;
    float       t = 0.0f;                       // clip seconds into the move
    float       charge = 0.0f;                  // a heavy held this long
    float       charge_mult = 1.0f;
    char        buffered = 0;                   // 'l' light, 'h' heavy, 'd' dodge, 'p' parry
    float       buffer_left = 0.0f;
    std::vector<Entity> hit_this;               // reached by this move (once each)
    glm::vec3   prev_a{0.0f}, prev_b{0.0f};
    bool        prev_ok = false;
    bool        arc_done = false;
    float       since_spent = 99.0f, since_poise_hit = 99.0f;
    float       vulnerable = 0.0f;              // > 0: open to a riposte (parried, guard broken)
    Entity      riposte_of = null_entity;       // being riposted by
    Entity      face = null_entity;             // whom to turn to (tracking, a riposte)
    float       face_rate = 0.0f;               // degrees a second
    glm::vec3   turn_to{0.0f};                  // a move began toward this way (world, flat): turn there at once
    // ---- for the host: what to play ----
    std::string clip;
    bool        clip_fresh = false;
    float       clip_rate = 1.0f;
    bool        clip_loop = false;
    bool        root_motion = false;
};

/// What a blade did this frame, for the host to show and feel (hit-stop, a shake,
/// sparks, blood): queued in the world's context, taken each frame.
struct MeleeHitInfo {
    enum class Kind : uint8_t { Hit, Riposte, Blocked, GuardBroken, Parried, Dodged };
    Kind      kind = Kind::Hit;
    Entity    attacker = null_entity, target = null_entity;
    float     dealt = 0.0f, poise = 0.0f;
    glm::vec3 at{0.0f};                // where it landed
};
struct MeleeHits { std::vector<MeleeHitInfo> list; };
inline MeleeHits& melee_hits(World& w) { return w.registry().ctx().emplace<MeleeHits>(); }

namespace melee_detail {
inline void publish(GameplayEventBus* bus, const char* name, Entity by, Entity target, float value, const std::string& id) {
    if (!bus) return;
    GameplayEvent ev; ev.name = name; ev.instigator = by; ev.target = target; ev.value = value; ev.param = id;
    bus->publish(ev);
}
inline std::string team(World& w, Entity e) {
    if (!w.valid(e)) return {};
    if (const GameplayTags* t = w.try_get<GameplayTags>(e))
        for (const auto& s : t->tags) if (s.rfind("team.", 0) == 0) return s;
    return {};
}
inline float stamina(World& w, Entity e) {
    const AttributeSet* a = w.try_get<AttributeSet>(e);
    const Attribute* s = a ? a->find("Stamina") : nullptr;
    return s ? s->current : 1e9f;   // none: unlimited
}
inline void spend(World& w, Entity e, MeleeFighter& f, float amount) {
    if (amount <= 0.0f) return;
    if (AttributeSet* a = w.try_get<AttributeSet>(e))
        if (Attribute* s = a->find("Stamina")) s->current = std::max(s->min, s->current - amount);
    f.since_spent = 0.0f;
}
inline glm::vec3 facing(World& w, Entity e) {
    // The host may say where the body faces (its model, whatever way its entity is turned).
    if (const MeleeFighter* mf = w.try_get<MeleeFighter>(e)) {
        const glm::vec3 g(mf->forward.x, 0.0f, mf->forward.z);
        if (glm::length(g) > 0.1f) return glm::normalize(g);
    }
    const Transform* t = w.try_get<Transform>(e);
    if (!t) return glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 f = t->rotation * glm::vec3(0.0f, 0.0f, 1.0f);   // a character model faces +Z
    const glm::vec3 flat(f.x, 0.0f, f.z);
    return glm::length(flat) > 1e-4f ? glm::normalize(flat) : glm::vec3(0.0f, 0.0f, 1.0f);
}
inline float move_end(const MeleeFighter& f, const MeleeMove& m) {
    if (m.kind == MeleeMove::Kind::React && m.length > 0.0f) return m.length;
    if (m.end > 0.0f) return m.end;
    return f.clip_length > 0.0f ? f.clip_length : 1.0f;
}
/// The point of a capsule segment nearest `p` -- distance.
inline float seg_dist(const glm::vec3& a, const glm::vec3& b, const glm::vec3& p) {
    const glm::vec3 d = b - a;
    const float l2 = glm::dot(d, d);
    const float u = l2 > 1e-9f ? std::clamp(glm::dot(p - a, d) / l2, 0.0f, 1.0f) : 0.0f;
    return glm::length(a + d * u - p);
}
}  // namespace melee_detail

/// Start a move (an index into the fighter's moveset).
inline void melee_start(World& w, Entity e, MeleeFighter& f, const MovesetDef& ms, int index, GameplayEventBus* bus) {
    const MeleeMove& m = ms.moves[static_cast<size_t>(index)];
    f.act = MeleeFighter::Act::Move;
    f.move = index;
    f.t = 0.0f;
    f.charge = 0.0f;
    f.hit_this.clear();
    f.prev_ok = false;
    f.arc_done = false;
    f.clip = m.clip;
    f.clip_fresh = true;
    f.clip_rate = m.rate;
    f.clip_loop = false;
    f.root_motion = true;
    f.clip_length = -1.0f;
    melee_detail::spend(w, e, f, m.stamina);
    if (m.kind == MeleeMove::Kind::Attack) melee_detail::publish(bus, "melee.attack", e, null_entity, m.damage, m.id);
    if (m.kind == MeleeMove::Kind::Dodge) melee_detail::publish(bus, "melee.dodge", e, null_entity, 0.0f, m.id);
    if (m.kind == MeleeMove::Kind::Parry) melee_detail::publish(bus, "melee.parry_try", e, null_entity, 0.0f, "parry");
}

/// Back to its stance (or its guard, held).
inline void melee_rest(MeleeFighter& f, const MovesetDef& ms, bool guarding) {
    const int g = guarding ? ms.find(MeleeMove::Kind::Guard, "") : -1;
    f.act = g >= 0 ? MeleeFighter::Act::Guarding : MeleeFighter::Act::Ready;
    f.move = g;
    f.t = 0.0f;
    f.clip = g >= 0 ? ms.moves[static_cast<size_t>(g)].clip : ms.stance;
    f.clip_fresh = true;
    f.clip_rate = 1.0f;
    f.clip_loop = true;
    f.root_motion = false;
    f.clip_length = -1.0f;
    f.charge_mult = 1.0f;
}

/// A reaction on `e` (stagger, guard_hit, guard_break, parried, riposted): its
/// clip; parried and a broken guard leave it open to a riposte while it lasts.
inline bool melee_react(World& w, Entity e, const std::string& kind, GameplayEventBus* bus, Entity by = null_entity) {
    MeleeFighter* f = w.try_get<MeleeFighter>(e);
    const MovesetDef* ms = f ? find_moveset(f->moveset) : nullptr;
    melee_detail::publish(bus, ("melee." + kind).c_str(), by, e, 0.0f, kind);
    if (!f || !ms) return false;
    int i = ms->find(MeleeMove::Kind::React, kind);
    if (i < 0 && kind == "stagger_heavy") i = ms->find(MeleeMove::Kind::React, "stagger");
    if (i < 0) return false;
    melee_start(w, e, *f, *ms, i, nullptr);
    const MeleeMove& m = ms->moves[static_cast<size_t>(i)];
    f->vulnerable = (kind == "parried" || kind == "guard_break") ? std::max(0.6f, m.length > 0.0f ? m.length : 1.2f) : 0.0f;
    f->buffered = 0;
    return true;
}

namespace melee_detail {
/// The defender's side of a hit: i-frames, a parry, a guard, poise.
/// Returns the damage that got through (0: none).
inline float resolve_hit(World& w, Entity attacker, MeleeFighter& af, const MeleeMove& m, Entity target, GameplayEventBus* bus,
                         const glm::vec3& point) {
    auto note = [&](MeleeHitInfo::Kind k, float dealt) { melee_hits(w).list.push_back({k, attacker, target, dealt, m.poise, point}); };
    MeleeFighter* tf = w.try_get<MeleeFighter>(target);
    const MovesetDef* tms = tf ? find_moveset(tf->moveset) : nullptr;
    const MeleeMove* tm = tf && tms && tf->move >= 0 && tf->move < static_cast<int>(tms->moves.size()) ? &tms->moves[static_cast<size_t>(tf->move)] : nullptr;
    // Where the attacker stands, seen from the defender's front.
    const Transform* at = w.try_get<Transform>(attacker);
    const Transform* tt = w.try_get<Transform>(target);
    float front_deg = 0.0f;
    if (at && tt) {
        glm::vec3 to = at->position - tt->position;
        to.y = 0.0f;
        if (glm::length(to) > 1e-4f)
            front_deg = glm::degrees(std::acos(std::clamp(glm::dot(glm::normalize(to), facing(w, target)), -1.0f, 1.0f)));
    }
    // Invulnerable (a dodge's frames): it goes past.
    if (tm && tf->act == MeleeFighter::Act::Move && tm->iframes.on(tf->t)) {
        publish(bus, "melee.dodged", attacker, target, 0.0f, m.id);
        note(MeleeHitInfo::Kind::Dodged, 0.0f);
        return 0.0f;
    }
    // Parried: the attacker is thrown open.
    if (tm && tm->kind == MeleeMove::Kind::Parry && tm->window.on(tf->t) && front_deg <= 90.0f) {
        publish(bus, "melee.parried", target, attacker, 0.0f, m.id);
        melee_react(w, attacker, "parried", bus, target);
        note(MeleeHitInfo::Kind::Parried, 0.0f);
        return 0.0f;
    }
    // A riposte on one open to it: all of it, its own multiplier.
    const bool riposte = m.input == "riposte";
    float dmg = af.damage * m.damage * af.charge_mult * damage_bonus(w.try_get<AttributeSet>(attacker), "WeaponDamage");
    // Blocked: from the front, while guarding.
    if (!riposte && tf && tf->act == MeleeFighter::Act::Guarding && tm && tm->kind == MeleeMove::Kind::Guard &&
        front_deg <= tm->angle * 0.5f) {
        const float cost = m.poise * (100.0f - std::clamp(tm->stability, 0.0f, 100.0f)) / 100.0f * 2.0f + 4.0f;
        spend(w, target, *tf, cost);
        dmg *= 1.0f - std::clamp(tm->absorb, 0.0f, 1.0f);
        const bool broke = stamina(w, target) <= 0.0f;
        if (broke) {
            publish(bus, "melee.guard_broken", attacker, target, cost, m.id);
            melee_react(w, target, "guard_break", bus, attacker);
        } else {
            publish(bus, "melee.blocked", attacker, target, cost, m.id);
            const int gi = tms->find(MeleeMove::Kind::React, "guard_hit");
            if (gi >= 0) {
                melee_start(w, target, *tf, *tms, gi, nullptr);
                tf->act = MeleeFighter::Act::Move;
            }
        }
        float through = 0.0f;
        if (dmg > 0.0f) {
            DamageInfo di{dmg, af.damage_type, "Health", attacker};
            di.source = m.id;
            through = apply_damage(w, target, di);
        }
        note(broke ? MeleeHitInfo::Kind::GuardBroken : MeleeHitInfo::Kind::Blocked, through);
        return through;
    }
    DamageInfo di{dmg, af.damage_type, "Health", attacker};
    di.source = m.id;
    const float dealt = apply_damage(w, target, di);
    publish(bus, riposte ? "melee.riposte" : "melee.hit", attacker, target, dealt, m.id);
    note(riposte ? MeleeHitInfo::Kind::Riposte : MeleeHitInfo::Kind::Hit, dealt);
    // Poise: broken, a stagger -- unless it has hyperarmor now.
    if (CombatActor* ca = w.try_get<CombatActor>(target)) {
        const bool armored = tm && tf->act == MeleeFighter::Act::Move && tm->hyperarmor.on(tf->t);
        ca->poise -= m.poise;
        if (tf) tf->since_poise_hit = 0.0f;
        if (ca->poise <= 0.0f && !armored && !riposte) {
            ca->poise = ca->max_poise;
            const bool heavy = m.poise >= ca->max_poise * 0.6f;
            if (!melee_react(w, target, heavy ? "stagger_heavy" : "stagger", bus, attacker))
                publish(bus, "melee.stagger", attacker, target, m.poise, heavy ? "stagger_heavy" : "stagger");
        }
    }
    return dealt;
}
}  // namespace melee_detail

/// One frame of a fighter: its presses, its move's windows (hits, i-frames,
/// a parry), the next move at the cancel point, stamina and poise back.
inline void step_melee(World& w, Entity e, MeleeFighter& f, const MeleeInput& in, float dt, GameplayEventBus* bus) {
    const MovesetDef* ms = find_moveset(f.moveset);
    if (!ms) return;
    f.clip_fresh = false;
    f.face = null_entity;
    f.face_rate = 0.0f;
    f.turn_to = glm::vec3(0.0f);
    // Stamina and poise come back after a pause.
    f.since_spent += dt;
    f.since_poise_hit += dt;
    if (f.since_spent >= ms->stamina_delay && f.act != MeleeFighter::Act::Guarding)
        if (AttributeSet* a = w.try_get<AttributeSet>(e))
            if (Attribute* s = a->find("Stamina")) s->current = std::min(s->max, s->current + ms->stamina_regen * dt);
    if (f.act == MeleeFighter::Act::Guarding && f.since_spent >= ms->stamina_delay)
        if (AttributeSet* a = w.try_get<AttributeSet>(e))
            if (Attribute* s = a->find("Stamina")) s->current = std::min(s->max, s->current + ms->stamina_regen * 0.5f * dt);
    if (f.since_poise_hit >= ms->poise_delay)
        if (CombatActor* ca = w.try_get<CombatActor>(e)) ca->poise = std::min(ca->max_poise, ca->poise + ms->poise_regen * dt);
    if (f.vulnerable > 0.0f) f.vulnerable = std::max(0.0f, f.vulnerable - dt);
    // Presses wait for their moment.
    if (in.light) { f.buffered = 'l'; f.buffer_left = ms->buffer; }
    if (in.heavy) { f.buffered = 'h'; f.buffer_left = ms->buffer; }
    if (in.dodge) { f.buffered = 'd'; f.buffer_left = ms->buffer; }
    if (in.parry) { f.buffered = 'p'; f.buffer_left = ms->buffer; }
    if (f.buffer_left > 0.0f) f.buffer_left -= dt;
    if (f.buffer_left <= 0.0f) f.buffered = 0;
    if (f.clip.empty() && f.act == MeleeFighter::Act::Ready) melee_rest(f, *ms, false);

    const bool has_stamina = melee_detail::stamina(w, e) > 0.0f;
    // The move a press starts now, from the current one (-1 none).
    auto next_for = [&](char press, const std::string& from) -> int {
        if (press == 'd') {
            const bool roll = in.moving && glm::length(in.move_dir) > 0.1f;
            int i = ms->find(MeleeMove::Kind::Dodge, roll ? "roll" : "backstep");
            if (i < 0) i = ms->find(MeleeMove::Kind::Dodge, roll ? "backstep" : "roll");
            return i;
        }
        if (press == 'p') return ms->find(MeleeMove::Kind::Parry, "");
        const std::string base = press == 'h' ? "heavy" : "light";
        // A riposte: a light press with one open to it in reach, in front.
        if (press == 'l') {
            Entity best = null_entity;
            float best_d = ms->riposte_reach;
            const Transform* me = w.try_get<Transform>(e);
            const std::string myteam = melee_detail::team(w, e);
            if (me)
                w.each<MeleeFighter, Transform>([&](Entity o, MeleeFighter& of, Transform& ot) {
                    if (o == e || of.vulnerable <= 0.0f) return;
                    if (!myteam.empty() && melee_detail::team(w, o) == myteam) return;
                    const float d = glm::length(ot.position - me->position);
                    if (d < best_d) { best_d = d; best = o; }
                });
            if (best != null_entity) {
                const int r = ms->find(MeleeMove::Kind::Attack, "riposte");
                if (r >= 0) { f.riposte_of = best; return r; }
            }
        }
        // From a chain, a roll, a run, a jump.
        if (!from.empty()) {
            const int c = ms->find(MeleeMove::Kind::Attack, base, from);
            if (c >= 0) return c;
        }
        if (const MeleeMove* cur = f.act == MeleeFighter::Act::Move && f.move >= 0 && f.move < static_cast<int>(ms->moves.size())
                                       ? &ms->moves[static_cast<size_t>(f.move)] : nullptr;
            cur && cur->kind == MeleeMove::Kind::Dodge && cur->input == "roll") {
            const int ra = ms->find(MeleeMove::Kind::Attack, "roll_" + base);
            if (ra >= 0) return ra;
        }
        if (in.airborne) { const int j = ms->find(MeleeMove::Kind::Attack, "jump_" + base); if (j >= 0) return j; }
        if (in.running) { const int r = ms->find(MeleeMove::Kind::Attack, "run_" + base); if (r >= 0) return r; }
        return ms->find(MeleeMove::Kind::Attack, base);
    };
    auto take_press = [&](const std::string& from) {
        if (!f.buffered || !has_stamina) return false;
        const char p = f.buffered;
        const int n = next_for(p, from);
        if (n < 0) { f.buffered = 0; return false; }
        f.buffered = 0;
        f.charge_mult = 1.0f;
        const MeleeMove& m = ms->moves[static_cast<size_t>(n)];
        // A dodge goes the way it is pushed; so does a swing at no one in particular.
        const glm::vec3 flat(in.move_dir.x, 0.0f, in.move_dir.z);
        if (in.moving && glm::length(flat) > 0.1f &&
            (m.kind == MeleeMove::Kind::Dodge || (m.kind == MeleeMove::Kind::Attack && in.target == null_entity && m.input != "riposte")))
            f.turn_to = glm::normalize(flat);
        if (m.kind == MeleeMove::Kind::Attack && !m.charge_clip.empty() && m.charge_max > 0.0f && m.charge_from <= 0.0f && in.heavy_held) {
            // Held: its charge first; it swings when let go (or at its fullest).
            f.act = MeleeFighter::Act::Charging;
            f.move = n;
            f.charge = 0.0f;
            f.clip = m.charge_clip;
            f.clip_fresh = true;
            f.clip_rate = 1.0f;
            f.clip_loop = false;
            f.root_motion = true;
            f.clip_length = -1.0f;
            melee_detail::spend(w, e, f, m.stamina);
            return true;
        }
        melee_start(w, e, f, *ms, n, bus);
        if (m.input == "riposte" && f.riposte_of != null_entity) {
            melee_react(w, f.riposte_of, "riposted", bus, e);
            if (MeleeFighter* vf = w.try_get<MeleeFighter>(f.riposte_of)) { vf->vulnerable = 0.0f; vf->riposte_of = e; }
            f.face = f.riposte_of;
            f.face_rate = 1080.0f;
        }
        return true;
    };

    switch (f.act) {
        case MeleeFighter::Act::Ready:
        case MeleeFighter::Act::Guarding: {
            if (take_press("")) break;
            const bool g = in.guard && ms->find(MeleeMove::Kind::Guard, "") >= 0;
            if (g != (f.act == MeleeFighter::Act::Guarding)) melee_rest(f, *ms, g);
            break;
        }
        case MeleeFighter::Act::Charging: {
            const MeleeMove& m = ms->moves[static_cast<size_t>(f.move)];
            f.charge += dt;
            if (in.target != null_entity) { f.face = in.target; f.face_rate = std::max(m.track, 180.0f); }
            if (!in.heavy_held || f.charge >= m.charge_max) {
                const float k = m.charge_max > 0.0f ? std::clamp(f.charge / m.charge_max, 0.0f, 1.0f) : 0.0f;
                const float mult = 1.0f + (m.charge_damage - 1.0f) * k;
                int idx = f.move;
                if (!m.charge_release.empty())
                    for (size_t i = 0; i < ms->moves.size(); ++i)
                        if (ms->moves[i].kind == MeleeMove::Kind::Attack && ms->moves[i].id == m.charge_release) idx = static_cast<int>(i);
                const float stamina_back = m.stamina;   // (spent as the charge began; the release spends its own)
                AttributeSet* a = w.try_get<AttributeSet>(e);
                if (Attribute* s = a ? a->find("Stamina") : nullptr) s->current = std::min(s->max, s->current + stamina_back);
                melee_start(w, e, f, *ms, idx, bus);
                f.charge_mult = mult;
            }
            break;
        }
        case MeleeFighter::Act::Move: {
            const MeleeMove& m = ms->moves[static_cast<size_t>(f.move)];
            f.t += dt * std::max(0.05f, m.rate);
            // Held into its wind-up: the charge (its hold clip from the wound-up pose).
            if (m.kind == MeleeMove::Kind::Attack && !m.charge_clip.empty() && m.charge_max > 0.0f && m.charge_from > 0.0f &&
                in.heavy_held && f.t >= m.charge_from && (m.hits.empty() || f.t < m.hits.front().from)) {
                f.act = MeleeFighter::Act::Charging;
                f.charge = 0.0f;
                f.clip = m.charge_clip;
                f.clip_fresh = true;
                f.clip_rate = 1.0f;
                f.clip_loop = false;
                f.root_motion = true;
                f.clip_length = -1.0f;
                break;
            }
            // Turning toward its target until its first hit.
            if (m.track > 0.0f && in.target != null_entity && (m.hits.empty() || f.t < m.hits.front().from)) {
                f.face = in.target;
                f.face_rate = m.track;
            }
            if (f.riposte_of != null_entity && m.input == "riposte") { f.face = f.riposte_of; f.face_rate = 1080.0f; }
            // The blade: swept between last frame's segment and this one's, in a hit window.
            int window = -1;
            for (size_t i = 0; i < m.hits.size(); ++i) if (m.hits[i].on(f.t)) { window = static_cast<int>(i); break; }
            if (window >= 0 && m.kind == MeleeMove::Kind::Attack) {
                const std::string myteam = melee_detail::team(w, e);
                std::vector<Entity> reached;
                if (m.input == "riposte" && f.riposte_of != null_entity && w.valid(f.riposte_of)) {
                    // A riposte lands on the one it was for (held in place by its reaction).
                    if (std::find(f.hit_this.begin(), f.hit_this.end(), f.riposte_of) == f.hit_this.end()) reached.push_back(f.riposte_of);
                } else if (f.blade) {
                    const glm::vec3 a0 = f.prev_ok ? f.prev_a : f.blade_a, b0 = f.prev_ok ? f.prev_b : f.blade_b;
                    w.each<CombatActor, Transform>([&](Entity o, CombatActor& ca, Transform& ot) {
                        if (o == e || (!myteam.empty() && melee_detail::team(w, o) == myteam)) return;
                        if (std::find(f.hit_this.begin(), f.hit_this.end(), o) != f.hit_this.end()) return;
                        const HitSphere hb = combat_hurtbox(ca, ot.position);
                        for (int k = 0; k <= 4; ++k) {
                            const float u = static_cast<float>(k) / 4.0f;
                            const glm::vec3 a = a0 + (f.blade_a - a0) * u, b = b0 + (f.blade_b - b0) * u;
                            if (melee_detail::seg_dist(a, b, hb.center) <= hb.radius + f.blade_radius) { reached.push_back(o); break; }
                        }
                    });
                } else if (!f.arc_done) {
                    // No blade in the world (a server, a model without a hand): an arc in front, once.
                    f.arc_done = true;
                    const Transform* me = w.try_get<Transform>(e);
                    const glm::vec3 fwd = melee_detail::facing(w, e);
                    const float cos_half = std::cos(glm::radians(std::clamp(m.arc, 1.0f, 360.0f) * 0.5f));
                    if (me)
                        w.each<CombatActor, Transform>([&](Entity o, CombatActor& ca, Transform& ot) {
                            if (o == e || (!myteam.empty() && melee_detail::team(w, o) == myteam)) return;
                            if (std::find(f.hit_this.begin(), f.hit_this.end(), o) != f.hit_this.end()) return;
                            glm::vec3 to = ot.position - me->position;
                            to.y = 0.0f;
                            const float d = glm::length(to);
                            if (d > m.reach + ca.hurt_radius) return;
                            if (d > 0.3f && glm::dot(to / d, fwd) < cos_half) return;
                            reached.push_back(o);
                        });
                }
                for (Entity o : reached) {
                    f.hit_this.push_back(o);
                    glm::vec3 at(0.0f);
                    if (const Transform* ot = w.try_get<Transform>(o)) {
                        at = ot->position + glm::vec3(0.0f, 1.0f, 0.0f);
                        if (const CombatActor* oc = w.try_get<CombatActor>(o)) at = combat_hurtbox(*oc, ot->position).center;
                        if (f.blade) {
                            // The point of the blade nearest it.
                            const glm::vec3 d = f.blade_b - f.blade_a;
                            const float l2 = glm::dot(d, d);
                            const float u = l2 > 1e-9f ? std::clamp(glm::dot(at - f.blade_a, d) / l2, 0.0f, 1.0f) : 0.5f;
                            at = f.blade_a + d * u;
                        }
                    }
                    melee_detail::resolve_hit(w, e, f, m, o, bus, at);
                    if (f.act != MeleeFighter::Act::Move || f.move < 0) break;   // (parried: its own move ended)
                }
            } else if (window < 0) {
                f.arc_done = false;
                // Between two hits of one move, the next may reach the same one again.
                if (!m.hits.empty() && f.t >= m.hits.front().to) {
                    bool later = false;
                    for (const auto& h : m.hits) later = later || h.from > f.t;
                    if (later) f.hit_this.clear();
                }
            }
            if (f.act != MeleeFighter::Act::Move) break;   // a reaction took over
            // The next move at its cancel point; over at its end.
            const MeleeMove& m2 = ms->moves[static_cast<size_t>(f.move)];
            const float end = melee_detail::move_end(f, m2);
            const float cancel = m2.cancel >= 0.0f ? m2.cancel : end;
            const bool can_cancel = m2.kind != MeleeMove::Kind::React && f.t >= cancel;
            if (can_cancel && f.buffered) {
                const std::string from = m2.kind == MeleeMove::Kind::Attack ? m2.id : "";
                if (take_press(from)) break;
            }
            if (f.t >= end) {
                f.riposte_of = null_entity;
                melee_rest(f, *ms, in.guard);
            }
            break;
        }
    }
    f.prev_a = f.blade_a;
    f.prev_b = f.blade_b;
    f.prev_ok = f.blade;
    f.blade = false;   // (the host sets it again next frame)
}

/// Every fighter's frame, with its inputs (index-matched to `fighters`).
inline void step_melee_all(World& w, const std::vector<std::pair<Entity, MeleeInput>>& inputs, float dt, GameplayEventBus* bus) {
    for (const auto& [e, in] : inputs)
        if (w.valid(e))
            if (MeleeFighter* f = w.try_get<MeleeFighter>(e)) step_melee(w, e, *f, in, dt, bus);
}

/// Is it invulnerable now (a dodge's frames)?
inline bool melee_invulnerable(World& w, Entity e) {
    const MeleeFighter* f = w.try_get<MeleeFighter>(e);
    const MovesetDef* ms = f ? find_moveset(f->moveset) : nullptr;
    if (!f || !ms || f->act != MeleeFighter::Act::Move || f->move < 0) return false;
    return ms->moves[static_cast<size_t>(f->move)].iframes.on(f->t);
}
/// The id of the move it is in ("" standing, "guard" guarding, a reaction's kind).
inline std::string melee_move_id(World& w, Entity e) {
    const MeleeFighter* f = w.try_get<MeleeFighter>(e);
    const MovesetDef* ms = f ? find_moveset(f->moveset) : nullptr;
    if (!f || !ms || f->move < 0 || f->move >= static_cast<int>(ms->moves.size())) return {};
    if (f->act == MeleeFighter::Act::Guarding) return "guard";
    return ms->moves[static_cast<size_t>(f->move)].id;
}

// ---- the component ------------------------------------------------------------------------
inline void serialize_melee_fighter(const void* obj, std::vector<uint8_t>& out) {
    const auto& f = *static_cast<const MeleeFighter*>(obj);
    detail::put_i32(out, 1);
    detail::put_str(out, f.moveset);
    detail::put_f32(out, f.damage);
    detail::put_str(out, f.damage_type);
    detail::put_f32(out, f.blade_radius);
    detail::put_str(out, f.weapon);
    detail::put_f32(out, f.aggression);
    detail::put_f32(out, f.defense);
}
inline void deserialize_melee_fighter(void* obj, const uint8_t* data, size_t n) {
    auto& f = *static_cast<MeleeFighter*>(obj);
    f = MeleeFighter{};
    const uint8_t* p = data; const uint8_t* e = data + n;
    int32_t ver = 0;
    if (!detail::get_bytes(p, e, &ver, 4) || ver < 1) return;
    detail::get_str(p, e, f.moveset);
    detail::get_f32(p, e, f.damage);
    detail::get_str(p, e, f.damage_type);
    detail::get_f32(p, e, f.blade_radius);
    detail::get_str(p, e, f.weapon);
    detail::get_f32(p, e, f.aggression);
    detail::get_f32(p, e, f.defense);
}
inline void register_melee_components() {
    register_authorable(make_authorable_custom<MeleeFighter>("Melee Fighter", &serialize_melee_fighter, &deserialize_melee_fighter));
}

}  // namespace schizo::ecs
