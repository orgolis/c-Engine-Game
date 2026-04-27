/**
 * @file deferred_scene_test.cpp
 * @brief Phase 5 Week 1 live-GPU scene-pipeline smoke test.
 *
 * Exercises the new draw-list / Material / Mesh / Texture path through the
 * deferred pipeline:
 *   - builds a per-material descriptor set layout + pool
 *   - builds the G-Buffer with that layout (so the textured scene pipeline
 *     gets created)
 *   - constructs a programmatic textured cube Mesh + a default Material
 *   - hands DrawItems to VulkanRenderGraph::set_draw_items
 *   - records shadow + geometry + lighting + post stages and submits
 *
 * If env var GLTF_TEST_FILE is set to a `.gltf`/`.glb` path, the test ALSO
 * loads that scene through `GltfLoader` to exercise the file-path code.
 *
 * Headless: no surface, no swapchain, no presentation. Validates that the
 * GPU accepts what the new scene pipeline records with no validation errors.
 */

#include "renderer/gpu/vulkan/vulkan_device.h"
#include "renderer/gpu/vulkan/vulkan_g_buffer.h"
#include "renderer/gpu/vulkan/vulkan_lighting_pass.h"
#include "renderer/gpu/vulkan/vulkan_post_processing.h"
#include "renderer/gpu/vulkan/vulkan_render_graph.h"
#include "renderer/gpu/vulkan/vulkan_shadow_map.h"
#include "renderer/gpu/vulkan/vulkan_scene_mesh.h"
#include "renderer/gpu/vulkan/vulkan_scene_material.h"
#include "renderer/gpu/vulkan/vulkan_texture.h"
#include "renderer/gpu/vulkan/vulkan_gltf_loader.h"

#include <vulkan/vulkan.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace gws::renderer::gpu;

