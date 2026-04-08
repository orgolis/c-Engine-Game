#pragma once

#include "entity.h"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

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
};

} // namespace schizo::scene
