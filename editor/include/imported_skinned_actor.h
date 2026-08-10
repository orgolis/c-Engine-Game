#pragma once
// ============================================================================
// ImportedSkinnedActor — a rigged character loaded from a .gltf/.glb and driven
// through the SAME proven path as SkinnedAnimDemo (skeleton pose → GPU compute
// skin → DrawItem into the normal G-buffer/shadow/cull path), but from a real
// asset instead of a procedural rig. Layer 1 of "Path B" (rigged-asset import).
// ============================================================================
#include "vulkan/skinned_gltf_loader.h"
#include "vulkan/vulkan_skinned_mesh.h"
#include "vulkan/vulkan_scene_material.h"
#include "vulkan/vulkan_texture_manager.h"
#include "vulkan/vulkan_render_graph.h"   // DrawItem

#include "skeleton.h"
#include "animation.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace schizo::editor {

class ImportedSkinnedActor {
public:
    static std::unique_ptr<ImportedSkinnedActor> create(
        gws::renderer::gpu::VulkanDevice* device,
        VkDescriptorSetLayout mat_layout, VkDescriptorPool mat_pool,
        gws::renderer::gpu::TextureManager* textures,
        const std::string& gltf_path,
        const glm::vec3& world_pos,
        std::string* out_error = nullptr) {
        auto a = std::unique_ptr<ImportedSkinnedActor>(new ImportedSkinnedActor());
        if (!a->build(device, mat_layout, mat_pool, textures, gltf_path, world_pos, out_error))
            return nullptr;
        return a;
    }

    /// CPU tick: advance the current clip and pose the skeleton. Call once/frame.
    void advance(float dt) {
        if (!skeleton_ || clips_.empty()) return;
        time_ += dt;
        clips_[clip_index_]->Sample(*skeleton_, time_, /*loop=*/true);
        skeleton_->UpdateWorldTransforms();
    }

    /// Pose the skeleton WITHOUT advancing time. A paused actor still has to be
    /// posed on the first frame, or it renders in bind pose until you press
    /// play -- which reads as "the model is broken", not "it is paused".
    void refresh_pose() {
        if (!skeleton_ || clips_.empty()) return;
        clips_[clip_index_]->Sample(*skeleton_, time_, /*loop=*/true);
        skeleton_->UpdateWorldTransforms();
    }

    /// Place the actor with a full transform, so an entity's Transform (with
    /// rotation and non-uniform scale) drives it rather than the position+scale
    /// pair this class used when there was only ever one actor.
    void set_model(const glm::mat4& m) { model_ = m; use_model_ = true; }

    /// GPU: record the skinning dispatch from the current pose. Call right after
    /// the frame command buffer begins (before shadow/geometry read the vbo).
    void record_skin(VkCommandBuffer cmd) {
        if (!mesh_ || !skeleton_) return;
        const uint32_t n = skeleton_->GetBoneCount();
        std::vector<glm::mat4> globals(n);
        for (uint32_t i = 0; i < n; ++i)
            globals[i] = skeleton_->GetWorldMatrix(static_cast<uint16_t>(i));
        mesh_->skin(cmd, globals);
    }

    gws::renderer::gpu::DrawItem draw_item() const {
        gws::renderer::gpu::DrawItem d{};
        d.mesh          = mesh_->output_mesh();
        d.material      = mat_.get();
        d.model         = use_model_
                            ? model_
                            : glm::translate(glm::mat4(1.0f), world_pos_) *
                              glm::scale(glm::mat4(1.0f), glm::vec3(scale_));
        d.submesh_index = 0;
        d.is_blend      = false;
        d.is_terrain    = false;
        return d;
    }

    const std::string& name() const { return name_; }
    size_t clip_count() const { return clips_.size(); }
    size_t clip_index() const { return clip_index_; }
    void   set_clip(size_t i) { if (i < clips_.size()) { clip_index_ = i; time_ = 0.0f; } }
    void   set_world_pos(const glm::vec3& p) { world_pos_ = p; }
    void   set_scale(float s) { scale_ = s; }
    float  scale() const { return scale_; }

private:
    ImportedSkinnedActor() = default;

    bool build(gws::renderer::gpu::VulkanDevice* device,
               VkDescriptorSetLayout mat_layout, VkDescriptorPool mat_pool,
               gws::renderer::gpu::TextureManager* textures,
               const std::string& gltf_path, const glm::vec3& world_pos,
               std::string* out_error) {
        auto loaded = gws::renderer::gpu::load_skinned_gltf(gltf_path);
        if (!loaded.valid) { if (out_error) *out_error = loaded.error; return false; }

        skeleton_  = loaded.skeleton;
        clips_     = loaded.animations;
        world_pos_ = world_pos;
        name_      = std::filesystem::path(gltf_path).filename().string();

        mesh_ = gws::renderer::gpu::SkinnedMesh::create(
            device, loaded.vertices, loaded.indices, loaded.submeshes, loaded.inverse_bind);
        if (!mesh_) { if (out_error) *out_error = "GPU SkinnedMesh creation failed"; return false; }

        gws::renderer::gpu::MaterialUniforms mu{};
        mu.base_color_factor = glm::vec4(0.80f, 0.80f, 0.82f, 1.0f);
        mu.metallic_factor   = 0.0f;
        mu.roughness_factor  = 0.7f;
        const gws::renderer::gpu::Texture* dw = textures ? textures->white()  : nullptr;
        const gws::renderer::gpu::Texture* dn = textures ? textures->normal() : nullptr;
        const gws::renderer::gpu::Texture* db = textures ? textures->black()  : nullptr;
        mat_ = gws::renderer::gpu::Material::create(device, mat_layout, mat_pool, mu,
                                                    nullptr, nullptr, nullptr, nullptr, nullptr,
                                                    dw, dn, db);
        return mat_ != nullptr;
    }

    std::shared_ptr<schizo::renderer::Skeleton> skeleton_;
    std::vector<std::shared_ptr<schizo::renderer::AnimationClip>> clips_;
    std::unique_ptr<gws::renderer::gpu::SkinnedMesh> mesh_;
    std::unique_ptr<gws::renderer::gpu::Material>    mat_;

    glm::mat4 model_{1.0f};
    bool      use_model_ = false;   // set once an entity Transform drives this
    glm::vec3 world_pos_{0.0f};
    float     scale_ = 1.0f;
    float     time_  = 0.0f;
    size_t    clip_index_ = 0;
    std::string name_;
};

} // namespace schizo::editor
