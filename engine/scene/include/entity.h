#pragma once

#include "transform.h"
#include "mesh_component.h"
#include "skinned_mesh_component.h"
#include "particle_emitter_component.h"
#include "npc_agent_component.h"
#include <memory>
#include <vector>
#include <string>
#include <cstdint>
#include <typeinfo>
#include <type_traits>
#include <cstddef>

namespace schizo::scene {

// Forward declarations
class Scene;
class Component;

/// Which per-frame hooks a component actually overrides. Most override none:
/// a mesh or a collider is data, and calling three empty virtual functions on
/// it several times a frame is pure cost. The scene uses these bits to visit
/// only the entities that have something to run -- see Scene::Update.
enum ComponentHook : uint8_t {
    kHookFixedUpdate = 1u << 0,
    kHookUpdate      = 1u << 1,
    kHookLateUpdate  = 1u << 2,
    kHookAll         = kHookFixedUpdate | kHookUpdate | kHookLateUpdate,
};

namespace detail {
/// Bumped whenever any entity's component set changes, so a scene can tell
/// that its cached per-hook entity lists are stale without being told by each
/// entity (an entity does not know its scene). Defined in scene.cpp.
uint64_t component_set_generation();
void     bump_component_set_generation();
}  // namespace detail

// ============================================================================
// Component Base Class
// ============================================================================

/**
 * @class Component
 * @brief Base class for all entity components
 */
class Component {
public:
    virtual ~Component() = default;
    
    /**
     * @brief Called when component is attached to entity
     */
    virtual void OnAttach() {}
    
    /**
     * @brief Called when component is detached from entity
     */
    virtual void OnDetach() {}
    
    /**
     * @brief Called when entity becomes active
     */
    virtual void OnEnable() {}
    
    /**
     * @brief Called when entity becomes inactive
     */
    virtual void OnDisable() {}
    
    /**
     * @brief Called once per frame during fixed timestep
     */
    virtual void OnFixedUpdate(float delta_time) {}
    
    /**
     * @brief Called once per frame during variable timestep
     */
    virtual void OnUpdate(float delta_time) {}
    
    /**
     * @brief Called during late update phase
     */
    virtual void OnLateUpdate(float delta_time) {}
    
    /**
     * @brief Get owning entity
     */
    class Entity* GetEntity() const { return entity_; }

protected:
    class Entity* entity_ = nullptr;

private:
    // The component's exact type, recorded by Entity::AddComponent<T> -- the
    // one place that knows it, since it constructs a T. Lets GetComponent<T>
    // compare a key instead of running dynamic_cast. See GetComponent.
    std::size_t           type_hash_ = 0;
    const std::type_info* type_      = nullptr;
    // Hooks this component's type overrides, also recorded by AddComponent<T>.
    // All of them until then: a component that did not come through
    // AddComponent is assumed to need every call, which is what it got before.
    uint8_t               hooks_     = kHookAll;

    friend class Entity;
};

namespace detail {
/// Which hooks T overrides, decided at compile time. `&T::OnUpdate` has type
/// `void (Component::*)(float)` exactly when neither T nor any class between
/// it and Component declares OnUpdate -- i.e. T runs the empty default.
template <typename T>
constexpr uint8_t component_hooks() {
    uint8_t m = 0;
    if constexpr (!std::is_same_v<decltype(&T::OnFixedUpdate), void (Component::*)(float)>)
        m |= kHookFixedUpdate;
    if constexpr (!std::is_same_v<decltype(&T::OnUpdate), void (Component::*)(float)>)
        m |= kHookUpdate;
    if constexpr (!std::is_same_v<decltype(&T::OnLateUpdate), void (Component::*)(float)>)
        m |= kHookLateUpdate;
    return m;
}

// typeid(T).hash_code() hashes the mangled NAME on every call, so it is
// computed once per type. Name-based, so the value agrees across modules even
// where each module has its own type_info object for T.
template <typename T>
std::size_t component_type_hash() {
    static const std::size_t h = typeid(T).hash_code();
    return h;
}
}  // namespace detail

// ============================================================================
// Entity Class
// ============================================================================

/**
 * @class Entity
 * @brief Basic game object with transform and components
 */
class Entity : public std::enable_shared_from_this<Entity> {
public:
    Entity(const std::string& name = "Entity");
    ~Entity();
    
    // Deleted copy, allow move
    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;
    
    Entity(Entity&&) = default;
    Entity& operator=(Entity&&) = default;
    
