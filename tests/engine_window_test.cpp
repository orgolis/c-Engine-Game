/**
 * @file engine_window_test.cpp
 * @brief Phase 4 Week 4: end-to-end frame through the engine.
 *
 * Spawns a GLFW window, creates the engine's `VulkanDevice` + surface +
 * swapchain, builds the full deferred pipeline (G-Buffer with the demo
 * triangle pipeline, lighting pass with one directional light, shadow
 * map, post-processing with bloom + TAA + tone mapping), records a
 * `VulkanRenderGraph` frame, blits the post-processing output onto a
 * swapchain image, and presents.
 *
 * The window stays visible briefly so a human running the test can
 * confirm pixels actually appeared. CI runs it headlessly — the test
 * exits as soon as the present completes.
 */

#include "renderer/gpu/vulkan/vulkan_device.h"
#include "renderer/gpu/vulkan/vulkan_g_buffer.h"
#include "renderer/gpu/vulkan/vulkan_lighting_pass.h"
#include "renderer/gpu/vulkan/vulkan_post_processing.h"
#include "renderer/gpu/vulkan/vulkan_render_graph.h"
#include "renderer/gpu/vulkan/vulkan_shadow_map.h"
#include "renderer/gpu/vulkan/vulkan_swapchain.h"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <chrono>

using namespace gws::renderer::gpu;

namespace {

constexpr uint32_t kWidth  = 640;
constexpr uint32_t kHeight = 480;

void blit_to_swapchain(VkCommandBuffer cmd,
                       VkImage src_image,
                       VkExtent2D src_extent,
                       VkImage swapchain_image,
                       VkExtent2D dst_extent) {
    // Transition src: SHADER_READ_ONLY → TRANSFER_SRC
    VkImageMemoryBarrier src_to_transfer{};
    src_to_transfer.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    src_to_transfer.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    src_to_transfer.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    src_to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_to_transfer.image               = src_image;
    src_to_transfer.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    src_to_transfer.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    src_to_transfer.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    src_to_transfer.subresourceRange.baseMipLevel   = 0;
    src_to_transfer.subresourceRange.levelCount     = 1;
    src_to_transfer.subresourceRange.baseArrayLayer = 0;
    src_to_transfer.subresourceRange.layerCount     = 1;

    // Transition swapchain: UNDEFINED (just acquired) → TRANSFER_DST
    VkImageMemoryBarrier dst_to_transfer{};
    dst_to_transfer.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    dst_to_transfer.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    dst_to_transfer.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    dst_to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dst_to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dst_to_transfer.image               = swapchain_image;
    dst_to_transfer.srcAccessMask       = 0;
    dst_to_transfer.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    dst_to_transfer.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    dst_to_transfer.subresourceRange.baseMipLevel   = 0;
    dst_to_transfer.subresourceRange.levelCount     = 1;
    dst_to_transfer.subresourceRange.baseArrayLayer = 0;
    dst_to_transfer.subresourceRange.layerCount     = 1;

    VkImageMemoryBarrier pre_blit_barriers[] = {src_to_transfer, dst_to_transfer};
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 2, pre_blit_barriers);

    VkImageBlit blit{};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {static_cast<int32_t>(src_extent.width),
                          static_cast<int32_t>(src_extent.height), 1};
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {static_cast<int32_t>(dst_extent.width),
                          static_cast<int32_t>(dst_extent.height), 1};

    vkCmdBlitImage(cmd,
                   src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapchain_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, VK_FILTER_LINEAR);

    // Transition swapchain: TRANSFER_DST → PRESENT_SRC
    VkImageMemoryBarrier dst_to_present{};
    dst_to_present.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    dst_to_present.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    dst_to_present.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    dst_to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dst_to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dst_to_present.image               = swapchain_image;
    dst_to_present.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    dst_to_present.dstAccessMask       = 0;
    dst_to_present.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    dst_to_present.subresourceRange.baseMipLevel   = 0;
    dst_to_present.subresourceRange.levelCount     = 1;
    dst_to_present.subresourceRange.baseArrayLayer = 0;
    dst_to_present.subresourceRange.layerCount     = 1;

    // Restore src to SHADER_READ_ONLY so subsequent passes (none here, but
    // matches the layout the post-processing render pass expects on entry).
    VkImageMemoryBarrier src_to_read{};
    src_to_read.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    src_to_read.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    src_to_read.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    src_to_read.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_to_read.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_to_read.image               = src_image;
    src_to_read.srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    src_to_read.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    src_to_read.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    src_to_read.subresourceRange.baseMipLevel   = 0;
    src_to_read.subresourceRange.levelCount     = 1;
    src_to_read.subresourceRange.baseArrayLayer = 0;
    src_to_read.subresourceRange.layerCount     = 1;

    VkImageMemoryBarrier post_blit_barriers[] = {dst_to_present, src_to_read};
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 2, post_blit_barriers);
}

} // namespace

