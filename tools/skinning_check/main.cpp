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
#include <cstdint>

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

    // Read the skinned vertices back through a staging copy.
    //
    // This used to vkMapMemory the output vertex buffer directly, which worked
    // only while mesh geometry lived in HOST_VISIBLE memory. It is DEVICE_LOCAL
    // now (the compute pass writes it and the vertex stage reads it — both on
    // the GPU), so mapping it is invalid and the test segfaulted on the garbage
    // pointer. Device-local memory is read back by copying it to a host buffer,
    // which is what this does.
    const VkDeviceSize readback_size = sizeof(SceneVertex) * 4;
    std::vector<SceneVertex> out(4);
    {
        VkBuffer       host_buf = VK_NULL_HANDLE;
        VkDeviceMemory host_mem = VK_NULL_HANDLE;

        VkBufferCreateInfo hbi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        hbi.size        = readback_size;
        hbi.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        hbi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(vk, &hbi, nullptr, &host_buf) != VK_SUCCESS) {
            std::printf("  FAIL readback buffer creation\n");
            return 1;
        }
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(vk, host_buf, &req);
        VkMemoryAllocateInfo hai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        hai.allocationSize  = req.size;
        hai.memoryTypeIndex = device.find_memory_type(
            req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(vk, &hai, nullptr, &host_mem) != VK_SUCCESS) {
            std::printf("  FAIL readback memory allocation\n");
            return 1;
        }
        vkBindBufferMemory(vk, host_buf, host_mem, 0);

        VkCommandBuffer rc = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo rca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        rca.commandPool        = device.get_command_pool();
        rca.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        rca.commandBufferCount = 1;
        vkAllocateCommandBuffers(vk, &rca, &rc);
        VkCommandBufferBeginInfo rbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        rbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(rc, &rbi);
        VkBufferCopy region{};
        region.size = readback_size;
        vkCmdCopyBuffer(rc, sm->output_mesh()->get_vertex_buffer(), host_buf, 1, &region);
        vkEndCommandBuffer(rc);
        VkSubmitInfo rsi{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        rsi.commandBufferCount = 1;
        rsi.pCommandBuffers    = &rc;
        vkQueueSubmit(device.get_graphics_queue(), 1, &rsi, VK_NULL_HANDLE);
        vkQueueWaitIdle(device.get_graphics_queue());
        vkFreeCommandBuffers(vk, device.get_command_pool(), 1, &rc);

        void* p = nullptr;
        if (vkMapMemory(vk, host_mem, 0, readback_size, 0, &p) != VK_SUCCESS || !p) {
            std::printf("  FAIL readback map\n");
            return 1;
        }
        std::memcpy(out.data(), p, static_cast<size_t>(readback_size));
        vkUnmapMemory(vk, host_mem);
        vkFreeMemory(vk, host_mem, nullptr);
        vkDestroyBuffer(vk, host_buf, nullptr);
    }

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