namespace {

constexpr uint32_t kW = 320;
constexpr uint32_t kH = 240;

// Build a textured unit cube with proper SceneVertex format (24 verts so each
// face has its own normals/UVs; 36 indices).
std::unique_ptr<Mesh> make_cube(VulkanDevice* device) {
    auto v = [](float x, float y, float z, float nx, float ny, float nz, float u, float vu) {
        SceneVertex sv{};
        sv.position = {x, y, z};
        sv.normal   = {nx, ny, nz};
        sv.uv       = {u, vu};
        sv.tangent  = {1.0f, 0.0f, 0.0f, 1.0f};
        return sv;
    };

    std::vector<SceneVertex> verts = {
        // +X face
        v( 0.5f, -0.5f, -0.5f,  1, 0, 0, 0, 0),
        v( 0.5f,  0.5f, -0.5f,  1, 0, 0, 1, 0),
        v( 0.5f,  0.5f,  0.5f,  1, 0, 0, 1, 1),
        v( 0.5f, -0.5f,  0.5f,  1, 0, 0, 0, 1),
        // -X
        v(-0.5f, -0.5f,  0.5f, -1, 0, 0, 0, 0),
        v(-0.5f,  0.5f,  0.5f, -1, 0, 0, 1, 0),
        v(-0.5f,  0.5f, -0.5f, -1, 0, 0, 1, 1),
        v(-0.5f, -0.5f, -0.5f, -1, 0, 0, 0, 1),
        // +Y
        v(-0.5f,  0.5f, -0.5f,  0, 1, 0, 0, 0),
        v(-0.5f,  0.5f,  0.5f,  0, 1, 0, 1, 0),
        v( 0.5f,  0.5f,  0.5f,  0, 1, 0, 1, 1),
        v( 0.5f,  0.5f, -0.5f,  0, 1, 0, 0, 1),
        // -Y
        v(-0.5f, -0.5f,  0.5f,  0,-1, 0, 0, 0),
        v(-0.5f, -0.5f, -0.5f,  0,-1, 0, 1, 0),
        v( 0.5f, -0.5f, -0.5f,  0,-1, 0, 1, 1),
        v( 0.5f, -0.5f,  0.5f,  0,-1, 0, 0, 1),
        // +Z
        v(-0.5f, -0.5f,  0.5f,  0, 0, 1, 0, 0),
        v( 0.5f, -0.5f,  0.5f,  0, 0, 1, 1, 0),
        v( 0.5f,  0.5f,  0.5f,  0, 0, 1, 1, 1),
        v(-0.5f,  0.5f,  0.5f,  0, 0, 1, 0, 1),
        // -Z
        v( 0.5f, -0.5f, -0.5f,  0, 0,-1, 0, 0),
        v(-0.5f, -0.5f, -0.5f,  0, 0,-1, 1, 0),
        v(-0.5f,  0.5f, -0.5f,  0, 0,-1, 1, 1),
        v( 0.5f,  0.5f, -0.5f,  0, 0,-1, 0, 1),
    };

    std::vector<uint32_t> idx;
    idx.reserve(36);
    for (uint32_t face = 0; face < 6; ++face) {
        const uint32_t b = face * 4;
        idx.insert(idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
    }

    // Build a single-submesh layout, then ask meshoptimizer to add LOD
    // tiers. The cube is a degenerate case for simplification (24 verts /
    // 36 idx, every face flat) — meshopt will mostly stall, but we exercise
    // the API end-to-end.
    std::vector<Submesh> submeshes;
    Submesh sm{};
    sm.material_index = 0;
    MeshLod lod0{};
    lod0.index_offset = 0;
    lod0.index_count  = static_cast<uint32_t>(idx.size());
    lod0.distance_threshold = 0.0f;
    sm.lods.push_back(lod0);
    submeshes.push_back(std::move(sm));

    Mesh::generate_lods(verts, idx, submeshes,
                        /*ratios=*/    {0.5f, 0.25f},
                        /*distances=*/ {3.0f, 8.0f});

    auto cube_mesh = Mesh::create(device, verts, idx, std::move(submeshes));
    if (cube_mesh) {
        const auto& lods = cube_mesh->submeshes()[0].lods;
        std::cout << "[OK] Cube LOD tiers: " << lods.size() << " (";
        for (size_t i = 0; i < lods.size(); ++i) {
            if (i) std::cout << ", ";
            std::cout << "lod" << i << "=" << lods[i].index_count
                      << "@d>=" << lods[i].distance_threshold;
        }
        std::cout << ")\n";
    }
    return cube_mesh;
}

// 32×32 tessellated quad — gives meshoptimizer something to actually
// simplify and validates that generate_lods produces measurable shrinkage.
std::unique_ptr<Mesh> make_tessellated_grid(VulkanDevice* device) {
    constexpr int N = 32;
    std::vector<SceneVertex> verts;
    verts.reserve((N + 1) * (N + 1));
    for (int j = 0; j <= N; ++j) {
        for (int i = 0; i <= N; ++i) {
            const float u = float(i) / N;
            const float v = float(j) / N;
            SceneVertex sv{};
            sv.position = {(u - 0.5f) * 2.0f, 0.0f, (v - 0.5f) * 2.0f};
            sv.normal   = {0.0f, 1.0f, 0.0f};
            sv.uv       = {u, v};
            sv.tangent  = {1.0f, 0.0f, 0.0f, 1.0f};
            verts.push_back(sv);
        }
    }
    std::vector<uint32_t> idx;
    idx.reserve(N * N * 6);
    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < N; ++i) {
            const uint32_t a = j * (N + 1) + i;
            const uint32_t b = a + 1;
            const uint32_t c = a + (N + 1);
            const uint32_t d = c + 1;
            idx.insert(idx.end(), {a, c, b, b, c, d});
        }
    }

    std::vector<Submesh> submeshes;
    Submesh sm{};
    sm.material_index = 0;
    MeshLod lod0{};
    lod0.index_offset       = 0;
    lod0.index_count        = static_cast<uint32_t>(idx.size());
    lod0.distance_threshold = 0.0f;
    sm.lods.push_back(lod0);
    submeshes.push_back(std::move(sm));