    // ========== Identification ==========
    
    /**
     * @brief Get entity name
     */
    const std::string& GetName() const { return name_; }
    
    /**
     * @brief Set entity name
     */
    void SetName(const std::string& name) { name_ = name; }
    
    /**
     * @brief Get unique entity ID
     */
    uint32_t GetId() const { return id_; }
    
    /**
     * @brief Get entity tag for grouping
     */
    const std::string& GetTag() const { return tag_; }
    
    /**
     * @brief Set entity tag
     */
    void SetTag(const std::string& tag) { tag_ = tag; }
    
    // ========== Transform ==========
    
    /**
     * @brief Get entity transform
     */
    Transform* GetTransform() { return &transform_; }
    const Transform* GetTransform() const { return &transform_; }
    
    /**
     * @brief Get mesh component
     */
    MeshComponent* GetMeshComponent() { return &mesh_component_; }
    const MeshComponent* GetMeshComponent() const { return &mesh_component_; }

    /// Rigged-character data. Empty gltf_path = not a skinned character, which
    /// is what the render path tests. Data only — the GPU-side actor is owned
    /// editor-side and keyed by entity id, so a scene stays loadable without a
    /// device. See skinned_mesh_component.h.
    NpcAgentComponent* GetNpcAgentComponent() { return &npc_agent_component_; }
    const NpcAgentComponent* GetNpcAgentComponent() const { return &npc_agent_component_; }

    ParticleEmitterComponent* GetParticleEmitterComponent() { return &particle_emitter_component_; }
    const ParticleEmitterComponent* GetParticleEmitterComponent() const { return &particle_emitter_component_; }

    SkinnedMeshComponent* GetSkinnedMeshComponent() { return &skinned_mesh_component_; }
    const SkinnedMeshComponent* GetSkinnedMeshComponent() const { return &skinned_mesh_component_; }
    
    /**
     * @brief Set mesh on entity
     */
    void SetMesh(const std::string& mesh_path) {
        mesh_component_.SetMesh(mesh_path);
    }
    
    // ========== Hierarchy ==========
    
    /**
     * @brief Get parent entity
     */
    std::shared_ptr<Entity> GetParent() const { return parent_; }
    
    /**
     * @brief Set parent entity
     */
    void SetParent(std::shared_ptr<Entity> parent);
    
    /**
     * @brief Add child entity
     */
    void AddChild(std::shared_ptr<Entity> child);
    
    /**
     * @brief Remove child entity
     */
    void RemoveChild(std::shared_ptr<Entity> child);
    
    /**
     * @brief Get all children
     */
    const std::vector<std::shared_ptr<Entity>>& GetChildren() const { return children_; }
    
    /**
     * @brief Find child by name (recursive)
     */
    std::shared_ptr<Entity> FindChild(const std::string& name);
    
    /**
     * @brief Detach from parent
     */
    void Detach();
    
    // ========== Lifecycle ==========
    
    /**
     * @brief Check if entity is active
     */
    bool IsActive() const { return is_active_; }
    
    /**
     * @brief Set entity active/inactive
     */
    void SetActive(bool active);
    
    /**
     * @brief Check if entity is active in hierarchy (self and all parents)
     */
    bool IsActiveInHierarchy() const;
    
    // ========== Component Management ==========
    
    /**
     * @brief Add component of type T
     */
    template<typename T, typename... Args>
    std::shared_ptr<T> AddComponent(Args&&... args) {
        static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
        
        auto component = std::make_shared<T>(std::forward<Args>(args)...);
        component->entity_    = this;
        component->type_hash_ = detail::component_type_hash<T>();
        component->type_      = &typeid(T);   // make_shared<T>: exactly T
        component->hooks_     = detail::component_hooks<T>();
        component->OnAttach();

        components_.push_back(component);
        hook_mask_ |= component->hooks_;
        detail::bump_component_set_generation();
        return component;
    }

    /// OR of the hooks every component on this entity overrides.
    uint8_t HookMask() const { return hook_mask_; }

