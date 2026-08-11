#pragma once

// ============================================================================
// G6 · Quests — event-driven objectives, stages, rewards, journal.
// ============================================================================
//
// A QuestDef is authored data: ordered stages, each with objectives that advance
// when a matching gameplay EVENT fires (e.g. "combat.hit" of a tagged enemy,
// "pickup" of an item, "harvest.done"). QuestLog tracks per-quest progress on an
// entity. advance_quests() routes each bus event to progress; finishing the last
// stage grants the reward (items / XP / currency / tags) and fires quest.complete.
// So quests are pure data over G0 events + G3/G4/G5 — no bespoke quest scripting.

#include "world.h"
#include "gameplay_events.h"
#include "gameplay_tags.h"
#include "gameplay_items.h"
#include "gameplay_inventory.h"
#include "gameplay_progression.h"   // grant_xp
#include "gameplay_economy.h"       // wallet_add
#include "gameplay_crafting.h"      // ItemStack
#include "authorable_components.h"
#include "component_serialize.h"

#include <string>
#include <vector>
#include <cstdint>

namespace schizo::ecs {

struct QuestObjective {
    std::string description;
    std::string event;               // gameplay event that advances it
    std::string param;               // optional param filter ("" = any)
    int         required = 1;
    bool        owner_only = true;   // only count events instigated by the quest owner
};
struct QuestStage  { std::string name; std::vector<QuestObjective> objectives; };
struct QuestReward {
    std::vector<ItemStack>   items;
    float xp = 0.0f, currency = 0.0f;
    std::vector<std::string> tags;
};
struct QuestDef {
    std::string id, name, description;
    std::vector<QuestStage> stages;
    QuestReward reward;
};

inline std::vector<QuestDef>& quest_registry() { static std::vector<QuestDef> r; return r; }
inline void register_quest(const QuestDef& d) {
    for (auto& e : quest_registry()) if (e.id == d.id) { e = d; return; }
    quest_registry().push_back(d);
}
inline const QuestDef* find_quest(const std::string& id) {
    for (const auto& e : quest_registry()) if (e.id == id) return &e;
    return nullptr;
}

struct QuestProgress {
    std::string quest_id;
    int stage = 0;
    std::vector<int> counts;         // per-objective in the current stage
    bool complete = false;
};
struct QuestLog { std::vector<QuestProgress> active; std::vector<std::string> completed; };

inline bool has_quest(const QuestLog& log, const std::string& id) {
    for (const auto& q : log.active) if (q.quest_id == id) return true;
    for (const auto& c : log.completed) if (c == id) return true;
    return false;
}
inline const QuestProgress* find_progress(const QuestLog& log, const std::string& id) {
    for (const auto& q : log.active) if (q.quest_id == id) return &q;
    return nullptr;
}

inline void start_quest(World& w, Entity e, const std::string& quest_id, GameplayEventBus* bus = nullptr) {
    const QuestDef* d = find_quest(quest_id);
    if (!d || d->stages.empty()) return;
    if (!w.has<QuestLog>(e)) w.add<QuestLog>(e, QuestLog{});
    QuestLog& log = w.get<QuestLog>(e);
    if (has_quest(log, quest_id)) return;
    QuestProgress p; p.quest_id = quest_id; p.stage = 0;
    p.counts.assign(d->stages[0].objectives.size(), 0);
    log.active.push_back(std::move(p));
    if (bus) { GameplayEvent ev; ev.name = "quest.start"; ev.target = e; ev.param = quest_id; bus->publish(ev); }
}

inline void grant_quest_reward(World& w, Entity e, const QuestReward& r) {
    if (!r.items.empty()) {
        if (!w.has<Inventory>(e)) w.add<Inventory>(e, Inventory{});
        for (const auto& it : r.items) inventory_add(w.get<Inventory>(e), ItemInstance{it.item_id, it.quantity, 0, {}});
    }
    if (r.xp > 0.0f)       grant_xp(w, e, r.xp);
    if (r.currency > 0.0f) wallet_add(w, e, r.currency);
    if (!r.tags.empty()) {
        if (!w.has<GameplayTags>(e)) w.add<GameplayTags>(e, GameplayTags{});
        auto& g = w.get<GameplayTags>(e);
        for (const auto& t : r.tags) g.add(t);
    }
}

// Route one gameplay event into quest progress on every QuestLog.
inline void advance_quests(World& w, const GameplayEvent& ev, GameplayEventBus* bus = nullptr) {
    w.each<QuestLog>([&](Entity e, QuestLog& log) {
        for (size_t qi = 0; qi < log.active.size();) {
            QuestProgress& p = log.active[qi];
            const QuestDef* d = find_quest(p.quest_id);
            if (!d || p.stage >= static_cast<int>(d->stages.size())) { ++qi; continue; }
            const QuestStage& st = d->stages[p.stage];

            bool advanced = false;
            for (size_t oi = 0; oi < st.objectives.size() && oi < p.counts.size(); ++oi) {
                const QuestObjective& ob = st.objectives[oi];
                if (ob.event != ev.name) continue;
                if (!ob.param.empty() && ob.param != ev.param) continue;
                if (ob.owner_only && ev.instigator != e) continue;
                if (p.counts[oi] < ob.required) { ++p.counts[oi]; advanced = true; }
            }

            bool stage_done = true;
            for (size_t oi = 0; oi < st.objectives.size(); ++oi)
                if (p.counts[oi] < st.objectives[oi].required) { stage_done = false; break; }

            if (stage_done && advanced) {
                if (p.stage + 1 < static_cast<int>(d->stages.size())) {
                    ++p.stage;
                    p.counts.assign(d->stages[p.stage].objectives.size(), 0);
                    if (bus) { GameplayEvent e2; e2.name = "quest.stage"; e2.target = e; e2.param = p.quest_id;
                               e2.value = static_cast<float>(p.stage); bus->publish(e2); }
                } else {
                    grant_quest_reward(w, e, d->reward);
                    log.completed.push_back(p.quest_id);
                    if (bus) { GameplayEvent e2; e2.name = "quest.complete"; e2.target = e; e2.param = p.quest_id; bus->publish(e2); }
                    log.active.erase(log.active.begin() + qi);
                    continue;   // don't ++qi (erased)
                }
            }
            ++qi;
        }
    });
}

// ---------------- custom serialization (persist the journal for G9) ----------------
inline void serialize_quest_log(const void* obj, std::vector<uint8_t>& out) {
    const auto& log = *static_cast<const QuestLog*>(obj);
    uint32_t na = static_cast<uint32_t>(log.active.size()); detail::put_bytes(out, &na, 4);
    for (const auto& p : log.active) {
        detail::put_str(out, p.quest_id);
        detail::put_i32(out, p.stage);
        uint32_t nc = static_cast<uint32_t>(p.counts.size()); detail::put_bytes(out, &nc, 4);
        for (int c : p.counts) detail::put_i32(out, c);
        const uint8_t cm = p.complete ? 1 : 0; detail::put_bytes(out, &cm, 1);
    }
    detail::put_str_vec(out, log.completed);
}
inline void deserialize_quest_log(void* obj, const uint8_t* data, size_t n) {
    auto& log = *static_cast<QuestLog*>(obj); log.active.clear(); log.completed.clear();
    const uint8_t* p = data; const uint8_t* e = data + n;
    uint32_t na = 0; if (!detail::get_bytes(p, e, &na, 4)) return;
    for (uint32_t i = 0; i < na; ++i) {
        QuestProgress qp;
        if (!detail::get_str(p, e, qp.quest_id)) return;
        if (!detail::get_i32(p, e, qp.stage))    return;
        uint32_t nc = 0; if (!detail::get_bytes(p, e, &nc, 4)) return;
        for (uint32_t j = 0; j < nc; ++j) { int32_t c = 0; if (!detail::get_i32(p, e, c)) return; qp.counts.push_back(c); }
        uint8_t cm = 0; if (!detail::get_bytes(p, e, &cm, 1)) return; qp.complete = cm != 0;
        log.active.push_back(std::move(qp));
    }
    detail::get_str_vec(p, e, log.completed);
}
inline void register_quest_component() {
    register_authorable(make_authorable_custom<QuestLog>("Quest Log", &serialize_quest_log, &deserialize_quest_log));
}

}  // namespace schizo::ecs
