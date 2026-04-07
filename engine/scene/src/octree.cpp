#include "scene/include/octree.h"
#include "scene/include/entity.h"
#include "scene/include/transform.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace schizo::scene {

// ============================================================================
// OctreeNode Implementation
// ============================================================================

OctreeNode::OctreeNode(const glm::vec3& min, const glm::vec3& max, uint32_t depth)
    : min_(min), max_(max), depth_(depth) {
}

OctreeNode::~OctreeNode() {
    Clear();
}

void OctreeNode::Insert(std::shared_ptr<Entity> entity) {
    if (!entity) return;
    
    glm::vec3 pos = entity->GetTransform()->GetWorldPosition();
    
    // If we have children, try to insert into appropriate child
    if (HasChildren()) {
        int child_idx = FindChildContainingPoint(pos);
        if (child_idx >= 0) {
            children_[child_idx]->Insert(entity);
            return;
        }
    }
    
    // Add to this node
    entities_.push_back(entity);
    
    // If we've exceeded capacity and can subdivide, do so
    if (entities_.size() > max_entities_per_node_ && depth_ < max_depth_) {
        if (!HasChildren()) {
            Split();
        }
        
        // Try to redistribute entities to children
        std::vector<std::shared_ptr<Entity>> to_redistribute;
        for (auto& ent : entities_) {
            glm::vec3 ent_pos = ent->GetTransform()->GetWorldPosition();
            if (int idx = FindChildContainingPoint(ent_pos); idx >= 0) {
                to_redistribute.push_back(ent);
            }
        }
        
        for (auto& ent : to_redistribute) {
            auto it = std::find(entities_.begin(), entities_.end(), ent);
            if (it != entities_.end()) {
                entities_.erase(it);
            }
            
            int idx = FindChildContainingPoint(ent->GetTransform()->GetWorldPosition());
            if (idx >= 0) {
                children_[idx]->Insert(ent);
            }
        }
    }
}

bool OctreeNode::Remove(std::shared_ptr<Entity> entity) {
    if (!entity) return false;
    
    // Try children first
    if (HasChildren()) {
        for (auto& child : children_) {
            if (child->Remove(entity)) {
                return true;
            }
        }
    }
    
    // Remove from this node
    auto it = std::find(entities_.begin(), entities_.end(), entity);
    if (it != entities_.end()) {
        entities_.erase(it);
        return true;
    }
    
    return false;
}

void OctreeNode::Clear() {
    entities_.clear();
    children_.clear();
}

void OctreeNode::QueryAABB(const glm::vec3& min, const glm::vec3& max,
                          std::vector<std::shared_ptr<Entity>>& result) const {
    // If we don't intersect, return early
    if (!IntersectsAABB(min, max)) {
        return;
    }
    
    // Add entities from this node that are in bounds
    for (const auto& entity : entities_) {
        result.push_back(entity);
    }
    
    // Query children
    if (HasChildren()) {
        for (const auto& child : children_) {
            child->QueryAABB(min, max, result);
        }
    }
}

void OctreeNode::QuerySphere(const glm::vec3& center, float radius,
                            std::vector<std::shared_ptr<Entity>>& result) const {
    // If we don't intersect, return early
    if (!IntersectsSphere(center, radius)) {
        return;
    }
    
    // Add entities from this node that are in sphere
    for (const auto& entity : entities_) {
        glm::vec3 pos = entity->GetTransform()->GetWorldPosition();
        float dist = glm::distance(pos, center);
        if (dist <= radius) {
            result.push_back(entity);
        }
    }
    
    // Query children
    if (HasChildren()) {
        for (const auto& child : children_) {
            child->QuerySphere(center, radius, result);
        }
    }
}

void OctreeNode::GetAllEntities(std::vector<std::shared_ptr<Entity>>& result) const {
    for (const auto& entity : entities_) {
        result.push_back(entity);
    }
    
    if (HasChildren()) {
        for (const auto& child : children_) {
            child->GetAllEntities(result);
        }
    }
}

