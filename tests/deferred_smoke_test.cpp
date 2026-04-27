/**
 * @file deferred_smoke_test.cpp
 * @brief Phase 4 Week 3 live-GPU smoke test.
 *
 * Constructs the deferred-rendering components (G-Buffer, lighting pass,
 * render graph) against a real VulkanDevice, records and submits a
 * begin_frame/Geometry/Lighting/end_frame command buffer, and waits on a
 * fence. Validates that the components actually instantiate and link against
 * the running Vulkan driver without command-buffer or layout-transition
 * errors.
 *
 * This is intentionally headless: no surface, no swapchain, no presentation.
 * The smoke test is "did the GPU accept what the render graph recorded?".
 */

#include "renderer/gpu/vulkan/vulkan_device.h"
#include "renderer/gpu/vulkan/vulkan_g_buffer.h"
#include "renderer/gpu/vulkan/vulkan_lighting_pass.h"
#include "renderer/gpu/vulkan/vulkan_post_processing.h"
#include "renderer/gpu/vulkan/vulkan_render_graph.h"
#include "renderer/gpu/vulkan/vulkan_shadow_map.h"

#include <vulkan/vulkan.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>

using namespace gws::renderer::gpu;

namespace {

constexpr uint32_t kSmokeWidth  = 320;
constexpr uint32_t kSmokeHeight = 240;

bool record_and_submit(VulkanDevice& device, VulkanRenderGraph& graph) {
    VkDevice vk_device      = device.get_device();
    VkCommandPool pool      = device.get_command_pool();
    VkQueue graphics_queue  = device.get_graphics_queue();

    if (vk_device == VK_NULL_HANDLE || pool == VK_NULL_HANDLE ||
        graphics_queue == VK_NULL_HANDLE) {
        std::cerr << "VulkanDevice not fully initialized\n";
        return false;
    }

    VkCommandBufferAllocateInfo alloc{};
    alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool        = pool;
    alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(vk_device, &alloc, &cmd) != VK_SUCCESS) {
        std::cerr << "Failed to allocate command buffer\n";
        return false;
    }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS) {
        std::cerr << "Failed to begin command buffer\n";
        vkFreeCommandBuffers(vk_device, pool, 1, &cmd);
        return false;
    }

    graph.begin_frame(cmd);
    graph.execute_stage(cmd, RenderGraphStage::Shadow, {});
    graph.execute_stage(cmd, RenderGraphStage::Geometry, {});
    graph.execute_stage(cmd, RenderGraphStage::Lighting, {});
    graph.execute_stage(cmd, RenderGraphStage::PostProcess, {});
    graph.end_frame(cmd);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        std::cerr << "Failed to end command buffer\n";
        vkFreeCommandBuffers(vk_device, pool, 1, &cmd);
        return false;
    }

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(vk_device, &fence_info, nullptr, &fence) != VK_SUCCESS) {
        std::cerr << "Failed to create fence\n";
        vkFreeCommandBuffers(vk_device, pool, 1, &cmd);
        return false;
    }

    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;

    if (vkQueueSubmit(graphics_queue, 1, &submit, fence) != VK_SUCCESS) {
        std::cerr << "Failed to submit command buffer\n";
        vkDestroyFence(vk_device, fence, nullptr);
        vkFreeCommandBuffers(vk_device, pool, 1, &cmd);
        return false;
    }

    constexpr uint64_t kFiveSecondsNs = 5ull * 1000ull * 1000ull * 1000ull;
    if (vkWaitForFences(vk_device, 1, &fence, VK_TRUE, kFiveSecondsNs) != VK_SUCCESS) {
        std::cerr << "Fence wait timed out\n";
        vkDestroyFence(vk_device, fence, nullptr);
        vkFreeCommandBuffers(vk_device, pool, 1, &cmd);
        return false;
    }

    vkDestroyFence(vk_device, fence, nullptr);
    vkFreeCommandBuffers(vk_device, pool, 1, &cmd);
    return true;
}

} // namespace

