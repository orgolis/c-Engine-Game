#pragma once
// ============================================================================
// gameplay_perks.h -- perks: lasting rules a character carries ("when this
// happens, if that, do this"), plus the frame that runs statuses, ability
// zones and the actions both call for.
// ============================================================================
//
// A perk is whatever a game calls it -- a talent, a trait, a relic, a passive,
// a card -- and the game decides how a character gets one (a skill, an item, a
// script: add_perk / remove_perk) and how many it may carry. A perk is DATA
// (`perk <id>` blocks in .items files):
//
//   perk ember_hands
//     name Kindling Hands
//     labels ember aspect       # free words the game's own rules look for
//     mod CooldownReduction 5   # an attribute bonus while carried
//     tag perk.kindling         # tags while carried
//     when hit label melee      # a moment, with filters (below)
//     if target burning         # a condition (below)
//     chance 0.5
//     every 4                   # at most once in 4 s
//     do apply burning 3        # actions (gameplay_status.h): the one hit is the subject
//     do recharge melee 25
//     about Melee hits set targets alight.
//   end
//
// Moments (`when`): hit (its carrier hurt someone: an ability or a weapon),
// kill (its carrier killed someone), cast (its carrier used an ability), hurt
// (its carrier was hit), applied (its carrier put a status on someone),
// triggered (a status its carrier put on went off at its stack count).
// Filters after the moment, all of which must hold: ability, weapon, status
// (what the hit or kill came from), label <word> (the ability's or status's
// labels), id <id> (that ability or status; a bare word is an id too).
// Conditions (`if`): target <status> (the one hit or killed has it -- had it,
// for a kill), self <status>, tag <tag> (on the carrier), health_below <percent>.
//
// tick_perks_and_statuses runs it all each frame: statuses (tick_statuses),
// ability zones, the carried perks' bonuses and moments, and every queued
// action (a chain of reactions travels a frame at a time).
// Pure ECS: checked by tools/gameplay_check.

#include "world.h"
#include "authorable_components.h"
#include "gameplay_ability_cast.h"
#include "gameplay_attributes.h"
#include "gameplay_damage.h"
#include "gameplay_events.h"
#include "gameplay_status.h"
#include "gameplay_tags.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace schizo::ecs {

struct PerkTrigger {
    std::string event;                    // hit | kill | cast | hurt | applied | triggered
    std::vector<std::string> filters;     // ability weapon status label:<w> id:<id>
    std::string if_kind, if_what;         // target|self <status>; tag <tag>; health_below <percent>
    float       if_value = 0.0f;
    float       chance = 1.0f, every = 0.0f;
    std::vector<TriggerAction> actions;
};
struct PerkDef {
    std::string id, name, about;
    std::vector<std::string> labels;
    std::vector<StatusMod> mods;
    std::vector<std::string> tags;
    std::vector<PerkTrigger> triggers;
    bool has_label(const std::string& l) const { return std::find(labels.begin(), labels.end(), l) != labels.end(); }
};

inline std::vector<PerkDef>& perk_def_registry() { static std::vector<PerkDef> r; return r; }
inline void register_perk_def(const PerkDef& d) {
    for (auto& e : perk_def_registry()) if (e.id == d.id) { e = d; return; }
    perk_def_registry().push_back(d);
}
inline const PerkDef* find_perk_def(const std::string& id) {
    for (const auto& e : perk_def_registry()) if (e.id == id) return &e;
    return nullptr;
}

