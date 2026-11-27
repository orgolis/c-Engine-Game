#pragma once
// ============================================================================
// gameplay_status.h -- statuses: conditions put on a character that last a
// while, may stack, change its attributes and tags, hurt or heal it over time,
// shield it, and set things off at moments of their own.
// ============================================================================
//
// What a game makes of them is its own: a burn that stacks and bursts, a chill
// that freezes, a poison, a blessing, a mark that makes its bearer take more,
// a shield, a hidden player, a stun. A status is DATA (`status <id>` blocks in
// .items files):
//
//   status burning
//     name Burning
//     labels fire debuff        # free words the game's rules can look for (perks)
//     stacks 10                 # how many it holds at most (1: it does not stack)
//     duration 4                # seconds; every new application refreshes it (0: until cleared)
//     decay 1                   # a stack falls off every second (0: all go at the end)
//     tick 0.5 3 fire           # every 0.5 s, 3 fire damage per stack ("heal": heals)
//     mod MoveSpeed -5          # an attribute bonus while on, per stack
//     taken 15                  # it takes 15% more damage, per stack (DamageTaken)
//     dealt -20                 # it deals 20% less, per stack (Damage)
//     shield 60                 # a shield that takes damage first (Shield)
//     tag status.stunned        # tags while on
//     at 10 burst flare         # at 10 stacks: the action, then the status ends
//     hurt 40 burst shatter     # hit for 40 or more at once: the action (at most every 0.5 s)
//     death spread burning 3 6  # its bearer dies with it on
//     expire apply chilled 1    # it runs out
//     vfx burning               # its look on the bearer (vfx/effects.h, streamed)
//     about Burns; at 10 stacks it flares up.
//   end
//
// Actions -- the same words a perk's rules use (gameplay_perks.h runs them):
//   burst <ability>               the ability goes off around the bearer
//   apply <status> [stacks]       to the bearer
//   self <status> [stacks]        to the one who put the status on
//   spread <status> <stacks> <m>  to those on the bearer's side around it
//   chain <damage> <type> <n> <m> damage leaping to n others within m metres
//   damage <amount> [type]        to the bearer
//   heal <amount>                 the one who put it on
//   add <attribute> <amount> [per]  to that one's attribute (per: times the moment's amount)
//   recharge <label|all> <percent>  that one's abilities with the label cool down so much sooner
//   clear [status]                ends a status on the bearer (itself when none is named)
//
// Immune: a bearer with the tag immune.<status> or immune.<one of its labels>.
//
// This header keeps the statuses (apply, stack, decay, tick, expire, their
// bonuses, tags and shields) and QUEUES the actions their moments call for
// (PendingActions in the world's context); gameplay_perks.h runs them, so a
// chain of reactions travels a frame at a time and never recurses.
//
// Pure ECS: checked by tools/gameplay_check.

#include "world.h"
#include "gameplay_attributes.h"
#include "gameplay_combat.h"
#include "gameplay_damage.h"
#include "gameplay_events.h"
#include "gameplay_tags.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace schizo::ecs {

// ---- actions ------------------------------------------------------------------------
struct TriggerAction {
    enum class Kind : uint8_t { Burst, Apply, Self, Spread, Chain, Damage, Heal, Add, Recharge, Clear };
    Kind        kind = Kind::Apply;
    std::string id;     // burst: ability; apply/self/spread/clear: status; add: attribute; recharge: label
    std::string type;   // chain/damage: damage type
    float a = 0.0f;     // apply/self/spread: stacks; chain/damage: damage; heal/add: amount; recharge: percent
    float b = 0.0f;     // chain: how many
    float c = 0.0f;     // spread/chain: radius (m)
    bool  per = false;  // add: times the moment's amount
};