int main() {
    std::cout << "=== Phase 4 Week 3 Deferred Smoke Test ===\n";

    VulkanDevice device;
    RenderConfig render_config{};
    render_config.window_width      = kSmokeWidth;
    render_config.window_height     = kSmokeHeight;
    render_config.enable_validation = true;
    render_config.app_name          = "DeferredSmokeTest";

    try {
        device.initialize(render_config);
    } catch (const std::exception& e) {
        std::cerr << "VulkanDevice::initialize failed: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    GBufferConfig g_config{};
    g_config.width  = kSmokeWidth;
    g_config.height = kSmokeHeight;

    auto g_buffer = VulkanGBuffer::create(&device, g_config);
    if (!g_buffer) {
        std::cerr << "VulkanGBuffer::create failed\n";
        device.shutdown();
        return EXIT_FAILURE;
    }
    std::cout << "[OK] G-Buffer created (" << g_buffer->get_width() << "x"
              << g_buffer->get_height() << ")\n";

    LightingConfig l_config{};
    auto lighting = VulkanLightingPass::create(&device, l_config, g_buffer.get());
    if (!lighting) {
        std::cerr << "VulkanLightingPass::create failed\n";
        device.shutdown();
        return EXIT_FAILURE;
    }
    lighting->add_directional_light(glm::vec3(0.3f, -1.0f, 0.2f),
                                    glm::vec3(1.0f, 0.95f, 0.85f), 1.5f, false);
    std::cout << "[OK] LightingPass created with " << lighting->get_light_count()
              << " light(s)\n";

    ShadowMapConfig shadow_config{};
    shadow_config.type          = ShadowMapType::Cascaded2D;
    shadow_config.width         = 512;   // small to keep the smoke test fast
    shadow_config.height        = 512;
    shadow_config.cascade_count = 1;     // single cascade — exercises the pass path

    auto shadow_map = VulkanShadowMap::create(&device, shadow_config);
    if (!shadow_map) {
        std::cerr << "VulkanShadowMap::create failed\n";
        device.shutdown();
        return EXIT_FAILURE;
    }
    std::cout << "[OK] ShadowMap created (" << shadow_config.width << "x"
              << shadow_config.height << ", " << shadow_config.cascade_count
              << " cascade)\n";

    PostProcessingConfig pp_config{};
    pp_config.width            = kSmokeWidth;
    pp_config.height           = kSmokeHeight;
    pp_config.bloom.enabled    = true;
    pp_config.taa.enabled      = true;
    pp_config.tone_mapping.enabled = true;

    auto post_processing = VulkanPostProcessing::create(&device, pp_config);
    if (!post_processing) {
        std::cerr << "VulkanPostProcessing::create failed\n";
        device.shutdown();
        return EXIT_FAILURE;
    }
    // Wire post-processing input to the lighting pass's HDR output — the
    // lighting render pass leaves its output image in SHADER_READ_ONLY
    // before bloom/tone mapping read it. (Earlier iterations of this
    // test pointed at post.output_image_ as a self-loop hack; that's no
    // longer needed.)
    post_processing->set_input_image(lighting->get_output_view());
    std::cout << "[OK] PostProcessing created (tone mapping enabled)\n";

    RenderGraphConfig graph_config{};
    graph_config.device          = &device;
    graph_config.g_buffer        = g_buffer.get();
    graph_config.lighting        = lighting.get();
    graph_config.shadow_map      = shadow_map.get();
    graph_config.post_processing = post_processing.get();
    graph_config.width           = kSmokeWidth;
    graph_config.height          = kSmokeHeight;

    auto graph = VulkanRenderGraph::create(graph_config);
    if (!graph) {
        std::cerr << "VulkanRenderGraph::create failed\n";
        device.shutdown();
        return EXIT_FAILURE;
    }

    CameraData cam{};
    cam.position = glm::vec3(0.0f, 0.0f, 2.0f);
    cam.view     = glm::lookAt(cam.position, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    cam.proj     = glm::perspective(glm::radians(60.0f),
                                    static_cast<float>(kSmokeWidth) / kSmokeHeight, 0.1f, 100.0f);
    cam.proj[1][1] *= -1.0f; // GL → Vulkan Y flip
    graph->set_camera(cam);
    std::cout << "[OK] RenderGraph created (camera set)\n";

    if (!record_and_submit(device, *graph)) {
        std::cerr << "GPU submit failed\n";
        graph.reset();
        post_processing.reset();
        shadow_map.reset();
        lighting.reset();
        g_buffer.reset();
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
    } else {
        std::cout << "[--] GPU timings unavailable on this device\n";
    }

    device.wait_idle();
    graph.reset();
    post_processing.reset();
    shadow_map.reset();
    lighting.reset();
    g_buffer.reset();
    device.shutdown();

    std::cout << "=== SMOKE TEST PASSED ===\n";
    return EXIT_SUCCESS;
}
