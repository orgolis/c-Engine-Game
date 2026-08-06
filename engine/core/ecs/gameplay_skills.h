#pragma once

// ============================================================================
// G3 · Skill / talent trees + classes & respec.
// ============================================================================
//
// A SkillTree is authored data: nodes with a point cost, prerequisite nodes, and
// what unlocking grants — permanent attribute bonuses and/or GameplayTags (which
// gate abilities, unlock features, etc.). UnlockedSkills tracks what an entity has
// taken. unlock_skill() spends Progression points; respec() reverts every unlocked
// node exactly (grants applied as Add, so revert is precise) and refunds points.
//
// A "class / build" is just a prefab: a starting AttributeSet + a SkillTree +
// starting abilities/tags — no dedicated component needed.

#include "world.h"
#include "gameplay_attributes.h"
#include "gameplay_tags.h"
#include "gameplay_events.h"
#include "gameplay_progression.h"   // Progression (skill points)
#include "authorable_components.h"
#include "component_serialize.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace schizo::ecs {

struct StatGrant { std::string attribute; float amount = 0.0f; };
struct SkillNode {
    std::string id;
    std::string name;
    int cost = 1;
    std::vector<std::string> prerequisites;   // node ids that must be unlocked first
    std::vector<StatGrant>   grants;          // permanent attribute bonuses
    std::vector<std::string> granted_tags;    // e.g. an ability-unlock tag
};
struct SkillTree     { std::vector<SkillNode> nodes; };
struct UnlockedSkills { std::vector<std::string> unlocked; };  // node ids

inline const SkillNode* find_node(const SkillTree& t, const std::string& id) {
    for (const auto& n : t.nodes) if (n.id == id) return &n;
    return nullptr;
}
inline bool skill_unlocked(const UnlockedSkills& u, const std::string& id) {
    for (const auto& s : u.unlocked) if (s == id) return true;
    return false;
}

inline void apply_grants(AttributeSet& a, const std::vector<StatGrant>& g, float sign) {
    for (const auto& s : g)
        if (Attribute* at = a.find(s.attribute)) {
            at->base    += s.amount * sign;
            at->max     += s.amount * sign;
            at->current += s.amount * sign;
            at->current = std::max(at->min, std::min(at->max, at->current));
        }
}

inline bool can_unlock(World& w, Entity e, const std::string& node_id) {
    const SkillTree*     tree = w.try_get<SkillTree>(e);
    const UnlockedSkills* un  = w.try_get<UnlockedSkills>(e);
    if (!tree || !un) return false;
    const SkillNode* n = find_node(*tree, node_id);
    if (!n || skill_unlocked(*un, node_id)) return false;
    if (const Progression* prog = w.try_get<Progression>(e))
        if (prog->skill_points < n->cost) return false;
    for (const auto& pre : n->prerequisites)
        if (!skill_unlocked(*un, pre)) return false;
    return true;
}

inline bool unlock_skill(World& w, Entity e, const std::string& node_id, GameplayEventBus* bus = nullptr) {
    if (!can_unlock(w, e, node_id)) return false;
    SkillTree&     tree = w.get<SkillTree>(e);
    UnlockedSkills& un  = w.get<UnlockedSkills>(e);
    const SkillNode* n = find_node(tree, node_id);
    if (Progression* prog = w.try_get<Progression>(e)) prog->skill_points -= n->cost;
    if (AttributeSet* a = w.try_get<AttributeSet>(e)) apply_grants(*a, n->grants, +1.0f);
    if (!n->granted_tags.empty()) {
        if (!w.has<GameplayTags>(e)) w.add<GameplayTags>(e, GameplayTags{});
        auto& g = w.get<GameplayTags>(e);
        for (const auto& t : n->granted_tags) g.add(t);
    }
    un.unlocked.push_back(node_id);
    if (w.has<DerivedStats>(e)) recompute_derived(w, e);
    if (bus) { GameplayEvent ev; ev.name = "skill.unlocked"; ev.target = e; ev.param = node_id; bus->publish(ev); }
    return true;
}