/// Read an action from the rest of a line ("burst flare", "apply chilled 2" ...).
inline bool parse_trigger_action(std::istringstream& ls, TriggerAction& out) {
    using K = TriggerAction::Kind;
    std::string k;
    if (!(ls >> k)) return false;
    TriggerAction a;
    if (k == "burst") { a.kind = K::Burst; ls >> a.id; }
    else if (k == "apply" || k == "self") {
        a.kind = k == "apply" ? K::Apply : K::Self;
        ls >> a.id;
        if (!(ls >> a.a)) a.a = 1.0f;
    }
    else if (k == "spread") { a.kind = K::Spread; ls >> a.id >> a.a >> a.c; }
    else if (k == "chain") { a.kind = K::Chain; ls >> a.a >> a.type >> a.b >> a.c; }
    else if (k == "damage") { a.kind = K::Damage; ls >> a.a; if (!(ls >> a.type)) a.type.clear(); }
    else if (k == "heal") { a.kind = K::Heal; ls >> a.a; }
    else if (k == "add") { a.kind = K::Add; ls >> a.id >> a.a; std::string p; if (ls >> p) a.per = p == "per"; }
    else if (k == "recharge") { a.kind = K::Recharge; ls >> a.id >> a.a; }
    else if (k == "clear") { a.kind = K::Clear; if (!(ls >> a.id)) a.id.clear(); }
    else return false;
    const bool needs_id = a.kind == K::Burst || a.kind == K::Apply || a.kind == K::Self || a.kind == K::Spread ||
                          a.kind == K::Add || a.kind == K::Recharge;
    if (needs_id && a.id.empty()) return false;
    out = a;
    return true;
}

inline std::string trigger_action_text(const TriggerAction& a) {
    using K = TriggerAction::Kind;
    auto num = [](float v) {
        std::ostringstream o;
        o << v;
        return o.str();
    };
    switch (a.kind) {
        case K::Burst:    return "burst " + a.id;
        case K::Apply:    return "apply " + a.id + (a.a != 1.0f ? " " + num(a.a) : "");
        case K::Self:     return "self " + a.id + (a.a != 1.0f ? " " + num(a.a) : "");
        case K::Spread:   return "spread " + a.id + " " + num(a.a) + " " + num(a.c);
        case K::Chain:    return "chain " + num(a.a) + " " + a.type + " " + num(a.b) + " " + num(a.c);
        case K::Damage:   return "damage " + num(a.a) + (a.type.empty() ? "" : " " + a.type);
        case K::Heal:     return "heal " + num(a.a);
        case K::Add:      return "add " + a.id + " " + num(a.a) + (a.per ? " per" : "");
        case K::Recharge: return "recharge " + a.id + " " + num(a.a);
        case K::Clear:    return "clear" + (a.id.empty() ? std::string() : " " + a.id);
    }
    return {};
}

// ---- definitions ---------------------------------------------------------------------
struct StatusMod { std::string attribute; float amount = 0.0f; };

struct StatusTrigger {
    enum class When : uint8_t { Stacks, Hurt, Death, Expire };
    When          when = When::Stacks;
    float         value = 0.0f;   // stacks: how many; hurt: the least damage of one hit
    TriggerAction action;
};

struct StatusDef {
    std::string id, name, about, vfx;
    std::vector<std::string> labels;
    int   max_stacks = 1;
    float duration = 5.0f;          // 0: until cleared
    float decay = 0.0f;             // > 0: a stack falls off every so many seconds
    float tick_period = 0.0f, tick_amount = 0.0f;
    std::string tick_type;          // a damage type, or "heal"
    std::vector<StatusMod> mods;    // per stack
    float shield = 0.0f;
    std::vector<std::string> tags;
    std::vector<StatusTrigger> triggers;
    bool has_label(const std::string& l) const { return std::find(labels.begin(), labels.end(), l) != labels.end(); }
};

inline std::vector<StatusDef>& status_def_registry() { static std::vector<StatusDef> r; return r; }
inline void register_status_def(const StatusDef& d) {
    for (auto& e : status_def_registry()) if (e.id == d.id) { e = d; return; }
    status_def_registry().push_back(d);
}
inline const StatusDef* find_status_def(const std::string& id) {
    for (const auto& e : status_def_registry()) if (e.id == id) return &e;
    return nullptr;
}