// ---- data: `perk <id>` blocks -------------------------------------------------------------
inline std::vector<PerkDef> parse_perks_text(const std::string& text) {
    std::vector<PerkDef> out;
    PerkDef cur;
    bool have = false;
    auto flush = [&] { if (have && !cur.id.empty()) out.push_back(cur); cur = PerkDef{}; have = false; };
    auto rest_of = [](std::istringstream& ls) {
        std::string r;
        std::getline(ls, r);
        size_t a = 0, b = r.size();
        while (a < b && std::isspace(static_cast<unsigned char>(r[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(r[b - 1]))) --b;
        return r.substr(a, b - a);
    };
    auto words = [](std::istringstream& ls) {
        std::vector<std::string> w;
        for (std::string x; ls >> x;) { if (x[0] == '#') break; w.push_back(x); }
        return w;
    };
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#') continue;
        if (key == "perk") { flush(); ls >> cur.id; cur.name = cur.id; have = true; }
        else if (key == "item" || key == "weapon" || key == "ability" || key == "skill" || key == "status") { flush(); }
        else if (!have) { continue; }
        else if (key == "end") { flush(); }
        else if (key == "name") { cur.name = rest_of(ls); }
        else if (key == "about") { cur.about = rest_of(ls); }
        else if (key == "labels" || key == "label") { for (const auto& w : words(ls)) cur.labels.push_back(w); }
        else if (key == "mod") { StatusMod m; ls >> m.attribute >> m.amount; if (!m.attribute.empty()) cur.mods.push_back(m); }
        else if (key == "tag") { std::string t; ls >> t; if (!t.empty()) cur.tags.push_back(t); }
        else if (key == "when") {
            PerkTrigger t;
            const std::vector<std::string> w = words(ls);
            if (w.empty()) continue;
            t.event = w[0];
            for (size_t i = 1; i < w.size(); ++i) {
                if ((w[i] == "label" || w[i] == "id") && i + 1 < w.size()) { t.filters.push_back(w[i] + ":" + w[i + 1]); ++i; continue; }
                if (w[i] == "ability" || w[i] == "weapon" || w[i] == "status") { t.filters.push_back(w[i]); continue; }
                t.filters.push_back("id:" + w[i]);
            }
            cur.triggers.push_back(t);
        }
        else if (cur.triggers.empty()) { continue; }   // the rest belong to a moment
        else if (key == "if") {
            auto& t = cur.triggers.back();
            ls >> t.if_kind;
            if (t.if_kind == "health_below") ls >> t.if_value; else ls >> t.if_what;
        }
        else if (key == "chance") { ls >> cur.triggers.back().chance; }
        else if (key == "every") { ls >> cur.triggers.back().every; }
        else if (key == "do") { TriggerAction a; if (parse_trigger_action(ls, a)) cur.triggers.back().actions.push_back(a); }
    }
    flush();
    return out;
}

// ---- the perks a character carries ------------------------------------------------------------
struct Perks {
    std::vector<std::string> ids;
    // Runtime (not saved)
    std::vector<std::pair<std::string, float>> cooldowns;   // "<perk>#<moment>": seconds left
    std::vector<std::pair<std::string, float>> applied;     // the bonuses as applied (exact undo)
    std::vector<std::string> granted_tags;
    std::string synced;                                     // the perks the bonuses are for
};

inline bool has_perk(World& w, Entity e, const std::string& id) {
    const Perks* p = w.valid(e) ? w.try_get<Perks>(e) : nullptr;
    return p && std::find(p->ids.begin(), p->ids.end(), id) != p->ids.end();
}
/// Give `e` a perk (its bonuses and tags come with the next frame, or sync_perks).
/// False when there is no such perk or it has it already.
inline bool add_perk(World& w, Entity e, const std::string& id) {
    if (!w.valid(e) || !find_perk_def(id) || has_perk(w, e, id)) return false;
    if (!w.has<Perks>(e)) w.add<Perks>(e, Perks{});
    w.get<Perks>(e).ids.push_back(id);
    return true;
}
inline bool remove_perk(World& w, Entity e, const std::string& id) {
    Perks* p = w.valid(e) ? w.try_get<Perks>(e) : nullptr;
    if (!p) return false;
    const auto it = std::find(p->ids.begin(), p->ids.end(), id);
    if (it == p->ids.end()) return false;
    p->ids.erase(it);
    return true;
}

/// The carried perks' bonuses and tags on `e`, redone when the perks change.
inline void sync_perks(World& w, Entity e) {
    Perks* p = w.valid(e) ? w.try_get<Perks>(e) : nullptr;
    if (!p) return;
    std::string sig;
    for (const auto& id : p->ids) sig += id + "|";
    if (sig == p->synced) return;
    AttributeSet* attrs = w.try_get<AttributeSet>(e);
    if (attrs)
        for (const auto& [n, amt] : p->applied)
            if (Attribute* at = attrs->find(n)) at->current = std::max(at->min, std::min(at->max, at->current - amt));
    p->applied.clear();
    if (GameplayTags* g = w.try_get<GameplayTags>(e))
        for (const auto& t : p->granted_tags) g->remove_exact(t);
    p->granted_tags.clear();
    std::vector<std::string> tags;
    for (const auto& id : p->ids) {
        const PerkDef* d = find_perk_def(id);
        if (!d) continue;
        if (attrs)
            for (const auto& m : d->mods)
                if (Attribute* at = ensure_bonus_stat(*attrs, m.attribute)) {
                    const float before = at->current;
                    at->current = std::max(at->min, std::min(at->max, at->current + m.amount));
                    p->applied.push_back({m.attribute, at->current - before});
                }
        tags.insert(tags.end(), d->tags.begin(), d->tags.end());
    }
    p->synced = sig;
    if (!tags.empty()) {
        p->granted_tags = tags;
        if (!w.has<GameplayTags>(e)) w.add<GameplayTags>(e, GameplayTags{});
        for (const auto& t : tags) w.get<GameplayTags>(e).add(t);
    }
}

// ---- running actions -------------------------------------------------------------------------
/// What the frame's actions showed, for effects (read by the editor after the
/// frame's tick; cleared at the start of the next).
struct ActionVisuals {
    struct Burst { std::string ability; glm::vec3 at{0.0f}; };
    struct Chain { glm::vec3 from{0.0f}, to{0.0f}; std::string type; };
    std::vector<Burst> bursts;
    std::vector<Chain> chains;
};
inline ActionVisuals& action_visuals(World& w) { return w.registry().ctx().emplace<ActionVisuals>(); }

namespace perk_detail {
inline bool alive(World& w, Entity e) {
    if (!w.valid(e)) return false;
    if (const AttributeSet* a = w.try_get<AttributeSet>(e))
        if (const Attribute* h = a->find("Health")) return h->current > 0.0f;
    return w.has<AttributeSet>(e);
}
/// Those on `subject`'s side near `at` (alive, not the subject), nearest first.
inline std::vector<Entity> near_side(World& w, Entity subject, const glm::vec3& at, float radius) {
    const std::string side = ability_detail::team_of(w, subject);
    std::vector<std::pair<float, Entity>> found;
    w.each<Transform>([&](Entity e, Transform&) {
        if (e == subject || w.has<DamageReceiver>(e) || !w.has<AttributeSet>(e)) return;
        if (ability_detail::team_of(w, e) != side) return;
        const float d = glm::length(status_point(w, e) - at);
        if (d <= radius && alive(w, e)) found.push_back({d, e});
    });
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Entity> out;
    for (const auto& f : found) out.push_back(f.second);
    return out;
}
/// The abilities on `e`'s bar with this label (or all) cool down `percent` of their cooldown sooner.
inline void recharge(World& w, Entity e, const std::string& label, float percent) {
    AbilityBar* bar = w.try_get<AbilityBar>(e);
    if (!bar) return;
    const float cdr = std::clamp(bonus_stat(w.try_get<AttributeSet>(e), "CooldownReduction"), 0.0f, 75.0f);
    for (size_t i = 0; i < bar->slots.size() && i < bar->cooldown.size(); ++i) {
        const AbilityDef* d = find_ability_def(bar->slots[i]);
        if (!d || (label != "all" && !d->has_label(label))) continue;
        bar->cooldown[i] = std::max(0.0f, bar->cooldown[i] - d->cooldown * (1.0f - cdr / 100.0f) * percent / 100.0f);
    }
}
}  // namespace perk_detail

/// An ability going off around a point, as `by`'s: those it reaches there (an
/// area, whatever its own targeting), what it gives `by` (self effects and
/// statuses), and the zone it leaves.
inline int burst_ability(World& w, Entity by, const std::string& ability, const glm::vec3& at, GameplayEventBus* bus) {
    const AbilityDef* d = find_ability_def(ability);
    if (!d) return 0;
    const Entity caster = w.valid(by) ? by : null_entity;
    if (caster != null_entity) {
        for (const auto& e : d->self_effects) apply_effect(w, caster, to_gameplay_effect(e, d->name));
        for (const auto& st : d->self_statuses) apply_status(w, caster, st.id, st.stacks, caster, bus);
    }
    action_visuals(w).bursts.push_back({d->id, at});
    if (d->targeting == AbilityTargeting::Self) {
        if (caster != null_entity) ability_detail::leave_zone(w, caster, *d, at);
        return 0;
    }
    AbilityDef area = *d;
    area.targeting = AbilityTargeting::AreaAtAim;
    return ability_detail::reach_targets(w, caster, area, at, null_entity, bus);
}

/// Run one action (gameplay_status.h lists them).
inline void run_trigger_action(World& w, const PendingAction& p, GameplayEventBus* bus) {
    using K = TriggerAction::Kind;
    const TriggerAction& a = p.action;
    const Entity by = w.valid(p.by) ? p.by : null_entity;
    const int stacks = std::max(1, static_cast<int>(a.a));
    switch (a.kind) {
        case K::Burst: burst_ability(w, by, a.id, p.at, bus); break;
        case K::Apply: if (w.valid(p.subject)) apply_status(w, p.subject, a.id, stacks, by, bus); break;
        case K::Self: if (by != null_entity) apply_status(w, by, a.id, stacks, by, bus); break;
        case K::Spread:
            if (w.valid(p.subject))
                for (Entity e : perk_detail::near_side(w, p.subject, p.at, a.c)) apply_status(w, e, a.id, stacks, by, bus);
            break;
        case K::Chain: {
            if (!w.valid(p.subject)) break;
            const auto near = perk_detail::near_side(w, p.subject, p.at, a.c);
            glm::vec3 from = p.at;
            for (int i = 0; i < std::max(1, static_cast<int>(a.b)) && i < static_cast<int>(near.size()); ++i) {
                DamageInfo di{a.a, a.type, "Health", by};
                di.source = p.from;
                apply_damage(w, near[static_cast<size_t>(i)], di);
                const glm::vec3 to = status_point(w, near[static_cast<size_t>(i)]);
                action_visuals(w).chains.push_back({from, to, a.type});
                from = to;
            }
            break;
        }
        case K::Damage: {
            if (!w.valid(p.subject)) break;
            DamageInfo di{a.a, a.type, "Health", by};
            di.source = p.from;
            apply_damage(w, p.subject, di);
            break;
        }
        case K::Heal:
            if (by != null_entity)
                if (AttributeSet* at = w.try_get<AttributeSet>(by); at && at->get("Health", 0.0f) > 0.0f) at->adjust("Health", a.a);
            break;
        case K::Add:
            if (by != null_entity)
                if (AttributeSet* at = w.try_get<AttributeSet>(by); at && at->find(a.id)) at->adjust(a.id, a.per ? a.a * p.amount : a.a);
            break;
        case K::Recharge: if (by != null_entity) perk_detail::recharge(w, by, a.id, a.a); break;
        case K::Clear: {
            if (!w.valid(p.subject)) break;
            std::string id = a.id;
            if (id.empty() && p.from.rfind("status:", 0) == 0) id = p.from.substr(7);
            if (!id.empty()) clear_status(w, p.subject, id);
            break;
        }
    }
}

// ---- the frame --------------------------------------------------------------------------------
/// The events perks listen to, caught from the bus (kept in the world's context).
struct PerkEventTap {
    GameplayEventBus* bus = nullptr;
    std::vector<GameplayEventBus::SubId> subs;
    std::shared_ptr<std::vector<GameplayEvent>> events = std::make_shared<std::vector<GameplayEvent>>();
    uint64_t rng = 0x9E3779B97F4A7C15ULL;
};

namespace perk_detail {
inline float roll(PerkEventTap& t) {
    uint64_t z = (t.rng += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return static_cast<float>((z ^ (z >> 31)) >> 40) / 16777216.0f;
}
inline void tap(World& w, GameplayEventBus* bus) {
    PerkEventTap& t = w.registry().ctx().emplace<PerkEventTap>();
    if (!bus || t.bus == bus) return;
    if (t.bus) for (auto id : t.subs) t.bus->unsubscribe(id);
    t.subs.clear();
    t.bus = bus;
    // (The context may move its objects: the handlers hold the list itself.)
    std::shared_ptr<std::vector<GameplayEvent>> sink = t.events;
    for (const char* name : {"ability.hit", "weapon.hit", "died", "ability.fire", "status.applied", "status.triggered"})
        t.subs.push_back(bus->subscribe(name, [sink](const GameplayEvent& ev) {
            if (sink->size() < 4096) sink->push_back(ev);
        }));
}
/// Do a moment's filters hold for what it came from: `source` (an ability id, a
/// weapon's damage type, "status:<id>") and, for applied/triggered, the status.
inline bool filters_match(const PerkTrigger& t, const std::string& source, bool from_weapon, const std::string& status) {
    const AbilityDef* ad = from_weapon || !status.empty() ? nullptr : find_ability_def(source);
    const bool from_status = source.rfind("status:", 0) == 0;
    const std::string status_id = !status.empty() ? status : from_status ? source.substr(7) : std::string();
    const StatusDef* sd = status_id.empty() ? nullptr : find_status_def(status_id);
    for (const auto& f : t.filters) {
        if (f == "ability") { if (!ad) return false; }
        else if (f == "weapon") { if (!from_weapon) return false; }
        else if (f == "status") { if (!sd) return false; }
        else if (f.rfind("label:", 0) == 0) {
            const std::string l = f.substr(6);
            if (!((ad && ad->has_label(l)) || (sd && sd->has_label(l)))) return false;
        }
        else if (f.rfind("id:", 0) == 0) {
            const std::string id = f.substr(3);
            if (!((ad && ad->id == id) || (sd && sd->id == id))) return false;
        }
        else return false;
    }
    return true;
}
}  // namespace perk_detail

/// One frame: statuses, ability zones, the carried perks' bonuses and moments,
/// and the actions statuses and perks called for. Runs on the world that
/// decides damage (a guest online leaves it to the host).
inline void tick_perks_and_statuses(World& w, float dt, GameplayEventBus* bus) {
    using namespace perk_detail;
    if (const auto* policy = w.registry().ctx().find<DamageAuthority>(); policy && !policy->authoritative) return;
    action_visuals(w) = ActionVisuals{};
    tap(w, bus);
    tick_statuses(w, dt, bus);
    tick_ability_zones(w, dt, bus);

    std::vector<Entity> carriers;
    w.each<Perks>([&](Entity e, Perks&) { carriers.push_back(e); });
    for (Entity e : carriers) {
        if (!w.valid(e) || !w.has<Perks>(e)) continue;
        sync_perks(w, e);
        for (auto& c : w.get<Perks>(e).cooldowns) c.second = std::max(0.0f, c.second - dt);
    }

    // The moments perks listen for.
    PerkEventTap& tp = w.registry().ctx().emplace<PerkEventTap>();
    std::vector<GameplayEvent> events = std::move(*tp.events);
    tp.events->clear();
    std::vector<PendingAction> todo;
    for (const auto& ev : events) {
        std::string kind, source, status;
        Entity owner = null_entity, subject = null_entity;
        bool from_weapon = false;
        if (ev.name == "ability.hit" || ev.name == "weapon.hit") {
            kind = "hit"; owner = ev.instigator; subject = ev.target; source = ev.param; from_weapon = ev.name == "weapon.hit";
        } else if (ev.name == "died") {
            const DamageLog* log = w.valid(ev.target) ? w.try_get<DamageLog>(ev.target) : nullptr;
            if (!log) continue;
            kind = "kill"; owner = log->last_by; subject = ev.target; source = log->last_source;
            from_weapon = !source.empty() && !find_ability_def(source) && source.rfind("status:", 0) != 0 &&
                          source.rfind("perk:", 0) != 0;
        } else if (ev.name == "ability.fire") {
            kind = "cast"; owner = ev.instigator; subject = ev.instigator; source = ev.param;
        } else if (ev.name == "status.applied" || ev.name == "status.triggered") {
            kind = ev.name == "status.applied" ? "applied" : "triggered"; owner = ev.instigator; subject = ev.target; status = ev.param;
        } else continue;
        // Whose moment: the one who did it, and for a hit also the one hit (`hurt`).
        std::vector<std::pair<Entity, std::string>> listeners;
        if (w.valid(owner) && w.has<Perks>(owner)) listeners.push_back({owner, kind});
        if (kind == "hit" && w.valid(subject) && w.has<Perks>(subject) && subject != owner) listeners.push_back({subject, "hurt"});
        for (const auto& [who, moment] : listeners) {
            Perks& carried = w.get<Perks>(who);
            for (const auto& pid : std::vector<std::string>(carried.ids)) {
                const PerkDef* p = find_perk_def(pid);
                if (!p) continue;
                for (size_t ti = 0; ti < p->triggers.size(); ++ti) {
                    const PerkTrigger& t = p->triggers[ti];
                    if (t.event != moment) continue;
                    if (!filters_match(t, source, from_weapon, moment == "hurt" ? std::string() : status)) continue;
                    // The subject: the one hit or killed; for hurt and cast, the carrier.
                    const Entity target = moment == "hurt" ? owner : subject;
                    if (!t.if_kind.empty()) {
                        if (t.if_kind == "target") {
                            bool has = w.valid(target) && has_status(w, target, t.if_what);
                            if (!has && kind == "kill" && w.valid(target))
                                if (const DamageLog* log = w.try_get<DamageLog>(target))
                                    has = std::find(log->died_with.begin(), log->died_with.end(), t.if_what) != log->died_with.end();
                            if (!has) continue;
                        } else if (t.if_kind == "self") {
                            if (!has_status(w, who, t.if_what)) continue;
                        } else if (t.if_kind == "tag") {
                            const GameplayTags* g = w.try_get<GameplayTags>(who);
                            if (!g || !g->has(t.if_what)) continue;
                        } else if (t.if_kind == "health_below") {
                            const AttributeSet* a = w.try_get<AttributeSet>(who);
                            const Attribute* h = a ? a->find("Health") : nullptr;
                            if (!h || h->max <= 0.0f || h->current / h->max * 100.0f >= t.if_value) continue;
                        }
                    }
                    const std::string key = pid + "#" + std::to_string(ti);
                    float* cd = nullptr;
                    for (auto& c : carried.cooldowns) if (c.first == key) cd = &c.second;
                    if (cd && *cd > 0.0f) continue;
                    if (t.chance < 1.0f && roll(tp) >= t.chance) continue;
                    if (t.every > 0.0f) { if (cd) *cd = t.every; else carried.cooldowns.push_back({key, t.every}); }
                    const Entity subj = w.valid(target) ? target : who;
                    for (const auto& a : t.actions) todo.push_back({a, subj, who, status_point(w, subj), "perk:" + pid, ev.value});
                }
            }
        }
    }
    // What statuses and perks called for (a bound keeps a runaway chain from stalling a frame).
    std::vector<PendingAction> queued = std::move(pending_actions(w).list);
    pending_actions(w).list.clear();
    queued.insert(queued.end(), todo.begin(), todo.end());
    int budget = 512;
    for (const auto& p : queued) {
        if (--budget < 0) break;
        run_trigger_action(w, p, bus);
    }
}

// ---- the Perks component ------------------------------------------------------------------------
inline void serialize_perks(const void* obj, std::vector<uint8_t>& out) {
    detail::put_i32(out, 1);
    detail::put_str_vec(out, static_cast<const Perks*>(obj)->ids);
}
inline void deserialize_perks(void* obj, const uint8_t* data, size_t n) {
    auto& p = *static_cast<Perks*>(obj);
    p = Perks{};
    const uint8_t* c = data;
    const uint8_t* e = data + n;
    int32_t ver = 0;
    if (!detail::get_i32(c, e, ver) || ver < 1) return;
    detail::get_str_vec(c, e, p.ids);
}
inline void register_perk_components() {
    register_authorable(make_authorable_custom<Perks>("Perks", &serialize_perks, &deserialize_perks));
}

}  // namespace schizo::ecs
