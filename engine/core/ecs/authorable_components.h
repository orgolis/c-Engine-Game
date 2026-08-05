#pragma once

// ============================================================================
// Authorable component registry  (Master Plan Stage 1.4 — ECS authoring)
// ============================================================================
//
// The list of ECS components the editor can author GENERICALLY. Each entry pairs
// a component's reflection TypeInfo (its fields) with type-erased ECS ops
// (has/add/remove/get), so the inspector, save/load, and prefab code all work
// over ANY registered component with zero per-component boilerplate.
//
// To make a new gameplay component editable + saveable + prefab-able, just add
// one `make_authorable<T>("Name")` line below — the generic inspector and
// serializer pick it up automatically.
//
// Transform / MeshRenderer stay OOP-authored for now (the pragmatic migration:
// ECS is authoritative for GAMEPLAY first), so they are intentionally NOT here.

#include "world.h"
#include "components.h"
#include "reflection/reflection.h"

#include <vector>

namespace schizo::ecs {

struct AuthorableComponent {
    const char*                   name;   // display name + stable save key
    const gws::reflect::TypeInfo* type;   // fields, for the inspector + serializer
    bool  (*has)   (World&, Entity);
    void  (*add)   (World&, Entity);      // default-construct + emplace
    void  (*remove)(World&, Entity);
    void* (*get)   (World&, Entity);      // -> the live component, or nullptr
};

// Build an entry from a component type. Captureless lambdas decay to function
// pointers; reflect<T>() registers the type so it is findable by name.
template <typename T>
inline AuthorableComponent make_authorable(const char* name) {
    return AuthorableComponent{
        name,
        gws::reflect::reflect<T>(),
        [](World& w, Entity e) { return w.has<T>(e); },
        [](World& w, Entity e) { w.add<T>(e, T{}); },
        [](World& w, Entity e) { if (w.has<T>(e)) w.remove<T>(e); },
        [](World& w, Entity e) -> void* { return static_cast<void*>(w.try_get<T>(e)); },
    };
}

// The gameplay components the editor can author. Grows as G0+ (attributes,
// abilities, inventory, …) lands.
inline const std::vector<AuthorableComponent>& authorable_components() {
    static const std::vector<AuthorableComponent> list = {
        make_authorable<Health>("Health"),
        make_authorable<AbilityState>("Ability State"),
    };
    return list;
}

// Find an authorable entry by its save-key/name (used by the serializer/prefabs).
inline const AuthorableComponent* find_authorable(const char* name) {
    for (const auto& c : authorable_components())
        if (std::string_view(c.name) == name) return &c;
    return nullptr;
}

}  // namespace schizo::ecs
