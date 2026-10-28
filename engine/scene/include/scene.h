#pragma once

#include "entity.h"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace schizo::scene {

// ============================================================================
// Scene Configuration
// ============================================================================

/**
 * @struct SceneConfig
 * @brief Configuration for scene creation
 */
struct SceneConfig {
    std::string name = "DefaultScene";
    
    // Spatial partitioning
    bool use_spatial_partitioning = true;
    glm::vec3 world_bounds_min = glm::vec3(-1000.0f);
    glm::vec3 world_bounds_max = glm::vec3(1000.0f);
    uint32_t octree_max_depth = 8;
    uint32_t octree_objects_per_node = 8;
    
    // Physics
    glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f);

    // Update settings
    float fixed_timestep = 0.02f;  // 50 FPS fixed update
    float max_frame_time = 0.1f;   // Cap frame time

    // Environment / sky (scene-bound). `sky_hdr` is a project-relative path to an
    // equirectangular .hdr (empty = the procedural gradient sky). `sky_intensity`
    // scales the environment/IBL contribution.
    std::string sky_hdr;
    float       sky_intensity = 1.0f;
};

// ============================================================================
// Scene Class
// ============================================================================

/**
 * @class Scene
 * @brief Manages all entities in a scene with spatial partitioning and physics
 */
class Scene {
public:
    Scene(const std::string& name = "Scene");
    ~Scene();
    
    // Deleted copy, allow move
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    
    Scene(Scene&&) = default;
    Scene& operator=(Scene&&) = default;
    
    /**
     * @brief Create scene with configuration
     */
    static std::shared_ptr<Scene> Create(const SceneConfig& config);
    
    // ========== Scene Properties ==========
    
    /**
     * @brief Get scene name
     */
    const std::string& GetName() const { return name_; }
    
    /**
     * @brief Get scene configuration
     */
    const SceneConfig& GetConfig() const { return config_; }
    
    /**
     * @brief Set gravity vector
     */
    void SetGravity(const glm::vec3& gravity) { config_.gravity = gravity; }
    
    /**
     * @brief Get gravity vector
     */
    glm::vec3 GetGravity() const { return config_.gravity; }

    // ========== Environment / sky (scene-bound) ==========
    const std::string& GetSkyHdr() const { return config_.sky_hdr; }
    void  SetSkyHdr(const std::string& path) { config_.sky_hdr = path; }
    float GetSkyIntensity() const { return config_.sky_intensity; }
    void  SetSkyIntensity(float v) { config_.sky_intensity = v; }
    
    // ========== Entity Management ==========
    
    /**
     * @brief Create new entity in scene
     */
    std::shared_ptr<Entity> CreateEntity(const std::string& name = "Entity");
    
    /**
     * @brief Add existing entity to scene
     */
    void AddEntity(std::shared_ptr<Entity> entity);
    
    /**
     * @brief Remove entity from scene
     */
    void RemoveEntity(std::shared_ptr<Entity> entity);
    
    /**
     * @brief Get entity by ID
     */
    std::shared_ptr<Entity> GetEntityById(uint32_t id) const;
    
    /**
     * @brief Get entity by name (first found)
     */
    std::shared_ptr<Entity> GetEntityByName(const std::string& name) const;
    
    /**
     * @brief Get entities by tag
     */
    std::vector<std::shared_ptr<Entity>> GetEntitiesByTag(const std::string& tag) const;
    
    /**
     * @brief Get all entities
     */
    const std::vector<std::shared_ptr<Entity>>& GetEntities() const { return entities_; }
    
    /**
     * @brief Get entity count
     */
    uint32_t GetEntityCount() const { return static_cast<uint32_t>(entities_.size()); }
    
    // ========== Spatial Queries ==========
    
    /**
     * @brief Get entities in AABB
     */
    std::vector<std::shared_ptr<Entity>> QueryAABB(const glm::vec3& min, const glm::vec3& max) const;
    
    /**
     * @brief Get entities in sphere
     */
    std::vector<std::shared_ptr<Entity>> QuerySphere(const glm::vec3& center, float radius) const;
    
    /**
     * @brief Get entities in frustum
     */
    std::vector<std::shared_ptr<Entity>> QueryFrustum(const glm::mat4& view_proj) const;
    
    // ========== Lifecycle ==========
    
    /**
     * @brief Initialize scene (called before first update)
     */
    void Initialize();
    
    /**
     * @brief called once per frame to update all entities
     */
    void Update(float delta_time);
    
