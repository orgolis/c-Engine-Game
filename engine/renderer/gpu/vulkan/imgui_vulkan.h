/**
 * @file imgui_vulkan.h
 * @brief ImGui integration for Vulkan renderer (Phase 6 Week 21).
 *
 * Wraps ImGui's Vulkan backend with our render graph and handles:
 * - Font texture creation & descriptor set
 * - ImGui rendering pipeline setup
 * - Frame recording and command buffer integration
 * - Input event forwarding (keyboard, mouse)
 */

#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <glm/glm.hpp>

namespace gws::renderer::gpu {

class VulkanDevice;
class VulkanRenderGraph;

/**
 * @class ImGuiVulkan
 * @brief Manages ImGui rendering on Vulkan backend.
 *
 * Integrates Dear ImGui with our deferred pipeline:
 * - One-time setup: initialize font texture, descriptor set
 * - Per-frame: ImGui::NewFrame(), user code builds UI, ImGui::Render()
 * - Recording: convert ImGui draw list to Vulkan commands
 *
 * Usage:
 * ```cpp
 *   auto imgui = ImGuiVulkan::create(device, render_graph);
 *   while (running) {
 *       imgui->begin_frame(io);
 *       ImGui::SetNextWindowPos(...);
 *       ImGui::Begin("Debug");
 *       ImGui::Text("Hello, ImGui!");
 *       ImGui::End();
 *       imgui->end_frame(cmd);  // Records ImGui draw commands
 *   }
 * ```
 */
class ImGuiVulkan {
public:
    ImGuiVulkan() = default;
    ~ImGuiVulkan();

    ImGuiVulkan(const ImGuiVulkan&) = delete;
    ImGuiVulkan& operator=(const ImGuiVulkan&) = delete;
    ImGuiVulkan(ImGuiVulkan&&) = default;
    ImGuiVulkan& operator=(ImGuiVulkan&&) = default;

    /// Create ImGui integration for Vulkan rendering.
    /// Initializes Dear ImGui, Vulkan backend, and font atlas.
    /// Must be called once before using ImGui.
    /// @param device Vulkan device for rendering
    /// @param graph Render graph for integration
    /// @param window Native GLFW window handle for input handling
    /// @param width Initial window width
    /// @param height Initial window height
    /// @param render_pass VkRenderPass ImGui will render into (swapchain overlay pass)
    static std::unique_ptr<ImGuiVulkan> create(VulkanDevice* device,
                                                VulkanRenderGraph* graph,
                                                void* window,  // GLFWwindow*
                                                uint32_t width,
                                                uint32_t height,
                                                VkRenderPass render_pass = VK_NULL_HANDLE);

    /// Prepare ImGui for the new frame (call once per frame before UI code).
    /// Integrates with the supplied IO structure for input handling.
    void begin_frame();

    /// Finalize ImGui frame and record UI rendering commands into cmd buffer.
    /// Call after ImGui::Render() (automatically called by ImGui).
    void end_frame(VkCommandBuffer cmd);

    /// Resize internal render targets (called when window resizes).
    void resize(uint32_t width, uint32_t height);

    /// Check if ImGui wants to capture mouse input (UI is absorbing input).
    bool wants_mouse() const;

    /// Check if ImGui wants to capture keyboard input.
    bool wants_keyboard() const;

    /// Forward a mouse position event to ImGui.
    void on_mouse_move(float x, float y);

    /// Forward a mouse button event to ImGui.
    void on_mouse_button(int button, bool pressed);

    /// Forward a mouse scroll event to ImGui.
    void on_mouse_scroll(float delta_x, float delta_y);

    /// Forward a keyboard key event to ImGui.
    void on_key_press(int key, bool pressed);

    /// Forward a text input event to ImGui.
    void on_text_input(const char* text);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    explicit ImGuiVulkan(std::unique_ptr<Impl> impl);
};

} // namespace gws::renderer::gpu