void OctreeNode::Split() {
    if (HasChildren()) return;  // Already split
    
    glm::vec3 center = (min_ + max_) * 0.5f;
    glm::vec3 size = max_ - min_;
    glm::vec3 half_size = size * 0.5f;
    
    // Create 8 child nodes
    // Order: 0-3 are bottom layer (Y-), 4-7 are top layer (Y+)
    children_.emplace_back(std::make_unique<OctreeNode>(
        glm::vec3(min_.x, min_.y, min_.z), 
        glm::vec3(center.x, center.y, center.z), 
        depth_ + 1));
    
    children_.emplace_back(std::make_unique<OctreeNode>(
        glm::vec3(center.x, min_.y, min_.z),
        glm::vec3(max_.x, center.y, center.z),
        depth_ + 1));
    
    children_.emplace_back(std::make_unique<OctreeNode>(
        glm::vec3(center.x, min_.y, center.z),
        glm::vec3(max_.x, center.y, max_.z),
        depth_ + 1));
    
    children_.emplace_back(std::make_unique<OctreeNode>(
        glm::vec3(min_.x, min_.y, center.z),
        glm::vec3(center.x, center.y, max_.z),
        depth_ + 1));
    
    // Top layer
    children_.emplace_back(std::make_unique<OctreeNode>(
        glm::vec3(min_.x, center.y, min_.z),
        glm::vec3(center.x, max_.y, center.z),
        depth_ + 1));
    
    children_.emplace_back(std::make_unique<OctreeNode>(
        glm::vec3(center.x, center.y, min_.z),
        glm::vec3(max_.x, max_.y, center.z),
        depth_ + 1));
    
    children_.emplace_back(std::make_unique<OctreeNode>(
        glm::vec3(center.x, center.y, center.z),
        glm::vec3(max_.x, max_.y, max_.z),
        depth_ + 1));
    
    children_.emplace_back(std::make_unique<OctreeNode>(
        glm::vec3(min_.x, center.y, center.z),
        glm::vec3(center.x, max_.y, max_.z),
        depth_ + 1));
}

int OctreeNode::FindChildContainingPoint(const glm::vec3& point) const {
    if (!HasChildren()) return -1;
    
    for (size_t i = 0; i < children_.size(); ++i) {
        if (children_[i]->Contains(point)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool OctreeNode::Contains(const glm::vec3& point) const {
    return point.x >= min_.x && point.x <= max_.x &&
           point.y >= min_.y && point.y <= max_.y &&
           point.z >= min_.z && point.z <= max_.z;
}

bool OctreeNode::IntersectsAABB(const glm::vec3& aabb_min, const glm::vec3& aabb_max) const {
    // AABB-AABB intersection test
    return !(aabb_max.x < min_.x || aabb_min.x > max_.x ||
             aabb_max.y < min_.y || aabb_min.y > max_.y ||
             aabb_max.z < min_.z || aabb_min.z > max_.z);
}

bool OctreeNode::IntersectsSphere(const glm::vec3& center, float radius) const {
    // Closest point in AABB to sphere center
    glm::vec3 closest = glm::clamp(center, min_, max_);
    
    // Check if distance from closest point to sphere center <= radius
    return glm::distance(closest, center) <= radius;
}

// ============================================================================
// Octree Implementation
// ============================================================================

Octree::Octree(const glm::vec3& min, const glm::vec3& max,
               uint32_t max_depth, uint32_t objects_per_node)
    : root_(std::make_unique<OctreeNode>(min, max, 0)) {
    root_->max_depth_ = max_depth;
    root_->max_entities_per_node_ = objects_per_node;
}

void Octree::Insert(std::shared_ptr<Entity> entity) {
    if (root_) {
        root_->Insert(entity);
    }
}

void Octree::Remove(std::shared_ptr<Entity> entity) {
    if (root_) {
        root_->Remove(entity);
    }
}

void Octree::Clear() {
    if (root_) {
        root_->Clear();
    }
}

std::vector<std::shared_ptr<Entity>> Octree::QueryAABB(const glm::vec3& min, const glm::vec3& max) const {
    std::vector<std::shared_ptr<Entity>> result;
    if (root_) {
        root_->QueryAABB(min, max, result);
    }
    return result;
}

std::vector<std::shared_ptr<Entity>> Octree::QuerySphere(const glm::vec3& center, float radius) const {
    std::vector<std::shared_ptr<Entity>> result;
    if (root_) {
        root_->QuerySphere(center, radius, result);
    }
    return result;
}

std::vector<std::shared_ptr<Entity>> Octree::GetAllEntities() const {
    std::vector<std::shared_ptr<Entity>> result;
    if (root_) {
        root_->GetAllEntities(result);
    }
    return result;
}

Octree::Stats Octree::GetStats() const {
    Stats stats;
    if (root_) {
        CalculateStats(root_.get(), stats);
    }
    return stats;
}

void Octree::CalculateStats(const OctreeNode* node, Stats& stats) const {
    if (!node) return;
    
    stats.node_count++;
    stats.max_depth = std::max(stats.max_depth, node->depth_);
    stats.entity_count += node->GetEntityCount();
    stats.max_entities_per_node = std::max(stats.max_entities_per_node, node->GetEntityCount());
    
    if (node->HasChildren()) {
        for (uint32_t i = 0; i < node->GetChildCount(); ++i) {
            CalculateStats(node->children_[i].get(), stats);
        }
    }
}

} // namespace schizo::scene