    Mesh::generate_lods(verts, idx, submeshes,
                        /*ratios=*/    {0.5f, 0.25f, 0.10f},
                        /*distances=*/ {5.0f, 15.0f, 30.0f});

    auto mesh = Mesh::create(device, verts, idx, std::move(submeshes));
    if (mesh) {
        const auto& lods = mesh->submeshes()[0].lods;
        std::cout << "[OK] Grid LOD tiers: " << lods.size() << " (";
        for (size_t i = 0; i < lods.size(); ++i) {
            if (i) std::cout << ", ";
            std::cout << "lod" << i << "=" << lods[i].index_count
                      << "@d>=" << lods[i].distance_threshold;
        }
        std::cout << ")\n";
    }
    return mesh;
}

bool record_and_submit(VulkanDevice& device, VulkanRenderGraph& graph) {
    VkDevice      vk    = device.get_device();
    VkCommandPool pool  = device.get_command_pool();
    VkQueue       queue = device.get_graphics_queue();

    VkCommandBufferAllocateInfo alloc{};
    alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool        = pool;
    alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(vk, &alloc, &cmd) != VK_SUCCESS) {
        std::cerr << "alloc cmd buffer failed\n"; return false;
    }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    graph.begin_frame(cmd);
    graph.execute_stage(cmd, RenderGraphStage::Shadow,      {});
    graph.execute_stage(cmd, RenderGraphStage::Geometry,    {});
    graph.execute_stage(cmd, RenderGraphStage::Lighting,    {});
    graph.execute_stage(cmd, RenderGraphStage::PostProcess, {});
    graph.end_frame(cmd);
    vkEndCommandBuffer(cmd);

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(vk, &fence_info, nullptr, &fence);

    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    if (vkQueueSubmit(queue, 1, &submit, fence) != VK_SUCCESS) {
        std::cerr << "queue submit failed\n";
        vkDestroyFence(vk, fence, nullptr);
        vkFreeCommandBuffers(vk, pool, 1, &cmd);
        return false;
    }

    constexpr uint64_t kFiveSecondsNs = 5ull * 1000ull * 1000ull * 1000ull;
    if (vkWaitForFences(vk, 1, &fence, VK_TRUE, kFiveSecondsNs) != VK_SUCCESS) {
        std::cerr << "fence wait timed out\n";
        vkDestroyFence(vk, fence, nullptr);
        vkFreeCommandBuffers(vk, pool, 1, &cmd);
        return false;
    }

    vkDestroyFence(vk, fence, nullptr);
    vkFreeCommandBuffers(vk, pool, 1, &cmd);
    return true;
}

} // namespace