// Refund every unlocked node (revert grants + tags, refund points). Returns points refunded.
inline int respec(World& w, Entity e, GameplayEventBus* bus = nullptr) {
    SkillTree*     tree = w.try_get<SkillTree>(e);
    UnlockedSkills* un  = w.try_get<UnlockedSkills>(e);
    if (!tree || !un) return 0;
    AttributeSet* attrs = w.try_get<AttributeSet>(e);
    GameplayTags* tags  = w.try_get<GameplayTags>(e);
    Progression*  prog  = w.try_get<Progression>(e);
    int refunded = 0;
    for (const auto& id : un->unlocked) {
        const SkillNode* n = find_node(*tree, id);
        if (!n) continue;
        if (attrs) apply_grants(*attrs, n->grants, -1.0f);
        if (tags)  for (const auto& t : n->granted_tags) tags->remove_exact(t);
        if (prog)  prog->skill_points += n->cost;
        refunded += n->cost;
    }
    un->unlocked.clear();
    if (w.has<DerivedStats>(e)) recompute_derived(w, e);
    if (bus) { GameplayEvent ev; ev.name = "skills.respec"; ev.target = e; bus->publish(ev); }
    return refunded;
}

// ---------------- custom serialization ----------------
inline void serialize_skill_tree(const void* obj, std::vector<uint8_t>& out) {
    const auto& t = *static_cast<const SkillTree*>(obj);
    uint32_t nn = static_cast<uint32_t>(t.nodes.size()); detail::put_bytes(out, &nn, 4);
    for (const auto& n : t.nodes) {
        detail::put_str(out, n.id);
        detail::put_str(out, n.name);
        detail::put_i32(out, n.cost);
        detail::put_str_vec(out, n.prerequisites);
        uint32_t ng = static_cast<uint32_t>(n.grants.size()); detail::put_bytes(out, &ng, 4);
        for (const auto& g : n.grants) { detail::put_str(out, g.attribute); detail::put_f32(out, g.amount); }
        detail::put_str_vec(out, n.granted_tags);
    }
}
inline void deserialize_skill_tree(void* obj, const uint8_t* data, size_t n) {
    auto& t = *static_cast<SkillTree*>(obj); t.nodes.clear();
    const uint8_t* p = data; const uint8_t* e = data + n;
    uint32_t nn = 0; if (!detail::get_bytes(p, e, &nn, 4)) return;
    for (uint32_t i = 0; i < nn; ++i) {
        SkillNode nd;
        if (!detail::get_str(p, e, nd.id))               return;
        if (!detail::get_str(p, e, nd.name))             return;
        if (!detail::get_i32(p, e, nd.cost))             return;
        if (!detail::get_str_vec(p, e, nd.prerequisites)) return;
        uint32_t ng = 0; if (!detail::get_bytes(p, e, &ng, 4)) return;
        for (uint32_t j = 0; j < ng; ++j) {
            StatGrant g;
            if (!detail::get_str(p, e, g.attribute)) return;
            if (!detail::get_f32(p, e, g.amount))    return;
            nd.grants.push_back(std::move(g));
        }
        if (!detail::get_str_vec(p, e, nd.granted_tags)) return;
        t.nodes.push_back(std::move(nd));
    }
}
inline void serialize_unlocked_skills(const void* obj, std::vector<uint8_t>& out) {
    detail::put_str_vec(out, static_cast<const UnlockedSkills*>(obj)->unlocked);
}
inline void deserialize_unlocked_skills(void* obj, const uint8_t* data, size_t n) {
    const uint8_t* p = data; const uint8_t* e = data + n;
    detail::get_str_vec(p, e, static_cast<UnlockedSkills*>(obj)->unlocked);
}

inline void register_skill_components() {
    register_authorable(make_authorable_custom<SkillTree>("Skill Tree", &serialize_skill_tree, &deserialize_skill_tree));
    register_authorable(make_authorable_custom<UnlockedSkills>("Unlocked Skills", &serialize_unlocked_skills, &deserialize_unlocked_skills));
}

}  // namespace schizo::ecs
