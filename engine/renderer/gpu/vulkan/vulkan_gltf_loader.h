/**
 * @file vulkan_gltf_loader.h
 * @brief glTF 2.0 → deferred-renderer scene loader (Phase 5 Week 1).
 *
 * Lives in `gws::renderer::gpu::` and produces our own `Mesh`, `Material`,
 * `Texture` types — distinct from the older `vks::glTFLoader` which only
 * generated a placeholder cube. Backed by the in-tree mini-tinygltf and
 * stb_image (for PNG/JPG decode of external/embedded images).
 *
 * Returns a `Scene` whose `draw_items` vector can be handed straight to
 * `VulkanRenderGraph::set_draw_items()`.
 */

#pragma once

#include "vulkan_render_graph.h" // for DrawItem
#include <vulkan/vulkan.h>
#include <memory>
#include <string>
#include <vector>

namespace gws::renderer::gpu {

// A parsed glTF, kept OPAQUE on purpose. TINYGLTF_IMPLEMENTATION is defined
// build-wide, so including tinygltf.h here would emit its implementation into
// every translation unit that touches this header -- and the editor touches it
// a lot. Construction and destruction happen in the .cpp where the type is
// complete, which is all a shared_ptr needs.
struct ParsedGltfModel;

class VulkanDevice;
class Mesh;
class Material;
class Texture;
class TextureManager;

/**
 * @struct Scene
 * @brief Owned bundle of GPU assets produced by the glTF loader.
 *
 * `draw_items` references entries in `meshes` / `materials` — keep the
 * Scene alive for as long as the render graph references its draws.
 */
struct Scene {
    // Indexed by glTF *texture* index (image+sampler pair), not image index —
    // so a texture's colour space and sampler are resolved per usage. Shared
    // so textures owned by the TextureManager (external files, deduplicated
    // across scenes) can be referenced here without double-ownership.
    std::vector<std::shared_ptr<Texture>>  textures;
    std::vector<std::unique_ptr<Material>> materials;
    std::vector<std::unique_ptr<Mesh>>     meshes;
    std::vector<DrawItem>                  draw_items;

    Scene() = default;
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) noexcept = default;
    Scene& operator=(Scene&&) noexcept = default;
};

class GltfLoader {
public:
    /**
     * @brief Load a `.gltf` (JSON+separate bin) or `.glb` (binary) file
     *        and produce GPU-resident meshes/materials/textures.
     *
     * @param device                  Vulkan device.
     * @param material_layout         Descriptor-set layout from `Material::create_descriptor_set_layout`.
     *                                Same layout the geometry pipeline was built against.
     * @param material_pool           Descriptor pool sized for at least the number of glTF materials.
     * @param path                    File path. Format is detected by extension.
     * @param textures                Optional TextureManager. When supplied, external-file
     *                                images are loaded (and deduplicated) through it, materials
     *                                use its shared default textures, and all samplers come from
     *                                its cache. When null, textures are decoded scene-locally
     *                                with baked samplers (legacy behaviour) — still correct, just
     *                                without cross-scene dedup / cooked-`.ctex` / hot-reload.
     * @return                        Owned scene, or nullptr on parse / GPU upload failure.
     */
    /**
     * @brief Read a glTF/glb into a tinygltf::Model. CPU only -- no device, no
     *        Vulkan calls -- so it is safe to run on a worker thread while the
     *        editor keeps rendering. Pair with build() on the device's thread.
     */
    static std::shared_ptr<ParsedGltfModel> parse(const std::string& path);

    /**
     * @brief Turn a parsed model into GPU resources. Touches the device, so it
     *        must run on the thread that owns it.
     */
    static std::unique_ptr<Scene> build(VulkanDevice* device,
                                        VkDescriptorSetLayout material_layout,
                                        VkDescriptorPool material_pool,
                                        const std::string& path,
                                        const std::shared_ptr<ParsedGltfModel>& parsed,
                                        TextureManager* textures = nullptr);

    static std::unique_ptr<Scene> load(VulkanDevice* device,
                                       VkDescriptorSetLayout material_layout,
                                       VkDescriptorPool material_pool,
                                       const std::string& path,
                                       TextureManager* textures = nullptr);
};

} // namespace gws::renderer::gpu
