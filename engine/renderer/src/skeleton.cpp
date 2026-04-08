#include "skeleton.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>

namespace schizo::renderer {

// ============================================================================
// BoneTransform
// ============================================================================

glm::mat4 BoneTransform::ToMatrix() const {
    glm::mat4 pos = glm::translate(glm::mat4(1), position);
    glm::mat4 rot = glm::mat4_cast(rotation);
    glm::mat4 scl = glm::scale(glm::mat4(1), scale);
    return pos * rot * scl;
}

BoneTransform BoneTransform::Lerp(const BoneTransform& a, const BoneTransform& b, float t) {
    BoneTransform result;
    result.position = glm::mix(a.position, b.position, t);
    result.rotation = glm::slerp(a.rotation, b.rotation, t);
    result.scale = glm::mix(a.scale, b.scale, t);
    return result;
}

BoneTransform BoneTransform::Slerp(const BoneTransform& a, const BoneTransform& b, float t) {
    return Lerp(a, b, t);  // For full transform, use Lerp with slerp for rotation
}

// ============================================================================
// Skeleton
// ============================================================================

Skeleton::Skeleton() = default;

uint16_t Skeleton::AddBone(const std::string& name, const std::string& parent_name,
                          const BoneTransform& local_transform) {
    Bone bone;
    bone.id = static_cast<uint16_t>(bones_.size());
    bone.name = name;
    bone.local_transform = local_transform;
    
    // Find parent
    if (!parent_name.empty()) {
        auto it = bone_name_map_.find(parent_name);
        if (it != bone_name_map_.end()) {
            bone.parent_id = it->second;
        }
    }
    
    uint16_t bone_id = bone.id;
    bones_.push_back(bone);
    bone_name_map_[name] = bone_id;
    
    return bone_id;
}

Bone* Skeleton::GetBone(uint16_t id) {
    if (id < bones_.size()) {
        return &bones_[id];
    }
    return nullptr;
}

const Bone* Skeleton::GetBone(uint16_t id) const {
    if (id < bones_.size()) {
        return &bones_[id];
    }
    return nullptr;
}

Bone* Skeleton::GetBone(const std::string& name) {
    auto it = bone_name_map_.find(name);
    if (it != bone_name_map_.end()) {
        return GetBone(it->second);
    }
    return nullptr;
}

const Bone* Skeleton::GetBone(const std::string& name) const {
    auto it = bone_name_map_.find(name);
    if (it != bone_name_map_.end()) {
        return GetBone(it->second);
    }
    return nullptr;
}

int32_t Skeleton::GetBoneId(const std::string& name) const {
    auto it = bone_name_map_.find(name);
    if (it != bone_name_map_.end()) {
        return it->second;
    }
    return -1;
}

void Skeleton::SetBoneLocalTransform(uint16_t id, const BoneTransform& transform) {
    if (id < bones_.size()) {
        bones_[id].local_transform = transform;
    }
}

void Skeleton::SetBoneLocalTransform(const std::string& name, const BoneTransform& transform) {
    Bone* bone = GetBone(name);
    if (bone) {
        bone->local_transform = transform;
    }
}

void Skeleton::UpdateWorldTransforms(uint16_t bone_id) {
    // Update all bones starting from roots
    for (size_t i = 0; i < bones_.size(); ++i) {
        if (bones_[i].parent_id < 0) {
            UpdateWorldTransformsRecursive(i);
        }
    }
}

void Skeleton::UpdateWorldTransformsRecursive(uint16_t bone_id) {
    if (bone_id >= bones_.size()) return;
    
    Bone& bone = bones_[bone_id];
    
    if (bone.parent_id < 0) {
        // Root bone: world = local
        bone.world_matrix = bone.local_transform.ToMatrix();
    } else {
        // Child bone: world = parent.world * local
        const Bone& parent = bones_[bone.parent_id];
        bone.world_matrix = parent.world_matrix * bone.local_transform.ToMatrix();
    }
    
    // Update all children
    std::vector<uint16_t> children = GetChildBones(bone_id);
    for (uint16_t child_id : children) {
        UpdateWorldTransformsRecursive(child_id);
    }
}

glm::mat4 Skeleton::GetWorldMatrix(uint16_t id) const {
    const Bone* bone = GetBone(id);
    if (bone) {
        return bone->world_matrix;
    }
    return glm::mat4(1);
}

std::vector<uint16_t> Skeleton::GetChildBones(uint16_t parent_id) const {
    std::vector<uint16_t> children;
    for (size_t i = 0; i < bones_.size(); ++i) {
        if (bones_[i].parent_id == static_cast<int32_t>(parent_id)) {
            children.push_back(i);
        }
    }
    return children;
}

std::unique_ptr<Skeleton> Skeleton::Clone() const {
    auto skeleton = std::make_unique<Skeleton>();
    
    // Copy all bones
    for (const Bone& bone : bones_) {
        Bone new_bone = bone;
        new_bone.local_transform = BoneTransform();  // Reset to default pose
        new_bone.world_matrix = glm::mat4(1);
        skeleton->bones_.push_back(new_bone);
        skeleton->bone_name_map_[bone.name] = bone.id;
    }
    
    skeleton->UpdateWorldTransforms();
    return skeleton;
}

} // namespace schizo::renderer
