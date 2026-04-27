/**
 * @file ui_manager.h
 * @brief Centralized ImGui state management and input routing (Phase 6 Week 21).
 *
 * Provides:
 * - Singleton access to ImGui context and state
 * - Input event dispatch (keyboard, mouse)
 * - UI panel registry (debug panels, inspector, hierarchy, etc.)
 * - Frame synchronization with render graph
 */

#pragma once

#include "imgui_vulkan.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>

namespace gws::renderer::gpu {

class VulkanDevice;
class VulkanRenderGraph;

/**
 * @class UIManager
 * @brief Centralized ImGui management for the engine.
 *
 * Single point of access for:
 * - ImGui frame lifecycle (begin, end, render)
 * - Input event routing (keyboard, mouse, text)
 * - Panel registration and visibility management
 * - Direct ImGui::* access via get_io()
 *
 * Usage:
 * ```cpp
 *   auto ui = UIManager::create(device, render_graph);
 *   ui->register_panel("debug_panel", []() {
 *       ImGui::Begin("Debug");
 *       ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
 *       ImGui::End();
 *   });
 *   ui->show_panel("debug_panel", true);
 *
 *   while (running) {
 *       ui->begin_frame();
 *       ui->draw_panels();
 *       ui->end_frame(cmd);
 *   }
 * ```
 */
class UIManager {
public:
    using PanelDrawFn = std::function<void()>;

    UIManager() = default;
    ~UIManager();

    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;
    UIManager(UIManager&&) = default;
    UIManager& operator=(UIManager&&) = default;

    /// Create the UI manager (must be called once at startup).
    /// Initializes ImGui Vulkan backend and input system.
    /// @param device Vulkan device
    /// @param graph Render graph
    /// @param window Native GLFW window handle (void* to avoid GLFW dependency in header)
    /// @param width Initial window width
    /// @param height Initial window height
    static std::unique_ptr<UIManager> create(VulkanDevice* device,
                                              VulkanRenderGraph* graph,
                                              void* window,
                                              uint32_t width,
                                              uint32_t height);

    /// Begin a new UI frame (call once per frame before drawing panels).
    void begin_frame();

    /// Draw all registered and visible panels.
    void draw_panels();

    /// End the UI frame and record rendering commands.
    /// Must be called after ImGui::Render() or within draw_panels().
    void end_frame(VkCommandBuffer cmd);

    /// Register a UI panel with the given ID and draw function.
    /// The draw function is called each frame if the panel is visible.
    void register_panel(const std::string& id, PanelDrawFn draw_fn);

    /// Show or hide a registered panel.
    void show_panel(const std::string& id, bool visible);

    /// Check if a panel is currently visible.
    bool is_panel_visible(const std::string& id) const;

    /// Unregister a panel (call cleanup function if registered).
    void unregister_panel(const std::string& id);

    /// Route input events
    void on_mouse_move(float x, float y);
    void on_mouse_button(int button, bool pressed);
    void on_mouse_scroll(float delta_x, float delta_y);
    void on_key_press(int key, bool pressed);
    void on_text_input(const char* text);

    /// Direct access to ImGui IO (for manual input handling).
    ImGuiIO& get_io();
    const ImGuiIO& get_io() const;

    /// Check if UI is requesting input capture.
    bool wants_mouse() const;
    bool wants_keyboard() const;

    /// Handle window resize.
    void resize(uint32_t width, uint32_t height);

    /// Get singleton instance (nullptr if not created).
    static UIManager* get();

private:
    struct Panel {
        PanelDrawFn draw_fn;
        bool visible = false;
    };

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::unordered_map<std::string, Panel> panels_;

    explicit UIManager(std::unique_ptr<Impl> impl);
};

} // namespace gws::renderer::gpu
