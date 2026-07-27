// ====================
// skinning_check — headless GPU compute-skinning verification (Stage 5)
// ====================
//
// Builds a SkinnedMesh on a real Vulkan device, runs the skinning compute pass
// with known bone matrices, maps the output vertex buffer back, and checks that
// each vertex was transformed by its weighted bones:
//   - a vertex bound 100% to bone 0 (identity) stays put,
//   - a vertex bound 100% to bone 1 (translate +2x) moves +2x,
//   - a vertex split 50/50 lands halfway,
//   - normals are re-transformed and stay unit length.
//
// Run with the toolchain + Vulkan SDK on PATH. Prints ALL OK / exits 0 on pass.

#include "vulkan/vulkan_device.h"
#include "vulkan/vulkan_scene_mesh.h"
#include "vulkan/vulkan_skinned_mesh.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace gws::renderer::gpu;

namespace {
int g_checks = 0;
bool check(bool ok, const char* what) {
    ++g_checks;
    std::printf("  [%s] %s\n", ok ? "OK" : "FAIL", what);
    return ok;
}
#define REQUIRE(c, w) do { if (!check((c), (w))) { \
    std::printf("skinning_check: FAILED at '%s'\n", w); return 1; } } while (0)

SkinnedVertex mk(glm::vec3 p, glm::uvec4 j, glm::vec4 w) {
    SkinnedVertex v; v.position = p; v.normal = glm::vec3(0, 1, 0);
    v.joints = j; v.weights = w; return v;
}
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== skinning_check: GPU compute skinning (Stage 5) ===\n");

    VulkanDevice device;
    RenderConfig cfg{};
    cfg.window_width = 64; cfg.window_height = 64;
    cfg.enable_validation = true; cfg.app_name = "skinning_check";
    try { device.initialize(cfg); }
    catch (const std::exception& e) {
        std::printf("skinning_check: device init failed: %s\n", e.what()); return 1;
    }
    VkDevice vk = device.get_device();

    // 4 verts: bone0-only, bone1-only, 50/50, unbound(weights 0 → identity).
    std::vector<SkinnedVertex> verts = {
        mk({0, 0, 0}, {0, 0, 0, 0}, {1, 0, 0, 0}),   // -> stays
        mk({0, 0, 0}, {1, 0, 0, 0}, {1, 0, 0, 0}),   // -> +2x
        mk({0, 0, 0}, {0, 1, 0, 0}, {0.5f, 0.5f, 0, 0}), // -> +1x
        mk({5, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}),   // -> unchanged (5,0,0)
    };
    std::vector<uint32_t> idx = {0, 1, 2, 0, 2, 3};  // 2 tris (unused, just valid)
    std::vector<glm::mat4> inv_bind = {glm::mat4(1.0f), glm::mat4(1.0f)};

    auto sm = SkinnedMesh::create(&device, verts, idx, {}, inv_bind);
    REQUIRE(sm != nullptr, "SkinnedMesh::create");
    REQUIRE(sm->bone_count() == 2 && sm->vertex_count() == 4, "bone/vertex counts");
    REQUIRE(sm->output_mesh() != nullptr, "output mesh exists");

    // Bone globals: bone0 identity, bone1 = translate +2 on x.
    std::vector<glm::mat4> bones = {
        glm::mat4(1.0f),
        glm::translate(glm::mat4(1.0f), glm::vec3(2, 0, 0)),
    };

    // Record + submit the skinning dispatch on a one-time command buffer.
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = device.get_command_pool();
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(vk, &ai, &cmd);
    VkCommandBufferBeginInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    sm->skin(cmd, bones);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkQueueSubmit(device.get_graphics_queue(), 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(device.get_graphics_queue());
    vkFreeCommandBuffers(vk, device.get_command_pool(), 1, &cmd);

    // Map the output vertex buffer (host-visible) and read the skinned verts.
    std::vector<SceneVertex> out(4);
    void* p = nullptr;
    vkMapMemory(vk, sm->output_mesh()->get_vertex_memory(), 0,
                sizeof(SceneVertex) * 4, 0, &p);
    std::memcpy(out.data(), p, sizeof(SceneVertex) * 4);
    vkUnmapMemory(vk, sm->output_mesh()->get_vertex_memory());

    auto near = [](const glm::vec3& a, const glm::vec3& b, float e = 1e-3f) {
        return glm::length(a - b) < e;
    };
    std::printf("       v0=(%.2f,%.2f,%.2f) v1=(%.2f,%.2f,%.2f) v2=(%.2f,%.2f,%.2f) v3=(%.2f,%.2f,%.2f)\n",
                out[0].position.x, out[0].position.y, out[0].position.z,
                out[1].position.x, out[1].position.y, out[1].position.z,
                out[2].position.x, out[2].position.y, out[2].position.z,
                out[3].position.x, out[3].position.y, out[3].position.z);
    REQUIRE(near(out[0].position, {0, 0, 0}), "bone0-only vertex stays at origin");
    REQUIRE(near(out[1].position, {2, 0, 0}), "bone1-only vertex moves +2x");
    REQUIRE(near(out[2].position, {1, 0, 0}), "50/50 vertex lands halfway (+1x)");
    REQUIRE(near(out[3].position, {5, 0, 0}), "unbound vertex passes through unchanged");
    REQUIRE(std::abs(glm::length(out[0].normal) - 1.0f) < 1e-2f, "normal stays unit length");

    sm.reset();   // free GPU resources BEFORE the device (avoids Invalid-device at exit)
    device.shutdown();
    std::printf("skinning_check: ALL OK (%d checks)\n", g_checks);
    return 0;
}
