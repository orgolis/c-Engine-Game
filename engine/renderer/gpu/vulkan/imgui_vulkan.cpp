/**
 * @file imgui_vulkan.cpp
 * @brief ImGui Vulkan backend implementation.
 */

#include "imgui_vulkan.h"
#include "vulkan_device.h"
#include "vulkan_render_graph.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_impl_glfw.h>
#include <spdlog/spdlog.h>
#include <cstdint>

namespace gws::renderer::gpu {

struct ImGuiVulkan::Impl {
    VulkanDevice* device = nullptr;
    VulkanRenderGraph* graph = nullptr;
    ImGui_ImplVulkanH_Window g_MainWindowData{};
    bool initialized = false;

    ~Impl() {
        if (initialized && device) {
            VkDevice vkdev = device->get_device();
            if (vkdev != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(vkdev);
            }
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }
    }
};

ImGuiVulkan::ImGuiVulkan(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

ImGuiVulkan::~ImGuiVulkan() = default;

std::unique_ptr<ImGuiVulkan> ImGuiVulkan::create(VulkanDevice* device,
                                                   VulkanRenderGraph* graph,
                                                   void* window,
                                                   uint32_t width,
                                                   uint32_t height,
                                                   VkRenderPass render_pass) {
    if (!device || !graph || !window) {
        spdlog::error("ImGuiVulkan::create: invalid device, graph, or window");
        return nullptr;
    }

    auto impl = std::make_unique<Impl>();
    impl->device = device;
    impl->graph = graph;

    // Initialize ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Docking enables the Unity-style tiled editor layout (a DockSpace fills
    // the window; panels tile it, resize via splitters, and refill on close).
    // The vendored imgui is the docking branch, so the flag is available.
    // NOTE: we deliberately do NOT enable ImGuiConfigFlags_ViewportsEnable —
    // that would spawn OS child windows needing a multi-window platform render
    // loop; the editor renders a single Vulkan swapchain.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    // Initialize ImGui platform backend (GLFW)
    if (!ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow*>(window), true)) {
        spdlog::error("ImGuiVulkan: ImGui_ImplGlfw_InitForVulkan failed");
        return nullptr;
    }

    // Initialize ImGui Vulkan backend
    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.Instance = device->get_vk_instance();
    init_info.PhysicalDevice = device->get_physical_device();
    init_info.Device = device->get_device();
    init_info.QueueFamily = device->get_graphics_queue_family();
    init_info.Queue = device->get_graphics_queue();
    init_info.PipelineCache = VK_NULL_HANDLE;  // Optional
    init_info.DescriptorPool = VK_NULL_HANDLE; // We'll create one below
    init_info.MinImageCount = 2;
    init_info.ImageCount = 2;
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = nullptr;
    // ImGui 1.92.x moved Subpass/MSAASamples/RenderPass into PipelineInfoMain.
    init_info.PipelineInfoMain.Subpass     = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.PipelineInfoMain.RenderPass  = render_pass;  // VK_NULL_HANDLE → lazy creation

    // Create descriptor pool for ImGui
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
    };

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device->get_device(), &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
        spdlog::error("ImGuiVulkan: Failed to create descriptor pool");
        return nullptr;
    }

    init_info.DescriptorPool = descriptor_pool;

    if (!ImGui_ImplVulkan_Init(&init_info)) {
        spdlog::error("ImGuiVulkan: ImGui_ImplVulkan_Init failed");
        vkDestroyDescriptorPool(device->get_device(), descriptor_pool, nullptr);
        return nullptr;
    }

    // Note: Font texture upload is deferred - ImGui_ImplVulkan_CreateFontsTexture
    // needs a command buffer which is typically available during rendering.

    impl->initialized = true;
    spdlog::info("ImGuiVulkan: Initialized successfully");

    return std::unique_ptr<ImGuiVulkan>(new ImGuiVulkan(std::move(impl)));
}

void ImGuiVulkan::begin_frame() {
    if (!impl_ || !impl_->initialized) return;

    ImGui_ImplGlfw_NewFrame();
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
}

void ImGuiVulkan::end_frame(VkCommandBuffer cmd) {
    if (!impl_ || !impl_->initialized || cmd == VK_NULL_HANDLE) return;

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

void ImGuiVulkan::resize(uint32_t width, uint32_t height) {
    if (!impl_) return;
    // ImGui handles resize internally
    spdlog::debug("ImGuiVulkan: Resized to {}x{}", width, height);
}

bool ImGuiVulkan::wants_mouse() const {
    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiVulkan::wants_keyboard() const {
    return ImGui::GetIO().WantCaptureKeyboard;
}

void ImGuiVulkan::on_mouse_move(float x, float y) {
    ImGui::GetIO().MousePos = ImVec2(x, y);
}

void ImGuiVulkan::on_mouse_button(int button, bool pressed) {
    if (button >= 0 && button < 5) {
        ImGui::GetIO().MouseDown[button] = pressed;
    }
}

void ImGuiVulkan::on_mouse_scroll(float delta_x, float delta_y) {
    ImGuiIO& io = ImGui::GetIO();
    io.MouseWheelH += delta_x;
    io.MouseWheel += delta_y;
}

void ImGuiVulkan::on_key_press(int /*key*/, bool /*pressed*/) {
    // ImGui ≥1.87 removed `KeysDown[]` in favour of `AddKeyEvent(ImGuiKey, bool)`.
    // The GLFW backend (ImGui_ImplGlfw_InitForVulkan with install_callbacks=true)
    // already wires keyboard events directly into ImGui via its own callbacks,
    // so this passthrough is intentionally a no-op. If the caller needs to
    // route keys manually it should call `ImGui::GetIO().AddKeyEvent(...)`
    // directly with a properly mapped `ImGuiKey`.
}

void ImGuiVulkan::on_text_input(const char* text) {
    if (!text) return;
    ImGui::GetIO().AddInputCharactersUTF8(text);
}

} // namespace gws::renderer::gpu
