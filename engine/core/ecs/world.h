#pragma once

// ====================
// ECS World  (Pillar D1 / Master Plan Stage 1.1)
// ====================
//
// A thin, data-oriented world over an EnTT registry — the unified data
// backbone the plan calls for (replacing the OOP GameObject/Component scene
// over time). Entities are lightweight handles (index + generation, recycled
// safely by EnTT); components are plain structs registered with reflection
// (Stage 0.4) so the editor/serializer see them automatically.

#include <entt/entt.hpp>
#include <utility>

namespace schizo::ecs {

using Entity = entt::entity;
inline constexpr Entity null_entity = entt::null;

class World {
public:
    Entity create()                 { return registry_.create(); }
    void   destroy(Entity e)        { registry_.destroy(e); }
    bool   valid(Entity e) const    { return registry_.valid(e); }

    template <typename T, typename... Args>
    T& add(Entity e, Args&&... args) {
        return registry_.emplace_or_replace<T>(e, std::forward<Args>(args)...);
    }
    template <typename T> T&       get(Entity e)       { return registry_.get<T>(e); }
    template <typename T> const T& get(Entity e) const { return registry_.get<T>(e); }
    template <typename T> T*       try_get(Entity e)   { return registry_.try_get<T>(e); }
    template <typename T> bool     has(Entity e) const { return registry_.all_of<T>(e); }
    template <typename T> void     remove(Entity e)    { registry_.remove<T>(e); }

    // Query a set of components. Returns an EnTT view (iterable over entities
    // that have all of <Ts...>).
    template <typename... Ts> auto view() { return registry_.view<Ts...>(); }

    // Serial iteration: fn(Entity, Ts&...).
    template <typename... Ts, typename Fn> void each(Fn&& fn) {
        registry_.view<Ts...>().each(std::forward<Fn>(fn));
    }
    // Count entities matching <Ts...> (O(n); not a hot path).
    template <typename... Ts> size_t count() {
        size_t n = 0;
        for (auto e : registry_.view<Ts...>()) { (void)e; ++n; }
        return n;
    }

    void                  clear()          { registry_.clear(); }
    entt::registry&       registry()       { return registry_; }
    const entt::registry& registry() const { return registry_; }

private:
    entt::registry registry_;
};

}  // namespace schizo::ecs
