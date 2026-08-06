#pragma once

// ============================================================================
// G0 · Gameplay Tags — hierarchical, user-defined state/condition labels.
// ============================================================================
//
// A game defines ANY tags as data ("state.stunned", "element.fire",
// "ability.dash", "team.red", "immune.poison"). Every gameplay system queries by
// tag, so conditions / immunities / requirements / ability gating stay open and
// data-driven rather than hardcoded enums.
//
// Matching is HIERARCHICAL (GAS semantics): owning "state.stunned.hard" satisfies
// a query for "state.stunned" (a child matches the parent query).
//
// Non-POD component -> registered with custom serialization; the editor draws it
// with a custom inspector.

#include "world.h"
#include "authorable_components.h"

#include <cstdint>
#include <string>
#include <vector>

namespace schizo::ecs {

struct GameplayTags {
    std::vector<std::string> tags;   // owned tags on this entity

    bool has_exact(const std::string& t) const {
        for (const auto& x : tags) if (x == t) return true;
        return false;
    }
    // True if the entity owns `query` OR a child of it (owned "a.b.c" matches "a.b").
    bool has(const std::string& query) const {
        for (const auto& x : tags)
            if (x == query ||
                (x.size() > query.size() &&
                 x.compare(0, query.size(), query) == 0 &&
                 x[query.size()] == '.'))
                return true;
        return false;
    }
    void add(const std::string& t) { if (!t.empty() && !has_exact(t)) tags.push_back(t); }
    void remove_exact(const std::string& t) {
        for (size_t i = 0; i < tags.size(); ++i)
            if (tags[i] == t) { tags.erase(tags.begin() + i); return; }
    }
};

inline void serialize_gameplay_tags(const void* obj, std::vector<uint8_t>& out) {
    const GameplayTags& g = *static_cast<const GameplayTags*>(obj);
    const uint32_t count = static_cast<uint32_t>(g.tags.size());
    detail::put_bytes(out, &count, 4);
    for (const auto& t : g.tags) {
        const uint32_t len = static_cast<uint32_t>(t.size());
        detail::put_bytes(out, &len, 4);
        detail::put_bytes(out, t.data(), len);
    }
}

inline void deserialize_gameplay_tags(void* obj, const uint8_t* data, size_t n) {
    GameplayTags& g = *static_cast<GameplayTags*>(obj);
    g.tags.clear();
    const uint8_t* p = data; const uint8_t* e = data + n;
    uint32_t count = 0;
    if (!detail::get_bytes(p, e, &count, 4)) return;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t len = 0;
        if (!detail::get_bytes(p, e, &len, 4) || p + len > e) return;
        g.tags.emplace_back(reinterpret_cast<const char*>(p), len); p += len;
    }
}

inline void register_tags_component() {
    register_authorable(make_authorable_custom<GameplayTags>(
        "Tags", &serialize_gameplay_tags, &deserialize_gameplay_tags));
}

}  // namespace schizo::ecs
