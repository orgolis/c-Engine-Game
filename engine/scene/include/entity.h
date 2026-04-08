#pragma once

#include "transform.h"
#include "mesh_component.h"
#include <memory>
#include <vector>
#include <string>
#include <cstdint>
#include <typeinfo>

namespace schizo::scene {

// Forward declarations
class Scene;
class Component;

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
    
    friend class Entity;
};

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
        component->entity_ = this;
        component->OnAttach();
        
        components_.push_back(component);
        return component;
    }
    
    /**
     * @brief Get component of type T (first found)
     */
    template<typename T>
    std::shared_ptr<T> GetComponent() {
        static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
        
        for (auto& comp : components_) {
            if (auto casted = std::dynamic_pointer_cast<T>(comp)) {
                return casted;
            }
        }
        return nullptr;
    }
    
    /**
     * @brief Get component of type T (const version)
     */
    template<typename T>
    std::shared_ptr<const T> GetComponent() const {
        static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
        
        for (const auto& comp : components_) {
            if (auto casted = std::dynamic_pointer_cast<const T>(comp)) {
                return casted;
            }
        }
        return nullptr;
    }
    
    /**
     * @brief Get all components of type T
     */
    template<typename T>
    std::vector<std::shared_ptr<T>> GetComponents() {
        static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
        
        std::vector<std::shared_ptr<T>> result;
        for (auto& comp : components_) {
            if (auto casted = std::dynamic_pointer_cast<T>(comp)) {
                result.push_back(casted);
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
    
    // Components
    std::vector<std::shared_ptr<Component>> components_;
    
    // State
    bool is_active_ = true;
    bool is_destroyed_ = false;
    
    friend class Scene;
};

} // namespace schizo::scene