    /// Is `comp` exactly a T? Only meaningful for a FINAL T -- see below.
    ///
    /// This is the hot path behind every GetComponent<T>(), which the scene
    /// calls about ten times per entity per frame from the ECS mirror alone --
    /// mostly for components the entity does not have. As a dynamic_cast scan
    /// that measured ~75 ns per probe on MinGW, where a failed RTTI type
    /// comparison falls back to comparing mangled names: 15 ms a frame at 20k
    /// entities for the mirror, before draw building asked the same questions
    /// again.
    ///
    /// For a FINAL T, dynamic_cast<T> succeeds exactly when the component's
    /// type is T, so comparing the key recorded in AddComponent gives the same
    /// answer -- an integer compare on a miss, which is the common case. A
    /// non-final T could be a base of the component, so GetComponent keeps the
    /// original dynamic_cast scan for it, unchanged. That is why the leaf
    /// components are marked final: the fast path is taken only where it is
    /// provably the same answer, and adding a subclass later means removing
    /// `final`, which falls back to correct-and-slower rather than silently
    /// missing components.
    template<typename T>
    static bool is_exactly(const Component& comp) {
        static_assert(std::is_final_v<T>, "only a final type can be matched by key");
        if (comp.type_ == nullptr)                       // not created by AddComponent
            return dynamic_cast<const T*>(&comp) != nullptr;
        return comp.type_hash_ == detail::component_type_hash<T>() &&
               *comp.type_ == typeid(T);
    }

    /**
     * @brief Get component of type T (first found)
     */
    template<typename T>
    std::shared_ptr<T> GetComponent() {
        static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");

        if constexpr (std::is_final_v<T>) {
            for (auto& comp : components_)
                if (comp && is_exactly<T>(*comp)) return std::static_pointer_cast<T>(comp);
            return nullptr;
        } else {
            for (auto& comp : components_) {
                if (auto casted = std::dynamic_pointer_cast<T>(comp)) {
                    return casted;
                }
            }
            return nullptr;
        }
    }

    /**
     * @brief Get component of type T (const version)
     */
    template<typename T>
    std::shared_ptr<const T> GetComponent() const {
        static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");

        if constexpr (std::is_final_v<T>) {
            for (const auto& comp : components_)
                if (comp && is_exactly<T>(*comp)) return std::static_pointer_cast<const T>(comp);
            return nullptr;
        } else {
            for (const auto& comp : components_) {
                if (auto casted = std::dynamic_pointer_cast<const T>(comp)) {
                    return casted;
                }
            }
            return nullptr;
        }
    }

    /**
     * @brief Get all components of type T
     */
    template<typename T>
    std::vector<std::shared_ptr<T>> GetComponents() {
        static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");

        std::vector<std::shared_ptr<T>> result;
        if constexpr (std::is_final_v<T>) {
            for (auto& comp : components_)
                if (comp && is_exactly<T>(*comp)) result.push_back(std::static_pointer_cast<T>(comp));
        } else {
            for (auto& comp : components_) {
                if (auto casted = std::dynamic_pointer_cast<T>(comp)) {
                    result.push_back(casted);
                }
            }
        }
        return result;
    }
    
    /**
     * @brief Remove component
     */
    void RemoveComponent(std::shared_ptr<Component> component);
    
    /**
     * @brief Get all components
     */
    const std::vector<std::shared_ptr<Component>>& GetComponents() const { return components_; }
    
    // ========== Update Callbacks ==========
    
    /**
     * @brief Called once per frame (fixed timestep)
     */
    void FixedUpdate(float delta_time);
    
    /**
     * @brief Called once per frame (variable timestep)
     */
    void Update(float delta_time);
    
    /**
     * @brief Called once per frame (late phase)
     */
    void LateUpdate(float delta_time);
    
    // ========== Scene Management ==========
    
    /**
     * @brief Destroy entity (marks for removal)
     */
    void Destroy();
    
    /**
     * @brief Check if entity is marked for destruction
     */
    bool IsDestroyed() const { return is_destroyed_; }

protected:
    // Identification
    std::string name_;
    std::string tag_;
    uint32_t id_ = 0;
    static uint32_t next_id_;
    
    // Hierarchy
    std::shared_ptr<Entity> parent_;
    std::vector<std::shared_ptr<Entity>> children_;
    
    // Transform
    Transform transform_;
    
    // Mesh component
    MeshComponent mesh_component_;
    SkinnedMeshComponent skinned_mesh_component_;
    ParticleEmitterComponent particle_emitter_component_;
    NpcAgentComponent npc_agent_component_;
    
    // Components
    std::vector<std::shared_ptr<Component>> components_;
    uint8_t hook_mask_ = 0;   // OR of components_' hooks_; kept by Add/RemoveComponent

    // State
    bool is_active_ = true;
    bool is_destroyed_ = false;
    
    friend class Scene;
};

} // namespace schizo::scene
