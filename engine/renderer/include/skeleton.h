#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <cstdint>

namespace schizo::renderer {

// ============================================================================
// Bone Structure & Skeleton
// ============================================================================

/**
 * @struct BoneTransform
 * @brief Local transform for a bone (position, rotation, scale)
 */
struct BoneTransform {
    glm::vec3 position = glm::vec3(0);
    glm::quat rotation = glm::quat(1, 0, 0, 0);
    glm::vec3 scale = glm::vec3(1);
    
    // Convert to transformation matrix
    glm::mat4 ToMatrix() const;
    
    // Interpolate between two transforms
    static BoneTransform Lerp(const BoneTransform& a, const BoneTransform& b, float t);
    // Slerp: Spherical Linear Interpolation (smooth rotation blending)
    static BoneTransform Slerp(const BoneTransform& a, const BoneTransform& b, float t);
};

/**
 * @struct Bone
 * @brief A bone in the skeleton (local to parent, doesn't include world transform)
 */
struct Bone {
    uint16_t id = 0;
    std::string name;
    int32_t parent_id = -1;  // -1 means root bone
    
    // Local transform relative to parent
    BoneTransform local_transform;
    
    // World space transform (computed from parent hierarchy)
    glm::mat4 world_matrix = glm::mat4(1);
    glm::mat4 inverse_bind_matrix = glm::mat4(1);  // For skeletal animation
    
    // Bone properties
    float length = 0.0f;  // Distance to first child (for visualization)
    bool is_animated = true;
};

/**
 * @class Skeleton
 * @brief Hierarchical skeleton with bones and transforms
 */
class Skeleton {
public:
    Skeleton();
    virtual ~Skeleton() = default;
    
    /**
     * Add a bone to the skeleton
     * @param name Bone identifier
     * @param parent_name Parent bone name (empty for root)
     * @param local_transform Initial local transform
     * @return Bone ID
     */
    uint16_t AddBone(const std::string& name, const std::string& parent_name = "",
                     const BoneTransform& local_transform = BoneTransform());
    
    /**
     * Get bone by ID
     */
    Bone* GetBone(uint16_t id);
    const Bone* GetBone(uint16_t id) const;
    
    /**
     * Get bone by name
     */
    Bone* GetBone(const std::string& name);
    const Bone* GetBone(const std::string& name) const;
    
    /**
     * Get bone ID by name
     */
    int32_t GetBoneId(const std::string& name) const;
    
    /**
     * Set local transform for a bone
     */
    void SetBoneLocalTransform(uint16_t id, const BoneTransform& transform);
    void SetBoneLocalTransform(const std::string& name, const BoneTransform& transform);
    
    /**
     * Update world transforms from local transforms (bottom-up)
     * @param bone_id Bone to update (0 for all from root)
     */
    void UpdateWorldTransforms(uint16_t bone_id = 0);
    
    /**
     * Get world transform for a bone
     */
    glm::mat4 GetWorldMatrix(uint16_t id) const;
    
    /**
     * Get child bones of a specific bone
     */
    std::vector<uint16_t> GetChildBones(uint16_t parent_id) const;
    
    /**
     * Get all bones in order (for iteration)
     */
    const std::vector<Bone>& GetAllBones() const { return bones_; }
    uint32_t GetBoneCount() const { return bones_.size(); }
    
    /**
     * Create a copy of this skeleton with same structure but reset poses
     */
    std::unique_ptr<Skeleton> Clone() const;
    
private:
    std::vector<Bone> bones_;
    std::map<std::string, uint16_t> bone_name_map_;
    
    void UpdateWorldTransformsRecursive(uint16_t bone_id);
};

} // namespace schizo::renderer