// ---- data: `status <id>` blocks --------------------------------------------------------
inline std::vector<StatusDef> parse_statuses_text(const std::string& text) {
    std::vector<StatusDef> out;
    StatusDef cur;
    bool have = false;
    auto flush = [&] { if (have && !cur.id.empty()) out.push_back(cur); cur = StatusDef{}; have = false; };
    auto rest_of = [](std::istringstream& ls) {
        std::string r;
        std::getline(ls, r);
        size_t a = 0, b = r.size();
        while (a < b && std::isspace(static_cast<unsigned char>(r[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(r[b - 1]))) --b;
        return r.substr(a, b - a);
    };
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#') continue;
        if (key == "status") { flush(); ls >> cur.id; cur.name = cur.id; have = true; }
        else if (key == "item" || key == "weapon" || key == "ability" || key == "skill" || key == "perk") { flush(); }
        else if (!have) { continue; }
        else if (key == "end") { flush(); }
        else if (key == "name") { cur.name = rest_of(ls); }
        else if (key == "labels" || key == "label") { for (std::string w; ls >> w;) { if (w[0] == '#') break; cur.labels.push_back(w); } }
        else if (key == "stacks") { ls >> cur.max_stacks; cur.max_stacks = std::max(1, cur.max_stacks); }
        else if (key == "duration") { ls >> cur.duration; }
        else if (key == "decay") { ls >> cur.decay; }
        else if (key == "tick") { ls >> cur.tick_period >> cur.tick_amount; if (!(ls >> cur.tick_type)) cur.tick_type.clear(); }
        else if (key == "mod") { StatusMod m; ls >> m.attribute >> m.amount; if (!m.attribute.empty()) cur.mods.push_back(m); }
        else if (key == "taken") { StatusMod m{"DamageTaken", 0.0f}; ls >> m.amount; cur.mods.push_back(m); }
        else if (key == "dealt") { StatusMod m{"Damage", 0.0f}; ls >> m.amount; cur.mods.push_back(m); }
        else if (key == "shield") { ls >> cur.shield; }
        else if (key == "tag") { std::string t; ls >> t; if (!t.empty()) cur.tags.push_back(t); }
        else if (key == "at" || key == "hurt" || key == "death" || key == "expire") {
            StatusTrigger t;
            t.when = key == "at" ? StatusTrigger::When::Stacks : key == "hurt" ? StatusTrigger::When::Hurt
                   : key == "death" ? StatusTrigger::When::Death : StatusTrigger::When::Expire;
            if (t.when == StatusTrigger::When::Stacks || t.when == StatusTrigger::When::Hurt) ls >> t.value;
            if (parse_trigger_action(ls, t.action)) cur.triggers.push_back(t);
        }
        else if (key == "vfx") { ls >> cur.vfx; }
        else if (key == "about") { cur.about = rest_of(ls); }
    }
    flush();
    return out;
}

// ---- on an entity ------------------------------------------------------------------------
struct StatusInstance {
    std::string id;
    int    stacks = 0;
    float  remaining = 0.0f;       // seconds left (duration > 0)
    float  decay_timer = 0.0f, tick_timer = 0.0f, hurt_cooldown = 0.0f;
    Entity by = null_entity;       // who put it on (last)
    float  shield_left = 0.0f;
    std::vector<std::pair<std::string, float>> applied;   // the bonuses as applied (exact undo)
};
/// The statuses on an entity (runtime; not saved).
struct Statuses { std::vector<StatusInstance> list; };

/// An action a status (or a perk) calls for, waiting for gameplay_perks.h to run it.
struct PendingAction {
    TriggerAction action;
    Entity      subject = null_entity;   // the bearer (status), the one hit (perk)
    Entity      by = null_entity;        // the one responsible (who put the status on; the perk's owner)
    glm::vec3   at{0.0f};
    std::string from;                    // "status:<id>" or "perk:<id>"
    float       amount = 0.0f;           // the moment's amount (damage dealt ...), for `add ... per`
};
struct PendingActions { std::vector<PendingAction> list; };

inline PendingActions& pending_actions(World& w) { return w.registry().ctx().emplace<PendingActions>(); }

/// Where an entity is, for what goes off around it: its hurtbox's middle (else a little above its feet).
inline glm::vec3 status_point(World& w, Entity e) {
    const Transform* t = w.valid(e) ? w.try_get<Transform>(e) : nullptr;
    if (!t) return glm::vec3(0.0f);
    if (const auto* ca = w.try_get<CombatActor>(e)) return combat_hurtbox(*ca, t->position).center;
    return t->position + glm::vec3(0.0f, 0.9f, 0.0f);
}

namespace status_detail {
inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
/// The status's bonuses for its stacks now (undoing what it had before).
inline void set_bonuses(World& w, Entity e, StatusInstance& s, const StatusDef& d) {
    AttributeSet* a = w.try_get<AttributeSet>(e);
    if (a)
        for (const auto& [name, amt] : s.applied)
            if (Attribute* at = a->find(name)) at->current = clampf(at->current - amt, at->min, at->max);
    s.applied.clear();
    if (!a) return;
    for (const auto& m : d.mods) {
        Attribute* at = ensure_bonus_stat(*a, m.attribute);
        if (!at) continue;
        const float before = at->current;
        at->current = clampf(before + m.amount * static_cast<float>(s.stacks), at->min, at->max);
        s.applied.push_back({m.attribute, at->current - before});
    }
}
inline void queue(World& w, const TriggerAction& act, Entity subject, Entity by, const std::string& status) {
    pending_actions(w).list.push_back({act, subject, by, status_point(w, subject), "status:" + status, 0.0f});
}
inline void publish(GameplayEventBus* bus, const char* name, Entity by, Entity target, float value, const std::string& id) {
    if (!bus) return;
    GameplayEvent ev;
    ev.name = name;
    ev.instigator = by;
    ev.target = target;
    ev.value = value;
    ev.param = id;
    bus->publish(ev);
}
/// Undo everything the status does on its bearer (bonuses, tags, its shield).
inline void undo(World& w, Entity e, StatusInstance& s, const StatusDef* d) {
    s.stacks = 0;
    if (d) set_bonuses(w, e, s, *d);
    else s.applied.clear();
    if (d && !d->tags.empty())
        if (GameplayTags* g = w.try_get<GameplayTags>(e)) {
            for (const auto& t : d->tags) {
                // Another status may hold the same tag: keep it then.
                bool held = false;
                if (const Statuses* st = w.try_get<Statuses>(e))
                    for (const auto& o : st->list) {
                        if (&o == &s || o.stacks <= 0) continue;
                        if (const StatusDef* od = find_status_def(o.id))
                            held = held || std::find(od->tags.begin(), od->tags.end(), t) != od->tags.end();
                    }
                if (!held) g->remove_exact(t);
            }
        }
    if (s.shield_left > 0.0f)
        if (AttributeSet* a = w.try_get<AttributeSet>(e))
            if (Attribute* sh = a->find("Shield")) sh->current = std::max(0.0f, sh->current - std::min(sh->current, s.shield_left));
    s.shield_left = 0.0f;
}
inline bool bearer_dead(World& w, Entity e) {
    if (const GameplayTags* g = w.try_get<GameplayTags>(e); g && g->has("state.dead")) return true;
    if (const AttributeSet* a = w.try_get<AttributeSet>(e))
        if (const Attribute* h = a->find("Health")) return h->current <= 0.0f;
    return false;
}
}  // namespace status_detail

/// How many stacks of a status an entity has (0: none).
inline int status_stacks(World& w, Entity e, const std::string& id) {
    if (const Statuses* s = w.valid(e) ? w.try_get<Statuses>(e) : nullptr)
        for (const auto& i : s->list)
            if (i.id == id) return i.stacks;
    return 0;
}
inline bool has_status(World& w, Entity e, const std::string& id) { return status_stacks(w, e, id) > 0; }

/// Put `stacks` of a status on `e` (more of it, if it has it already: up to its
/// most, its time refreshed). Reaching a stack count with an `at` moment queues
/// its action, and the status ends. Returns the stacks it has now (0: refused --
/// no such status, a dead bearer, or immune).
inline int apply_status(World& w, Entity e, const std::string& id, int stacks, Entity by = null_entity,
                        GameplayEventBus* bus = nullptr) {
    const StatusDef* d = find_status_def(id);
    if (!d || stacks <= 0 || !w.valid(e) || status_detail::bearer_dead(w, e)) return 0;
    if (const GameplayTags* g = w.try_get<GameplayTags>(e)) {
        if (g->has("immune." + id)) return 0;
        for (const auto& l : d->labels) if (g->has("immune." + l)) return 0;
    }
    if (!w.has<Statuses>(e)) w.add<Statuses>(e, Statuses{});
    Statuses& st = w.get<Statuses>(e);
    StatusInstance* s = nullptr;
    for (auto& i : st.list) if (i.id == id) { s = &i; break; }
    if (!s) {
        st.list.push_back(StatusInstance{});
        s = &st.list.back();
        s->id = id;
    }
    const bool fresh = s->stacks <= 0;
    s->stacks = std::min(d->max_stacks, s->stacks + stacks);
    s->remaining = d->duration;
    s->decay_timer = 0.0f;
    if (w.valid(by)) s->by = by;
    status_detail::set_bonuses(w, e, *s, *d);
    if (fresh && !d->tags.empty()) {
        if (!w.has<GameplayTags>(e)) w.add<GameplayTags>(e, GameplayTags{});
        for (const auto& t : d->tags) w.get<GameplayTags>(e).add(t);
    }
    if (d->shield > 0.0f)
        if (AttributeSet* a = w.try_get<AttributeSet>(e)) {
            Attribute* sh = a->find("Shield");
            if (!sh) sh = &a->define("Shield", 0.0f, 0.0f, 100000.0f);
            // Topped up to its full shield (what was used of it is not given twice).
            const float left = std::min(s->shield_left, sh->current);
            sh->current += d->shield - left;
            s->shield_left = d->shield;
        }
    status_detail::publish(bus, "status.applied", s->by, e, static_cast<float>(s->stacks), id);
    // Enough stacks: it goes off.
    bool fired = false;
    for (const auto& t : d->triggers)
        if (t.when == StatusTrigger::When::Stacks && s->stacks >= static_cast<int>(t.value)) {
            status_detail::queue(w, t.action, e, s->by, id);
            fired = true;
        }
    if (fired) {
        const int now = s->stacks;
        status_detail::publish(bus, "status.triggered", s->by, e, static_cast<float>(now), id);
        status_detail::undo(w, e, *s, d);
        auto& list = w.get<Statuses>(e).list;
        list.erase(std::remove_if(list.begin(), list.end(), [](const StatusInstance& i) { return i.stacks <= 0; }), list.end());
        return now;
    }
    return s->stacks;
}

/// End a status on `e` (its bonuses, tags and shield undone). True when it had it.
inline bool clear_status(World& w, Entity e, const std::string& id) {
    Statuses* st = w.valid(e) ? w.try_get<Statuses>(e) : nullptr;
    if (!st) return false;
    bool had = false;
    for (auto& i : st->list)
        if (i.id == id && i.stacks > 0) { status_detail::undo(w, e, i, find_status_def(id)); had = true; }
    st->list.erase(std::remove_if(st->list.begin(), st->list.end(), [](const StatusInstance& i) { return i.stacks <= 0; }),
                   st->list.end());
    return had;
}
/// End every status on `e`.
inline void clear_all_statuses(World& w, Entity e) {
    Statuses* st = w.valid(e) ? w.try_get<Statuses>(e) : nullptr;
    if (!st) return;
    for (auto& i : st->list) status_detail::undo(w, e, i, find_status_def(i.id));
    st->list.clear();
}

/// One frame of every status: damage or healing over time, stacks falling off,
/// time running out, what hits do to them (`hurt`), and their bearers' deaths
/// (`death`; a dead bearer keeps none). Their actions are queued (above).
/// Then the frame's damage records are cleared.
inline void tick_statuses(World& w, float dt, GameplayEventBus* bus = nullptr) {
    using namespace status_detail;
    std::vector<Entity> bearers;
    w.each<Statuses>([&](Entity e, Statuses& s) { if (!s.list.empty()) bearers.push_back(e); });
    for (Entity e : bearers) {
        if (!w.valid(e) || !w.has<Statuses>(e)) continue;
        // Dead: its `death` moments, and nothing stays on it.
        if (bearer_dead(w, e)) {
            {
                if (!w.has<DamageLog>(e)) w.add<DamageLog>(e, DamageLog{});
                auto& with = w.get<DamageLog>(e).died_with;
                with.clear();
                for (const auto& s : w.get<Statuses>(e).list) if (s.stacks > 0) with.push_back(s.id);
            }
            for (auto& s : w.get<Statuses>(e).list)
                if (s.stacks > 0)
                    if (const StatusDef* d = find_status_def(s.id))
                        for (const auto& t : d->triggers)
                            if (t.when == StatusTrigger::When::Death) queue(w, t.action, e, s.by, s.id);
            clear_all_statuses(w, e);
            continue;
        }
        // Hits taken this frame (DamageLog): `hurt` moments.
        if (const DamageLog* log = w.try_get<DamageLog>(e); log && !log->hits.empty()) {
            const std::vector<DamageRecord> hits = log->hits;
            for (const auto& h : hits) {
                if (!w.has<Statuses>(e)) break;
                std::vector<std::pair<TriggerAction, Entity>> go;
                std::vector<std::string> from;
                for (auto& s : w.get<Statuses>(e).list) {
                    const StatusDef* d = s.stacks > 0 ? find_status_def(s.id) : nullptr;
                    if (!d || s.hurt_cooldown > 0.0f) continue;
                    bool any = false;
                    for (const auto& t : d->triggers)
                        if (t.when == StatusTrigger::When::Hurt && h.amount >= t.value) {
                            go.push_back({t.action, s.by});
                            from.push_back(s.id);
                            any = true;
                        }
                    if (any) s.hurt_cooldown = 0.5f;
                }
                for (size_t k = 0; k < go.size(); ++k) queue(w, go[k].first, e, go[k].second, from[k]);
            }
        }
        // Time: ticks, decay, the end.
        std::vector<std::string> ended;
        struct Tick { float amount; std::string type, id; Entity by; };
        std::vector<Tick> ticks;
        for (auto& s : w.get<Statuses>(e).list) {
            const StatusDef* d = find_status_def(s.id);
            if (!d || s.stacks <= 0) { ended.push_back(s.id); continue; }
            s.hurt_cooldown = std::max(0.0f, s.hurt_cooldown - dt);
            if (d->tick_period > 0.0f && d->tick_amount != 0.0f) {
                s.tick_timer += dt;
                while (s.tick_timer >= d->tick_period) {
                    s.tick_timer -= d->tick_period;
                    ticks.push_back({d->tick_amount * static_cast<float>(s.stacks), d->tick_type, s.id, s.by});
                }
            }
            if (d->decay > 0.0f) {
                s.decay_timer += dt;
                while (s.decay_timer >= d->decay && s.stacks > 0) {
                    s.decay_timer -= d->decay;
                    --s.stacks;
                    set_bonuses(w, e, s, *d);
                }
                if (s.stacks <= 0) { ended.push_back(s.id); continue; }
            }
            if (d->duration > 0.0f) {
                s.remaining -= dt;
                if (s.remaining <= 0.0f) ended.push_back(s.id);
            }
        }
        // Damage over time (by whoever put it on), healing over time.
        for (const auto& t : ticks) {
            if (!w.valid(e)) break;
            if (t.type == "heal") {
                if (AttributeSet* a = w.try_get<AttributeSet>(e)) a->adjust("Health", t.amount);
            } else {
                DamageInfo di{t.amount, t.type, "Health", w.valid(t.by) ? t.by : null_entity};
                di.source = "status:" + t.id;
                apply_damage(w, e, di);
            }
        }
        // Ran out: its `expire` moment, then gone.
        for (const auto& id : ended) {
            if (!w.has<Statuses>(e)) break;
            Entity by = null_entity;
            for (auto& s : w.get<Statuses>(e).list)
                if (s.id == id) by = s.by;
            if (const StatusDef* d = find_status_def(id))
                for (const auto& t : d->triggers)
                    if (t.when == StatusTrigger::When::Expire) queue(w, t.action, e, by, id);
            clear_status(w, e, id);
            publish(bus, "status.ended", by, e, 0.0f, id);
        }
    }
    // This frame's hits have been heard -- the damage over time dealt above too, so
    // a status never hears its own ticks (its actions' damage lands next frame,
    // inside the 0.5 s a `hurt` moment waits).
    w.each<DamageLog>([](Entity, DamageLog& log) { log.hits.clear(); });
}

}  // namespace schizo::ecs