int main() {
    std::cout << "=== Engine Window Test (end-to-end frame + present) ===\n";

    if (!glfwInit()) {
        std::cerr << "glfwInit failed\n";
        return EXIT_FAILURE;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow* window = glfwCreateWindow(kWidth, kHeight, "engine_window_test", nullptr, nullptr);
    if (!window) {
        std::cerr << "glfwCreateWindow failed\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }

    VulkanDevice device;
    RenderConfig cfg{};
    cfg.window_width      = kWidth;
    cfg.window_height     = kHeight;
    cfg.enable_validation = true;
    cfg.app_name          = "engine_window_test";

    try {
        device.initialize(cfg);
    } catch (const std::exception& e) {
        std::cerr << "VulkanDevice::initialize: " << e.what() << "\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(device.get_vk_instance(), window, nullptr, &surface) != VK_SUCCESS) {
        std::cerr << "glfwCreateWindowSurface failed\n";
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    if (!device.attach_surface(surface) || !device.create_window_swapchain(kWidth, kHeight)) {
        std::cerr << "Surface/swapchain setup failed\n";
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }
    auto* swapchain = device.get_swapchain();
    std::cout << "[OK] Swapchain created: " << swapchain->get_width() << "x"
              << swapchain->get_height() << " (" << swapchain->get_image_count() << " images)\n";

    // Build the deferred pipeline at swapchain dimensions.
    GBufferConfig g_cfg{}; g_cfg.width = kWidth; g_cfg.height = kHeight;
    auto g_buffer = VulkanGBuffer::create(&device, g_cfg);

    LightingConfig l_cfg{};
    auto lighting = VulkanLightingPass::create(&device, l_cfg, g_buffer.get());
    lighting->add_directional_light(glm::vec3(0.3f, -1.0f, 0.2f),
                                    glm::vec3(1.0f, 0.95f, 0.85f), 1.5f, false);

    ShadowMapConfig s_cfg{}; s_cfg.width = 512; s_cfg.height = 512; s_cfg.cascade_count = 1;
    auto shadow_map = VulkanShadowMap::create(&device, s_cfg);

    PostProcessingConfig pp_cfg{};
    pp_cfg.width  = kWidth;
    pp_cfg.height = kHeight;
    pp_cfg.bloom.enabled = true;
    pp_cfg.taa.enabled   = true;
    pp_cfg.tone_mapping.enabled = true;
    auto post_processing = VulkanPostProcessing::create(&device, pp_cfg);
    post_processing->set_input_image(lighting->get_output_view());

    if (!g_buffer || !lighting || !shadow_map || !post_processing) {
        std::cerr << "Component construction failed\n";
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    RenderGraphConfig graph_cfg{};
    graph_cfg.device          = &device;
    graph_cfg.g_buffer        = g_buffer.get();
    graph_cfg.lighting        = lighting.get();
    graph_cfg.shadow_map      = shadow_map.get();
    graph_cfg.post_processing = post_processing.get();
    graph_cfg.width           = kWidth;
    graph_cfg.height          = kHeight;
    auto graph = VulkanRenderGraph::create(graph_cfg);
    if (!graph) {
        std::cerr << "RenderGraph::create failed\n";
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    CameraData cam{};
    cam.position = glm::vec3(0.0f, 0.0f, 2.0f);
    cam.view     = glm::lookAt(cam.position, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    cam.proj     = glm::perspective(glm::radians(60.0f),
                                    static_cast<float>(kWidth) / kHeight, 0.1f, 100.0f);
    cam.proj[1][1] *= -1.0f; // GL → Vulkan Y flip
    graph->set_camera(cam);
    std::cout << "[OK] All components built and camera set\n";

    // ===== One-frame render =====
    VkSemaphore acquire_sem = VK_NULL_HANDLE, render_sem = VK_NULL_HANDLE;
    VkSemaphoreCreateInfo sem_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vkCreateSemaphore(device.get_vk_device(), &sem_info, nullptr, &acquire_sem);
    vkCreateSemaphore(device.get_vk_device(), &sem_info, nullptr, &render_sem);

    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(device.get_vk_device(), &fence_info, nullptr, &fence);

    const uint32_t image_index = swapchain->acquire_next_image(acquire_sem);
    std::cout << "[OK] Acquired swapchain image " << image_index << "\n";

    VkCommandBufferAllocateInfo alloc{};
    alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool        = device.get_command_pool();
    alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device.get_vk_device(), &alloc, &cmd);

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    graph->begin_frame(cmd);
    graph->execute_stage(cmd, RenderGraphStage::Shadow, {});
    graph->execute_stage(cmd, RenderGraphStage::Geometry, {}); // default recorder draws demo triangle
    graph->execute_stage(cmd, RenderGraphStage::Lighting, {});
    graph->execute_stage(cmd, RenderGraphStage::PostProcess, {});
    graph->end_frame(cmd);

    blit_to_swapchain(cmd,
                      post_processing->get_output_vk_image(),
                      {kWidth, kHeight},
                      swapchain->get_image(image_index),
                      {swapchain->get_width(), swapchain->get_height()});

    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &acquire_sem;
    submit.pWaitDstStageMask    = &wait_stage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &render_sem;
    vkQueueSubmit(device.get_graphics_queue(), 1, &submit, fence);

    swapchain->present_image(image_index, render_sem);
    std::cout << "[OK] Frame submitted + presented\n";

    vkWaitForFences(device.get_vk_device(), 1, &fence, VK_TRUE, UINT64_MAX);
    if (graph->resolve_timings()) {
        const auto& s = graph->get_stats();
        std::cout << "[OK] GPU timings (us): shadow=" << s.shadow_us
                  << " geometry=" << s.geometry_us
                  << " lighting=" << s.lighting_us
                  << " post=" << s.post_process_us << "\n";
    }

    // Hold the window briefly so a human watcher can see it.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    device.wait_idle();
    vkFreeCommandBuffers(device.get_vk_device(), device.get_command_pool(), 1, &cmd);
    vkDestroyFence(device.get_vk_device(), fence, nullptr);
    vkDestroySemaphore(device.get_vk_device(), acquire_sem, nullptr);
    vkDestroySemaphore(device.get_vk_device(), render_sem, nullptr);
    graph.reset();
    post_processing.reset();
    shadow_map.reset();
    lighting.reset();
    g_buffer.reset();
    device.shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout << "=== ENGINE WINDOW TEST PASSED ===\n";
    return EXIT_SUCCESS;
}
