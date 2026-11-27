#pragma once
// ============================================================================
// gameplay_profile.h -- what a player keeps from one scene of a game to the next
// (a game's modes: an expedition, a raid): their level, XP and skill points, the
// skills they unlocked, the abilities in their slots, their bag, what they wear
// and the guns they carry.
//
// A profile is a Prefab (prefab.h) of just those components -- the same text a
// save game uses. It is applied onto a player FRESH from its scene: the scene
// keeps its own design (base health, XP curve, growth per level, the skill
// tree, item definitions) and the profile's facts are rebuilt on top of it --
// the levels' growth added, each unlocked skill's grants and tags applied, what
// is worn re-equipped (its modifiers recomputed) -- so a change to the game's
// balance reaches a saved player, and a mode starts at full health. Apply it
// once, onto a fresh player (applying twice adds the bonuses twice).
//
// Pure ECS: checked by tools/gameplay_check.
// ============================================================================
#include "world.h"
#include "prefab.h"
#include "authorable_components.h"
#include "gameplay_ability_cast.h"
#include "gameplay_attributes.h"
#include "gameplay_inventory.h"
#include "gameplay_loadout.h"
#include "gameplay_perks.h"
#include "gameplay_progression.h"
#include "gameplay_skills.h"
#include "gameplay_weapons.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace schizo::ecs {

/// The authorable components a profile keeps (their registry names).
inline const std::vector<std::string>& profile_component_names() {
    static const std::vector<std::string> names = {"Progression", "Unlocked Skills", "Ability Bar", "Inventory",
                                                   "Equipment",   "Weapon Loadout",  "Weapon",    "Perks"};
    return names;
}

/// A player's profile: those components as they are now.
inline Prefab capture_profile(World& w, Entity e) {
    Prefab p;
    p.name = "Profile";
    const auto& keep = profile_component_names();
    for (const auto& ct : authorable_components()) {
        if (std::find(keep.begin(), keep.end(), std::string(ct.name)) == keep.end()) continue;
        if (!ct.has(w, e)) continue;
        if (void* c = ct.get(w, e)) p.components.emplace_back(ct.name, serialize_authorable(ct, c));
    }
    return p;
}

namespace profile_detail {
inline const std::vector<uint8_t>* bytes_of(const Prefab& p, const char* name) {
    for (const auto& [n, b] : p.components) if (n == name) return &b;
    return nullptr;
}
/// Deserialize a profile's component into a free object of its type (not onto an entity).
template <class T>
inline bool read(const Prefab& p, const char* name, T& out) {
    const std::vector<uint8_t>* b = bytes_of(p, name);
    const AuthorableComponent* ct = find_authorable(name);
    if (!b || !ct) return false;
    deserialize_authorable(*ct, &out, b->data(), b->size());
    return true;
}
/// Put a profile's component on the entity whole (adding it if missing).
inline void replace(World& w, Entity e, const Prefab& p, const char* name) {
    const std::vector<uint8_t>* b = bytes_of(p, name);
    const AuthorableComponent* ct = find_authorable(name);
    if (!b || !ct) return;
    ct->add(w, e);
    if (void* c = ct->get(w, e)) deserialize_authorable(*ct, c, b->data(), b->size());
}
}  // namespace profile_detail

/// Onto a player fresh from its scene (see the top of this file).
inline void apply_profile(World& w, Entity e, const Prefab& p) {
    namespace pd = profile_detail;
    if (!w.valid(e) || p.components.empty()) return;
    AttributeSet* attrs = w.try_get<AttributeSet>(e);
    // Levels: the scene's own growth per level for each level past the scene's start.
    if (Progression* cur = w.try_get<Progression>(e)) {
        Progression saved;
        if (pd::read(p, "Progression", saved)) {
            const int gained = saved.level - cur->level;
            if (gained > 0 && attrs)
                for (const auto& g : cur->per_level_growth)
                    if (Attribute* a = attrs->find(g.attribute)) {
                        const float add = g.per_level * static_cast<float>(gained);
                        a->base += add;
                        a->max += add;
                        a->current += add;
                    }
            cur->level = std::max(1, saved.level);
            cur->xp = std::max(0.0f, saved.xp);
            cur->skill_points = std::max(0, saved.skill_points);
        }
    }
    // Skills: each one the scene's tree still has, its grants and tags (its points were
    // spent already: the saved skill points say what is left).
    if (SkillTree* tree = w.try_get<SkillTree>(e)) {
        UnlockedSkills saved;
        if (pd::read(p, "Unlocked Skills", saved)) {
            if (!w.has<UnlockedSkills>(e)) w.add<UnlockedSkills>(e, UnlockedSkills{});
            UnlockedSkills& un = w.get<UnlockedSkills>(e);
            for (const auto& id : saved.unlocked) {
                if (std::find(un.unlocked.begin(), un.unlocked.end(), id) != un.unlocked.end()) continue;
                const SkillNode* n = find_node(*tree, id);
                if (!n) continue;   // (gone from the game's tree)
                if (attrs) apply_grants(*attrs, n->grants, +1.0f);
                if (!n->granted_tags.empty()) {
                    if (!w.has<GameplayTags>(e)) w.add<GameplayTags>(e, GameplayTags{});
                    for (const auto& t : n->granted_tags) w.get<GameplayTags>(e).add(t);
                }
                un.unlocked.push_back(id);
            }
        }
    }
    // What it wears: re-equipped (the scene's starting gear reverted, the saved gear's
    // modifiers applied by the item definitions as they are now).
    {
        Equipment saved;
        if (pd::read(p, "Equipment", saved)) {
            if (!w.has<Equipment>(e)) w.add<Equipment>(e, Equipment{});
            w.get<Equipment>(e).slots = saved.slots;
            recompute_equipment(w, e);
        }
    }
    // The bag, the abilities in their slots, the guns carried and the one in hand.
    pd::replace(w, e, p, "Inventory");
    pd::replace(w, e, p, "Ability Bar");
    pd::replace(w, e, p, "Weapon Loadout");
    pd::replace(w, e, p, "Weapon");
    // The perks carried (their bonuses and tags rebuilt; one the game no longer has goes).
    pd::replace(w, e, p, "Perks");
    if (Perks* pk = w.try_get<Perks>(e)) {
        pk->ids.erase(std::remove_if(pk->ids.begin(), pk->ids.end(), [](const std::string& id) { return !find_perk_def(id); }),
                      pk->ids.end());
        sync_perks(w, e);
    }
    if (WeaponState* ws = w.try_get<WeaponState>(e)) {
        ws->cooldown = 0.0f;
        ws->reloading = 0.0f;
        if (Inventory* inv = w.try_get<Inventory>(e)) sync_ammo_with_inventory(*ws, *inv);
    }
    if (w.has<DerivedStats>(e)) recompute_derived(w, e);
    // A new mode starts at full health.
    if (attrs)
        if (Attribute* h = attrs->find("Health")) h->current = h->max;
}

}  // namespace schizo::ecs
