// Layout probe: sizeof/alignof of the engine's hot types, built with the
// editor's own include paths and flags. Not part of the engine.
#include <cstdio>
#include <functional>
#include <unordered_map>
#include <mutex>
#include "entity.h"
#include "transform.h"
#include "scene.h"
#include "vulkan/vulkan_scene_mesh.h"
#include "vulkan/vulkan_scene_material.h"
#include "vulkan/vulkan_render_graph.h"
#include "vulkan/vulkan_skinned_mesh.h"
#include "vulkan/vulkan_lighting_pass.h"
#include "vulkan/vulkan_g_buffer.h"
#include "vulkan/culling.h"
#include "scene_render_bridge.h"
#include "jobs/job.h"

#define P(T) std::printf("%-58s %6zu %4zu\n", #T, sizeof(T), alignof(T))

int main() {
    using namespace schizo::scene;
    namespace g = gws::renderer::gpu;
    P(Entity);
    P(Component);
    P(Transform);
    P(MeshComponent);
    P(SkinnedMeshComponent);
    P(ParticleEmitterComponent);
    P(NpcAgentComponent);
    P(MeshRendererComponent);
    P(std::string);
    P(std::shared_ptr<Entity>);
    P(std::vector<int>);
    P(std::function<void()>);
    P(std::mutex);
    P(g::SceneVertex);
    P(g::Submesh);
    P(g::MeshLod);
    P(g::Meshlet);
    P(g::Mesh);
    P(g::Material);
    P(g::MaterialUniforms);
    P(g::SkinnedVertex);
    P(g::DrawItem);
    P(g::Light);
    P(g::AABB);
    P(g::GeometryPushConstants);
    P(schizo::editor::detail_draw::EntityDraw);
    P(gws::jobs::Job);
    P(glm::mat4);
    P(glm::quat);
    return 0;
}
