// ====================
// model_check — headless custom-model load + culling verification
// ====================
//
// Reproduces the editor's model path end-to-end without a window:
//   1. Loads each given model file through the editor's OWN AssetMeshCache
//      (the same code build_draw_items uses), reporting meshes / draw items /
//      materials / bounding boxes — a null here is exactly the "model falls
//      back to a cube" bug.
//   2. Runs the NEW cull_draw_items_frustum (sphere fast-path + compaction)
//      over each model's draw list from a typical editor camera and reports
//      kept/culled — a full cull of an on-screen model = false-cull bug.
//
//   model_check [model files...]        (default: scans assets/models)

#include "scene_render_bridge.h"   // AssetMeshCache + build path (editor header)
#include "asset_path_util.h"       // resolve_asset_path / utf8_path (shared w/ physics)
#include "vulkan/vulkan_device.h"
#include "vulkan/vulkan_scene_material.h"
#include "vulkan/culling.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

using namespace gws::renderer::gpu;
namespace fs = std::filesystem;

// Mirror scene_playback_manager's LoadMeshTriangles_OBJ file access (the same
// resolve + UTF-8 open the physics MeshCollider now uses) to prove the collider
// path reaches the same file the renderer does. Returns triangle count, 0 on
// open failure. .obj only (glТF collider load goes through tinygltf, verified
// by the render load succeeding).
static size_t probe_collider_tris(const std::string& path) {
    std::ifstream f(schizo::editor::utf8_path(schizo::editor::resolve_asset_path(path)));
    if (!f.is_open()) return 0;
    size_t verts = 0, tris = 0;
    std::string line;
    while (std::getline(f, line)) {
        if (line.size() > 1 && line[0] == 'v' && line[1] == ' ') ++verts;
        else if (line.size() > 1 && line[0] == 'f' && line[1] == ' ') {
            int n = 0; std::string s; std::istringstream ls(line.substr(2));
            while (ls >> s) ++n;
            if (n >= 3) tris += static_cast<size_t>(n - 2);
        }
    }
    return verts ? tris : 0;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== model_check: custom-model load + culling + collider ===\n");

    std::vector<std::string> paths;
    for (int i = 1; i < argc; ++i) paths.push_back(argv[i]);
    if (paths.empty()) {
        std::error_code ec;
        auto is_model = [](std::string x) {
            for (char& c : x) c = static_cast<char>(::tolower(c));
            return x.size() > 4 && (x.substr(x.size() - 4) == ".obj" ||
                                    x.substr(x.size() - 4) == ".glb" ||
                                    x.substr(x.size() - 4) == ".fbx" ||
                                    (x.size() > 5 && x.substr(x.size() - 5) == ".gltf"));
        };
        for (const auto& e : fs::directory_iterator("assets/models", ec))
            if (e.is_regular_file() && is_model(e.path().string()))
                paths.push_back(e.path().string());
        if (paths.empty()) {
            std::printf("model_check: no inputs and assets/models not found (run from repo root)\n");
            return 1;
        }
    }

    VulkanDevice device;
    RenderConfig cfg{};
    cfg.window_width = 64; cfg.window_height = 64;
    cfg.enable_validation = true;
    cfg.app_name = "model_check";
    try { device.initialize(cfg); }
    catch (const std::exception& e) {
        std::printf("model_check: device init failed: %s\n", e.what());
        return 1;
    }

    VkDescriptorSetLayout layout = Material::create_descriptor_set_layout(device.get_device());
    VkDescriptorPool      pool   = Material::create_descriptor_pool(device.get_device(), 64);

    // Typical editor camera: a few units up/back, looking at the origin.
    const glm::mat4 view = glm::lookAt(glm::vec3(6.0f, 5.0f, 10.0f),
                                       glm::vec3(0.0f, 0.0f, 0.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
    const Frustum frustum = Frustum::from_matrix(proj * view);

    schizo::editor::AssetMeshCache cache;
    int failures = 0;

    for (const std::string& path : paths) {
        const auto* scene = cache.get_or_load(path, &device, layout, pool);
        if (!scene) {
            std::printf("[LOAD FAIL] %s  -> get_or_load returned null (editor would show a CUBE)\n",
                        path.c_str());
            ++failures;
            continue;
        }

        // Report geometry + bounds.
        size_t total_lods = 0;
        for (const auto& m : scene->meshes)
            for (const auto& sm : m->submeshes()) total_lods += sm.lods.size();
        const AABB& bb0 = scene->meshes.empty() ? AABB{} : scene->meshes[0]->bounding_box();
        std::printf("[OK]   %-40s meshes=%zu draws=%zu mats=%zu lods=%zu bb=(%.1f,%.1f,%.1f)..(%.1f,%.1f,%.1f)\n",
                    fs::path(path).filename().string().c_str(),
                    scene->meshes.size(), scene->draw_items.size(),
                    scene->materials.size(), total_lods,
                    bb0.min.x, bb0.min.y, bb0.min.z, bb0.max.x, bb0.max.y, bb0.max.z);

        // Cull the model's draws at the origin with the editor-style camera.
        // A model sitting at the origin must never be fully culled.
        std::vector<DrawItem> draws = scene->draw_items;
        // Normalize: recenter + scale the model to a ~4-unit box at origin so
        // huge city models and tiny props all sit in view identically.
        if (!scene->meshes.empty()) {
            const AABB& bb = scene->meshes[0]->bounding_box();
            const glm::vec3 c = bb.center();
            const float r = glm::max(bb.bounding_radius(), 1e-3f);
            const float s = 2.0f / r;
            const glm::mat4 norm =
                glm::scale(glm::mat4(1.0f), glm::vec3(s)) *
                glm::translate(glm::mat4(1.0f), -c);
            for (auto& d : draws) d.model = norm * d.model;
        }
        CullingStats stats{};
        cull_draw_items_frustum(draws, frustum, &stats);
        const bool all_culled = (stats.input_items > 0 && stats.output_items == 0);
        std::printf("       cull: %u -> %u (culled %u)%s\n",
                    stats.input_items, stats.output_items, stats.culled_frustum,
                    all_culled ? "   <-- FALSE CULL (model is on-screen!)" : "");
        if (all_culled) ++failures;

        // Physics mesh-collider load (.obj): must resolve the SAME file the
        // renderer just loaded — a mismatch = model renders but has no collider.
        std::string low = path; for (char& ch : low) ch = static_cast<char>(::tolower(ch));
        if (low.size() > 4 && low.substr(low.size() - 4) == ".obj") {
            const size_t tris = probe_collider_tris(path);
            if (tris == 0) {
                std::printf("       collider: LOAD FAIL (renders but NO collider!)\n");
                ++failures;
            } else {
                std::printf("       collider: %zu triangles\n", tris);
            }
        }
    }

    // Destroy all loaded scenes (their Vulkan meshes/materials/textures) BEFORE
    // the device — otherwise their destructors run at function-scope exit after
    // shutdown() and hit a dead VkDevice ("Invalid device" loader error).
    cache.clear();
    vkDestroyDescriptorPool(device.get_device(), pool, nullptr);
    vkDestroyDescriptorSetLayout(device.get_device(), layout, nullptr);
    device.shutdown();

    std::printf(failures == 0 ? "model_check: ALL OK\n"
                              : "model_check: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
