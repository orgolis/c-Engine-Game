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
#include "component_serialize.h"   // serialize_component/deserialize_component (dispatch)
#include "reflection/reflection.h"

#include <vector>

namespace schizo::ecs {

struct AuthorableComponent {
    const char*                   name;   // display name + stable save key
    const gws::reflect::TypeInfo* type;   // reflected fields (POD path); null for custom
    bool  (*has)   (World&, Entity);
    void  (*add)   (World&, Entity);      // default-construct + emplace
    void  (*remove)(World&, Entity);
    void* (*get)   (World&, Entity);      // -> the live component, or nullptr
    // Custom serialization for NON-POD / data-driven components (e.g. AttributeSet
    // with a dynamic list). null -> the generic reflection path over `type` is used.
    void (*serialize)  (const void*, std::vector<uint8_t>&) = nullptr;
    void (*deserialize)(void*, const uint8_t*, size_t)      = nullptr;
};

// Build an entry from a POD component type. Captureless lambdas decay to function
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

// Build an entry for a NON-POD / data-driven component with custom serialization
// (the inspector draws it via an editor-side custom drawer keyed by name).
template <typename T>
inline AuthorableComponent make_authorable_custom(
        const char* name,
        void (*ser)(const void*, std::vector<uint8_t>&),
        void (*deser)(void*, const uint8_t*, size_t)) {
    return AuthorableComponent{
        name,
        nullptr,                                  // no reflected POD layout
        [](World& w, Entity e) { return w.has<T>(e); },
        [](World& w, Entity e) { w.add<T>(e, T{}); },
        [](World& w, Entity e) { if (w.has<T>(e)) w.remove<T>(e); },
        [](World& w, Entity e) -> void* { return static_cast<void*>(w.try_get<T>(e)); },
        ser, deser,
    };
}

// The gameplay components the editor can author. An extensible registry so each
// gameplay MODULE registers its own components (fits the modular engine) — the
// inspector/save/prefabs pick them up automatically. Seeded with the built-ins.
inline std::vector<AuthorableComponent>& authorable_registry() {
    static std::vector<AuthorableComponent> list = {
        make_authorable<Health>("Health"),
        make_authorable<AbilityState>("Ability State"),
    };
    return list;
}
inline const std::vector<AuthorableComponent>& authorable_components() { return authorable_registry(); }

// Register a module's authorable component (idempotent by name). Call at startup.
inline void register_authorable(const AuthorableComponent& c) {
    for (const auto& e : authorable_registry())
        if (std::string_view(e.name) == c.name) return;   // already present
    authorable_registry().push_back(c);
}

// Find an authorable entry by its save-key/name (used by the serializer/prefabs).
inline const AuthorableComponent* find_authorable(const char* name) {
    for (const auto& c : authorable_components())
        if (std::string_view(c.name) == name) return &c;
    return nullptr;
}

// Serialize/deserialize a component through its custom hook when set, else the
// generic reflection path. The single entry point used by save + prefabs so both
// POD and data-driven (AttributeSet, …) components persist uniformly.
inline std::vector<uint8_t> serialize_authorable(const AuthorableComponent& ct, const void* comp) {
    if (ct.serialize) { std::vector<uint8_t> b; ct.serialize(comp, b); return b; }
    if (ct.type)      return serialize_component(comp, *ct.type);
    return {};
}
inline void deserialize_authorable(const AuthorableComponent& ct, void* comp,
                                   const uint8_t* data, size_t n) {
    if (ct.deserialize) { ct.deserialize(comp, data, n); return; }
    if (ct.type)        deserialize_component(comp, *ct.type, data, n);
}

}  // namespace schizo::ecs
