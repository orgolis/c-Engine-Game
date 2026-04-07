#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <cstdint>

namespace schizo::scene {

// Forward declarations
class Entity;

// ============================================================================
// Octree Node
// ============================================================================

/**
 * @class OctreeNode
 * @brief Single node in octree spatial partitioning structure
 */
class OctreeNode {
public:
    OctreeNode(const glm::vec3& min, const glm::vec3& max, uint32_t depth);
    ~OctreeNode();
    
    /**
     * @brief Insert entity into octree
     */
    void Insert(std::shared_ptr<Entity> entity);
    
    /**
     * @brief Remove entity from octree
     */
    bool Remove(std::shared_ptr<Entity> entity);
    
    /**
     * @brief Clear all entities
     */
    void Clear();
    
    /**
     * @brief Get entities in AABB
     */
    void QueryAABB(const glm::vec3& min, const glm::vec3& max, 
                   std::vector<std::shared_ptr<Entity>>& result) const;
    
    /**
     * @brief Get entities in sphere
     */
    void QuerySphere(const glm::vec3& center, float radius,
                     std::vector<std::shared_ptr<Entity>>& result) const;
    
    /**
     * @brief Get all entities in node
     */
    void GetAllEntities(std::vector<std::shared_ptr<Entity>>& result) const;
    
    /**
     * @brief Get bounding box
     */
    const glm::vec3& GetMin() const { return min_; }
    const glm::vec3& GetMax() const { return max_; }
    glm::vec3 GetCenter() const { return (min_ + max_) * 0.5f; }
    glm::vec3 GetSize() const { return max_ - min_; }
    
    /**
     * @brief Check if node has children
     */
    bool HasChildren() const { return children_.size() > 0; }
    
    /**
     * @brief Get child count
     */
    uint32_t GetChildCount() const { return static_cast<uint32_t>(children_.size()); }
    
    /**
     * @brief Get entity count in this node
     */
    uint32_t GetEntityCount() const { return static_cast<uint32_t>(entities_.size()); }

protected:
    glm::vec3 min_, max_;
    uint32_t depth_;
    
    std::vector<std::shared_ptr<Entity>> entities_;
    std::vector<std::unique_ptr<OctreeNode>> children_;
    
    uint32_t max_entities_per_node_ = 8;
    uint32_t max_depth_ = 8;
    
    /**
     * @brief Split node into 8 children
     */
    void Split();
    
    /**
     * @brief Find child that contains point
     */
    int FindChildContainingPoint(const glm::vec3& point) const;
    
    /**
     * @brief Check if point is in node bounds
     */
    bool Contains(const glm::vec3& point) const;
    
    /**
     * @brief Check AABB intersection
     */
    bool IntersectsAABB(const glm::vec3& min, const glm::vec3& max) const;
    
    /**
     * @brief Check sphere intersection
     */
    bool IntersectsSphere(const glm::vec3& center, float radius) const;
    
    friend class Octree;
};

// ============================================================================
// Octree
// ============================================================================

/**
 * @class Octree
 * @brief 3D octree spatial partitioning structure for efficient entity queries
 */
class Octree {
public:
    /**
     * @brief Create octree with bounds
     */
    Octree(const glm::vec3& min, const glm::vec3& max,
           uint32_t max_depth = 8, uint32_t objects_per_node = 8);
    
    /**
     * @brief Insert entity into octree
     */
    void Insert(std::shared_ptr<Entity> entity);
    
    /**
     * @brief Remove entity from octree
     */
    void Remove(std::shared_ptr<Entity> entity);
    
    /**
     * @brief Clear octree
     */
    void Clear();
    
    /**
     * @brief Query entities in AABB
     */
    std::vector<std::shared_ptr<Entity>> QueryAABB(const glm::vec3& min, const glm::vec3& max) const;
    
    /**
     * @brief Query entities in sphere
     */
    std::vector<std::shared_ptr<Entity>> QuerySphere(const glm::vec3& center, float radius) const;
    
    /**
     * @brief Get all entities
     */
    std::vector<std::shared_ptr<Entity>> GetAllEntities() const;
    
    /**
     * @brief Get root node
     */
    OctreeNode* GetRoot() { return root_.get(); }
    const OctreeNode* GetRoot() const { return root_.get(); }
    
    /**
     * @brief Get bounds
     */
    glm::vec3 GetMin() const { return root_->GetMin(); }
    glm::vec3 GetMax() const { return root_->GetMax(); }
    
    /**
     * @brief Get statistics
     */
    struct Stats {
        uint32_t node_count = 0;
        uint32_t max_depth = 0;
        uint32_t entity_count = 0;
        uint32_t max_entities_per_node = 0;
    };
    Stats GetStats() const;

protected:
    std::unique_ptr<OctreeNode> root_;
    
    /**
     * @brief Calculate statistics recursively
     */
    void CalculateStats(const OctreeNode* node, Stats& stats) const;
};

} // namespace schizo::scene