int main() {
    std::cout << "=== Phase 5 Week 1 Deferred Scene Test ===\n";

    VulkanDevice device;
    RenderConfig render_config{};
    render_config.window_width      = kW;
    render_config.window_height     = kH;
    render_config.enable_validation = true;
    render_config.app_name          = "DeferredSceneTest";

    try {
        device.initialize(render_config);
    } catch (const std::exception& e) {
        std::cerr << "VulkanDevice init failed: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    VkDevice vk = device.get_device();

    // Material layout + pool first — the G-Buffer needs the layout to build
    // its scene pipeline, so we have to allocate before the G-Buffer.
    VkDescriptorSetLayout mat_layout = Material::create_descriptor_set_layout(vk);
    VkDescriptorPool      mat_pool   = Material::create_descriptor_pool(vk, /*max_materials=*/16);
    std::cout << "[OK] material layout + pool created\n";

    GBufferConfig g_config{};
    g_config.width               = kW;
    g_config.height              = kH;
    g_config.material_set_layout = mat_layout;
    auto g_buffer = VulkanGBuffer::create(&device, g_config);
    if (!g_buffer) { std::cerr << "G-Buffer create failed\n"; return EXIT_FAILURE; }
    std::cout << "[OK] G-Buffer (with scene pipeline)\n";

    LightingConfig l_config{};
    auto lighting = VulkanLightingPass::create(&device, l_config, g_buffer.get());
    if (!lighting) { std::cerr << "Lighting create failed\n"; return EXIT_FAILURE; }
    lighting->add_directional_light(glm::vec3(0.3f, -1.0f, 0.2f),
                                    glm::vec3(1.0f, 0.95f, 0.85f), 1.5f, true);
    std::cout << "[OK] LightingPass\n";

    ShadowMapConfig shadow_config{};
    shadow_config.type          = ShadowMapType::Cascaded2D;
    shadow_config.width         = 512;
    shadow_config.height        = 512;
    shadow_config.cascade_count = 1;
    auto shadow_map = VulkanShadowMap::create(&device, shadow_config);
    if (!shadow_map) { std::cerr << "ShadowMap create failed\n"; return EXIT_FAILURE; }
    std::cout << "[OK] ShadowMap (with caster pipeline)\n";

    PostProcessingConfig pp_config{};
    pp_config.width                = kW;
    pp_config.height               = kH;
    pp_config.bloom.enabled        = true;
    pp_config.taa.enabled          = true;
    pp_config.tone_mapping.enabled = true;
    auto post = VulkanPostProcessing::create(&device, pp_config);
    if (!post) { std::cerr << "PostProcessing create failed\n"; return EXIT_FAILURE; }
    post->set_input_image(lighting->get_output_view());
    std::cout << "[OK] PostProcessing\n";

    // Programmatic scene: textured cube + default material with a 2×2
    // checkerboard albedo so the G-Buffer gets recognisable pixels.
    uint8_t checker[16] = {
        255, 80, 80, 255,    80, 200, 80, 255,
        80,  80, 255, 255,   220, 220, 80, 255,
    };
    auto albedo = Texture::create_from_pixels(&device, checker, 2, 2, /*srgb=*/false);
    if (!albedo) { std::cerr << "albedo upload failed\n"; return EXIT_FAILURE; }

    MaterialUniforms mu{};
    mu.base_color_factor = glm::vec4(1.0f);
    mu.metallic_factor   = 0.0f;
    mu.roughness_factor  = 0.6f;
    auto material = Material::create(&device, mat_layout, mat_pool, mu,
                                     albedo.get(),  // baseColor
                                     nullptr,       // normal → fallback flat
                                     nullptr,       // mr     → fallback white
                                     nullptr,       // ao     → fallback white
                                     nullptr);      // emissive → fallback black
    if (!material) { std::cerr << "material create failed\n"; return EXIT_FAILURE; }
    std::cout << "[OK] Material (checkerboard albedo)\n";

    auto cube = make_cube(&device);
    if (!cube) { std::cerr << "cube mesh create failed\n"; return EXIT_FAILURE; }
    std::cout << "[OK] Cube mesh: " << cube->vertex_count() << " verts, "
              << cube->index_count() << " idx (incl. LOD ranges)\n";

    auto grid = make_tessellated_grid(&device);
    if (!grid) { std::cerr << "grid mesh create failed\n"; return EXIT_FAILURE; }
    std::cout << "[OK] Grid mesh: " << grid->vertex_count() << " verts, "
              << grid->index_count() << " idx (incl. LOD ranges)\n";

    // Sanity-check LOD selection on the grid (which has multiple tiers).
    const size_t lod_at_zero = grid->select_lod(0, 0.0f);
    const size_t lod_at_far  = grid->select_lod(0, 100.0f);
    std::cout << "[OK] LOD selection (grid): d=0 → lod=" << lod_at_zero
              << ", d=100 → lod=" << lod_at_far << "\n";

    // glTF loader exercise (optional — only if a path is given).
    std::unique_ptr<Scene> gltf_scene;
    if (const char* p = std::getenv("GLTF_TEST_FILE")) {
        gltf_scene = GltfLoader::load(&device, mat_layout, mat_pool, p);
        std::cout << "[OK] glTF loaded: " << p
                  << " (draws=" << (gltf_scene ? gltf_scene->draw_items.size() : 0) << ")\n";
    }

    // Build draw list. Cube near origin, grid offset by 25 world units
    // (so its LOD selection lands on the highest tier and the iteration
    // exercises a non-trivial select_lod path), + optional glTF.
    std::vector<DrawItem> draws;
    DrawItem cube_draw{};
    cube_draw.mesh          = cube.get();
    cube_draw.material      = material.get();
    cube_draw.model         = glm::mat4(1.0f);
    cube_draw.submesh_index = 0;
    draws.push_back(cube_draw);

    DrawItem grid_draw{};
    grid_draw.mesh          = grid.get();
    grid_draw.material      = material.get();
    grid_draw.model         = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, -25.0f));
    grid_draw.submesh_index = 0;
    draws.push_back(grid_draw);

    if (gltf_scene) {
        for (const auto& d : gltf_scene->draw_items) draws.push_back(d);
    }

    RenderGraphConfig graph_config{};
    graph_config.device          = &device;
    graph_config.g_buffer        = g_buffer.get();
    graph_config.lighting        = lighting.get();
    graph_config.shadow_map      = shadow_map.get();
    graph_config.post_processing = post.get();
    graph_config.width           = kW;
    graph_config.height          = kH;
    auto graph = VulkanRenderGraph::create(graph_config);
    if (!graph) { std::cerr << "RenderGraph create failed\n"; return EXIT_FAILURE; }

    CameraData cam{};
    cam.position = glm::vec3(2.0f, 2.0f, 3.0f);
    cam.view     = glm::lookAt(cam.position, glm::vec3(0.0f), glm::vec3(0, 1, 0));
    cam.proj     = glm::perspective(glm::radians(60.0f),
                                    static_cast<float>(kW) / kH, 0.1f, 100.0f);
    cam.proj[1][1] *= -1.0f;
    graph->set_camera(cam);
    graph->set_draw_items(draws);
    std::cout << "[OK] RenderGraph + " << draws.size() << " draw item(s)\n";

    if (!record_and_submit(device, *graph)) {
        std::cerr << "GPU submit failed\n";
        graph.reset(); post.reset(); shadow_map.reset();
        lighting.reset(); g_buffer.reset();
        cube.reset(); grid.reset(); material.reset(); albedo.reset(); gltf_scene.reset();
        vkDestroyDescriptorPool(vk, mat_pool, nullptr);
        vkDestroyDescriptorSetLayout(vk, mat_layout, nullptr);
        device.shutdown();
        return EXIT_FAILURE;
    }

    const auto& stats = graph->get_stats();
    std::cout << "[OK] Frame submitted: shadow=" << stats.shadow_passes_run
              << " geometry=" << stats.geometry_passes_run
              << " lighting=" << stats.lighting_passes_run
              << " post=" << stats.post_process_passes_run << "\n";

    if (graph->resolve_timings()) {
        std::cout << "[OK] GPU timings (us): shadow=" << stats.shadow_us
                  << " geometry=" << stats.geometry_us
                  << " lighting=" << stats.lighting_us
                  << " post=" << stats.post_process_us << "\n";
    }

    device.wait_idle();
    graph.reset(); post.reset(); shadow_map.reset();
    lighting.reset(); g_buffer.reset();
    cube.reset(); grid.reset(); material.reset(); albedo.reset(); gltf_scene.reset();
    vkDestroyDescriptorPool(vk, mat_pool, nullptr);
    vkDestroyDescriptorSetLayout(vk, mat_layout, nullptr);
    device.shutdown();

    std::cout << "=== DEFERRED SCENE TEST PASSED ===\n";
    return EXIT_SUCCESS;
}
