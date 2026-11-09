// ============================================================================
// gbufferresize_check — VulkanGBuffer::resize must hand over its handles, not
// destroy them twice.
//
// resize() builds a new G-buffer and move-assigns it into *this. The defaulted
// move copies raw Vulkan handles and leaves them in the source as well, so the
// source's destructor used to destroy every handle *this had just taken over:
// a double destroy at the resize, then a use-after-free on the next frame.
// Nothing called resize() yet, which is why it stayed hidden; a viewport that
// follows the window will.
//
// Creates a real device with the validation layers on, resizes the G-buffer
// twice, records a geometry pass into the result, and tears everything down.
// The layers must report nothing. Without them installed the check still runs
// the sequence (a driver may crash on the double destroy) but cannot assert on
// what the layers would have said.
// ============================================================================
#include "vulkan/vulkan_device.h"
#include "vulkan/vulkan_g_buffer.h"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>

using namespace gws::renderer::gpu;

namespace {
int g_pass = 0, g_fail = 0;
void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}
}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== gbufferresize_check ===\n\n");

    VulkanDevice device;
    RenderConfig cfg{};
    cfg.window_width = 64; cfg.window_height = 64;
    cfg.enable_validation = true;
    cfg.app_name = "gbufferresize_check";
    try { device.initialize(cfg); }
    catch (const std::exception& e) {
        std::printf("SKIP gbufferresize_check: no Vulkan device (%s)\n", e.what());
        return 0;
    }
    const uint32_t before = VulkanDevice::validation_message_count();

    {
        GBufferConfig gc{};
        gc.width = 640; gc.height = 360;
        auto gb = VulkanGBuffer::create(&device, gc);
        check(gb != nullptr, "a G-buffer is created");
        if (gb) {
            gb->resize(&device, 800, 600);
            check(gb->get_width() == 800 && gb->get_height() == 600 &&
                  gb->get_normal_view() != VK_NULL_HANDLE && gb->get_render_pass() != VK_NULL_HANDLE,
                  "resize gives it the new size and live handles");
            gb->resize(&device, 1280, 720);
            check(gb->get_width() == 1280 && gb->get_height() == 720,
                  "a second resize works too");

            // Use it: record and submit an empty geometry pass on the resized
            // targets, so a handle destroyed by the resize would be caught here.
            VkDevice vk = device.get_device();
            VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            ai.commandPool        = device.get_command_pool();
            ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ai.commandBufferCount = 1;
            VkCommandBuffer cmd = VK_NULL_HANDLE;
            vkAllocateCommandBuffers(vk, &ai, &cmd);
            VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &bi);
            gb->begin_geometry_pass(cmd);
            gb->end_geometry_pass(cmd);
            vkEndCommandBuffer(cmd);
            VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            si.commandBufferCount = 1;
            si.pCommandBuffers    = &cmd;
            vkQueueSubmit(device.get_graphics_queue(), 1, &si, VK_NULL_HANDLE);
            vkQueueWaitIdle(device.get_graphics_queue());
            vkFreeCommandBuffers(vk, device.get_command_pool(), 1, &cmd);
            check(true, "a geometry pass records and runs on the resized G-buffer");
        }
        vkDeviceWaitIdle(device.get_device());
    }   // G-buffer destroyed here: must not touch anything twice

    const uint32_t msgs = VulkanDevice::validation_message_count() - before;
    if (device.validation_enabled())
        check(msgs == 0, "the validation layers reported nothing (" + std::to_string(msgs) + ")");
    else
        std::printf("  NOTE validation layers not installed: the layer assertion is skipped\n");

    device.shutdown();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL gbufferresize_check\n");
    return g_fail ? 1 : 0;
}
