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
#include <algorithm>
#include <cstdint>
#include <filesystem>

namespace gws::renderer::gpu {

namespace {

// A calm, high-contrast editor theme shared by the launcher and every docked
// tool.  Dear ImGui's stock dark theme is intentionally generic; in a large
// editor it makes panels, fields and selected rows blend into one grey sheet.
// The blue/cyan accent is reserved for focus and primary actions, while warm
// colours remain available for warnings and destructive actions inside panels.
void apply_worldshaper_theme(GLFWwindow* window) {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding     = ImVec2(12.0f, 10.0f);
    style.FramePadding      = ImVec2(9.0f, 6.0f);
    style.CellPadding       = ImVec2(8.0f, 5.0f);
    style.ItemSpacing       = ImVec2(8.0f, 7.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 5.0f);
    style.TouchExtraPadding = ImVec2(1.0f, 1.0f);
    style.IndentSpacing     = 18.0f;
    style.ScrollbarSize     = 13.0f;
    style.GrabMinSize       = 10.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize  = 1.0f;
    style.PopupBorderSize  = 1.0f;
    style.FrameBorderSize  = 0.0f;
    style.TabBorderSize    = 0.0f;

    style.WindowRounding    = 7.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 5.0f;
    style.PopupRounding     = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding      = 5.0f;
    style.TabRounding       = 5.0f;

    constexpr ImVec4 text       {0.91f, 0.94f, 0.97f, 1.00f};
    constexpr ImVec4 text_dim   {0.52f, 0.59f, 0.67f, 1.00f};
    constexpr ImVec4 canvas     {0.035f, 0.047f, 0.066f, 1.00f};
    constexpr ImVec4 panel      {0.055f, 0.071f, 0.096f, 1.00f};
    constexpr ImVec4 raised     {0.078f, 0.102f, 0.137f, 1.00f};
    constexpr ImVec4 border     {0.15f, 0.20f, 0.27f, 1.00f};
    constexpr ImVec4 accent     {0.10f, 0.68f, 0.82f, 1.00f};
    constexpr ImVec4 accent_hot {0.16f, 0.78f, 0.91f, 1.00f};

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                  = text;
    c[ImGuiCol_TextDisabled]          = text_dim;
    c[ImGuiCol_WindowBg]              = canvas;
    c[ImGuiCol_ChildBg]               = panel;
    c[ImGuiCol_PopupBg]               = ImVec4(0.045f, 0.060f, 0.083f, 0.98f);
    c[ImGuiCol_Border]                = border;
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]               = raised;
    c[ImGuiCol_FrameBgHovered]        = ImVec4(0.11f, 0.17f, 0.22f, 1.00f);
    c[ImGuiCol_FrameBgActive]         = ImVec4(0.12f, 0.22f, 0.28f, 1.00f);
    c[ImGuiCol_TitleBg]               = panel;
    c[ImGuiCol_TitleBgActive]         = ImVec4(0.065f, 0.088f, 0.120f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]      = panel;
    c[ImGuiCol_MenuBarBg]             = ImVec4(0.047f, 0.063f, 0.086f, 1.00f);
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0.025f, 0.034f, 0.048f, 0.75f);
    c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.20f, 0.26f, 0.33f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.27f, 0.35f, 0.43f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.34f, 0.44f, 0.52f, 1.00f);
    c[ImGuiCol_CheckMark]             = accent_hot;
    c[ImGuiCol_SliderGrab]            = accent;
    c[ImGuiCol_SliderGrabActive]      = accent_hot;
    c[ImGuiCol_Button]                = ImVec4(0.09f, 0.35f, 0.43f, 1.00f);
    c[ImGuiCol_ButtonHovered]         = ImVec4(0.10f, 0.53f, 0.64f, 1.00f);
    c[ImGuiCol_ButtonActive]          = ImVec4(0.08f, 0.62f, 0.74f, 1.00f);
    c[ImGuiCol_Header]                = ImVec4(0.09f, 0.31f, 0.38f, 0.82f);
    c[ImGuiCol_HeaderHovered]         = ImVec4(0.11f, 0.43f, 0.52f, 0.92f);
    c[ImGuiCol_HeaderActive]          = ImVec4(0.10f, 0.55f, 0.66f, 1.00f);
    c[ImGuiCol_Separator]             = border;
    c[ImGuiCol_SeparatorHovered]      = accent;
    c[ImGuiCol_SeparatorActive]       = accent_hot;
    c[ImGuiCol_ResizeGrip]            = ImVec4(accent.x, accent.y, accent.z, 0.20f);
    c[ImGuiCol_ResizeGripHovered]     = ImVec4(accent.x, accent.y, accent.z, 0.67f);
    c[ImGuiCol_ResizeGripActive]      = accent_hot;
    c[ImGuiCol_Tab]                   = raised;
    c[ImGuiCol_TabHovered]            = ImVec4(0.10f, 0.42f, 0.51f, 1.00f);
    c[ImGuiCol_TabSelected]           = ImVec4(0.08f, 0.30f, 0.37f, 1.00f);
    c[ImGuiCol_TabSelectedOverline]   = accent_hot;
    c[ImGuiCol_TabDimmed]             = panel;
    c[ImGuiCol_TabDimmedSelected]     = raised;
    c[ImGuiCol_DockingPreview]        = ImVec4(accent.x, accent.y, accent.z, 0.55f);
    c[ImGuiCol_DockingEmptyBg]        = canvas;
    c[ImGuiCol_PlotLines]             = ImVec4(0.35f, 0.69f, 0.78f, 1.00f);
    c[ImGuiCol_PlotHistogram]         = ImVec4(0.97f, 0.70f, 0.28f, 1.00f);
    c[ImGuiCol_TableHeaderBg]         = raised;
    c[ImGuiCol_TableBorderStrong]     = border;
    c[ImGuiCol_TableBorderLight]      = ImVec4(0.11f, 0.15f, 0.20f, 1.00f);
    c[ImGuiCol_TableRowBgAlt]         = ImVec4(1, 1, 1, 0.018f);
    c[ImGuiCol_TextSelectedBg]        = ImVec4(accent.x, accent.y, accent.z, 0.34f);
    c[ImGuiCol_DragDropTarget]        = ImVec4(0.99f, 0.77f, 0.24f, 0.95f);
    c[ImGuiCol_NavCursor]             = accent_hot;
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.01f, 0.02f, 0.03f, 0.72f);

    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    const float ui_scale = std::clamp(std::max(xscale, yscale), 1.0f, 1.75f);
    if (ui_scale > 1.01f) style.ScaleAllSizes(ui_scale);

    // The built-in Proggy font is useful for samples, but looks pixelated in a
    // full desktop editor. Prefer the native UI font on Windows and a common
    // sans-serif on Linux; if neither exists ImGui keeps its safe default.
    ImGuiIO& io = ImGui::GetIO();
    const char* font_candidates[] = {
#ifdef _WIN32
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
#else
        "/usr/share/fonts/TTF/Inter-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
#endif
    };
    for (const char* candidate : font_candidates) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(candidate, ec)) continue;
        if (ImFont* font = io.Fonts->AddFontFromFileTTF(candidate, 15.5f * ui_scale)) {
            io.FontDefault = font;
            break;
        }
    }
}

}  // namespace

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
    // Docking provides Unity-style tab stacks and split regions. Viewports let
    // a tab be dragged out of the main window into its own native OS window.
    // Both the vendored GLFW and Vulkan backends provide viewport support.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();
    apply_worldshaper_theme(static_cast<GLFWwindow*>(window));
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        // Detached windows should visually match the main editor window.
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

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

void ImGuiVulkan::render_platform_windows() {
    if (!impl_ || !impl_->initialized) return;
    if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) return;
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
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