    /**
     * @brief Cleanup scene
     */
    void Shutdown();
    
    /**
     * @brief Reload scene (destroy and recreate)
     */
    void Reload();
    
    /**
     * @brief Clear all entities
     */
    void Clear();
    
    // ========== Debugging ==========
    
    /**
     * @brief Print scene hierarchy to console
     */
    void PrintHierarchy() const;
    
    /**
     * @brief Get scene statistics
     */
    struct Stats {
        uint32_t entity_count = 0;
        uint32_t active_entity_count = 0;
        uint32_t component_count = 0;
        uint32_t octree_nodes = 0;
        uint32_t octree_depth = 0;
    };
    Stats GetStats() const;

protected:
    std::string name_;
    SceneConfig config_;
    
    // Entity management
    std::vector<std::shared_ptr<Entity>> entities_;
    uint64_t entity_list_version_ = 0;   // bumped by AddEntity / RemoveEntity / Clear

    // Entities with a component overriding each per-frame hook, so Update
    // visits only those (see Scene::Update). Rebuilt when the entity list or
    // any component set changes. shared_ptr, not raw: an entity removed by an
    // earlier pass stays valid until the next rebuild instead of dangling.
    std::vector<std::shared_ptr<Entity>> hooked_fixed_, hooked_update_, hooked_late_;
    uint64_t hooked_list_version_  = ~0ull;
    uint64_t hooked_component_gen_ = ~0ull;
    uint64_t hooked_rebuilds_      = 0;
    uint64_t seen_destroy_gen_     = 0;
    std::unordered_map<uint32_t, std::shared_ptr<Entity>> entities_by_id_;
    std::unordered_map<std::string, std::vector<std::shared_ptr<Entity>>> entities_by_tag_;

    
    // Timing
    float accumulated_time_ = 0.0f;
    bool is_initialized_ = false;
    
    /**
     * @brief Update entity cache for queries
     */
    void UpdateSpatialPartitioning();
    
    /**
     * @brief Process destroyed entities
     */
    void ProcessDestroyedEntities();
    
    /**
     * @brief Update all entities
     */
    void UpdateEntities(float delta_time);

    /// Rebuild the per-hook entity lists if the entity list or any component
    /// set changed since the last rebuild. Cheap when nothing did.
    void RefreshHookedEntities();

public:
    /// How many times the per-hook lists were rebuilt -- for checks: a scene
    /// that rebuilds every frame has silently lost the point of caching them.
    uint64_t HookedListRebuilds() const { return hooked_rebuilds_; }
    size_t   HookedUpdateCount() const { return hooked_update_.size(); }

    /// Entities with at least one component of any of Ts, in scene order.
    ///
    /// For per-frame "find the few entities that have X" loops -- the lights,
    /// the water surfaces -- which used to walk the whole scene every frame to
    /// find two entities: at 20k entities, ~1.8 ms for the lights alone.
    /// Cached per type set and rebuilt only when the entity list or any
    /// component set changes (the same signals as the per-hook lists), so a
    /// steady scene pays nothing. Callers still check whatever per-frame state
    /// they care about (active, enabled) on the few entities returned.
    ///
    /// The reference is valid until the next call for the same type set.
    template <typename... Ts>
    const std::vector<std::shared_ptr<Entity>>& EntitiesWithAny() {
        static_assert(sizeof...(Ts) > 0, "name at least one component type");
        std::size_t key = 0;
        ((key = key * 1000003u ^ detail::component_type_hash<Ts>()), ...);
        ComponentIndex& ix = component_index_[key];
        const uint64_t gen = detail::component_set_generation();
        if (!ix.built || ix.gen != gen || ix.version != entity_list_version_) {
            ix.list.clear();
            for (const auto& e : entities_)
                if (e && (... || (e->GetComponent<Ts>() != nullptr)))
                    ix.list.push_back(e);
            ix.gen     = gen;
            ix.version = entity_list_version_;
            ix.built   = true;
            ++component_index_rebuilds_;
        }
        return ix.list;
    }
    uint64_t ComponentIndexRebuilds() const { return component_index_rebuilds_; }

private:
    struct ComponentIndex {
        std::vector<std::shared_ptr<Entity>> list;
        uint64_t gen     = 0;
        uint64_t version = 0;
        bool     built   = false;
    };
    std::unordered_map<std::size_t, ComponentIndex> component_index_;
    uint64_t component_index_rebuilds_ = 0;
};

} // namespace schizo::scene
