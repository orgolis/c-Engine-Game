/**
 * @file ui_manager.cpp
 * @brief Centralized ImGui state management implementation.
 */

#include "ui_manager.h"
#include <imgui.h>
#include <spdlog/spdlog.h>

namespace gws::renderer::gpu {

static UIManager* g_ui_manager = nullptr;

struct UIManager::Impl {
    std::unique_ptr<ImGuiVulkan> imgui;
    uint32_t width = 0;
    uint32_t height = 0;

    ~Impl() {
        if (imgui) {
            imgui.reset();
        }
    }
};

UIManager::UIManager(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {
    g_ui_manager = this;
}

UIManager::~UIManager() {
    if (g_ui_manager == this) {
        g_ui_manager = nullptr;
    }
}

std::unique_ptr<UIManager> UIManager::create(VulkanDevice* device,
                                              VulkanRenderGraph* graph,
                                              void* window,
                                              uint32_t width,
                                              uint32_t height) {
    if (!device || !graph || !window) {
        spdlog::error("UIManager::create: invalid device, graph, or window");
        return nullptr;
    }

    auto impl = std::make_unique<Impl>();
    impl->imgui = ImGuiVulkan::create(device, graph, window, width, height);
    if (!impl->imgui) {
        spdlog::error("UIManager: Failed to create ImGuiVulkan");
        return nullptr;
    }

    impl->width = width;
    impl->height = height;

    spdlog::info("UIManager: Created successfully ({}x{})", width, height);
    return std::make_unique<UIManager>(std::move(impl));
}

void UIManager::begin_frame() {
    if (!impl_ || !impl_->imgui) return;
    impl_->imgui->begin_frame();
}

void UIManager::draw_panels() {
    for (auto& [id, panel] : panels_) {
        if (panel.visible && panel.draw_fn) {
            panel.draw_fn();
        }
    }
}

void UIManager::end_frame(VkCommandBuffer cmd) {
    if (!impl_ || !impl_->imgui) return;
    impl_->imgui->end_frame(cmd);
}

void UIManager::register_panel(const std::string& id, PanelDrawFn draw_fn) {
    panels_[id] = Panel{draw_fn, false};
    spdlog::debug("UIManager: Registered panel '{}'", id);
}

void UIManager::show_panel(const std::string& id, bool visible) {
    auto it = panels_.find(id);
    if (it != panels_.end()) {
        it->second.visible = visible;
        spdlog::debug("UIManager: Panel '{}' {}", id, visible ? "shown" : "hidden");
    } else {
        spdlog::warn("UIManager: Panel '{}' not found", id);
    }
}

bool UIManager::is_panel_visible(const std::string& id) const {
    auto it = panels_.find(id);
    return it != panels_.end() && it->second.visible;
}

void UIManager::unregister_panel(const std::string& id) {
    panels_.erase(id);
    spdlog::debug("UIManager: Unregistered panel '{}'", id);
}

void UIManager::on_mouse_move(float x, float y) {
    if (!impl_ || !impl_->imgui) return;
    impl_->imgui->on_mouse_move(x, y);
}

void UIManager::on_mouse_button(int button, bool pressed) {
    if (!impl_ || !impl_->imgui) return;
    impl_->imgui->on_mouse_button(button, pressed);
}

void UIManager::on_mouse_scroll(float delta_x, float delta_y) {
    if (!impl_ || !impl_->imgui) return;
    impl_->imgui->on_mouse_scroll(delta_x, delta_y);
}

void UIManager::on_key_press(int key, bool pressed) {
    if (!impl_ || !impl_->imgui) return;
    impl_->imgui->on_key_press(key, pressed);
}

void UIManager::on_text_input(const char* text) {
    if (!impl_ || !impl_->imgui) return;
    impl_->imgui->on_text_input(text);
}

ImGuiIO& UIManager::get_io() {
    static ImGuiIO* dummy_io = nullptr;
    if (ImGui::GetCurrentContext()) {
        return ImGui::GetIO();
    }
    // Fallback - should not reach here in normal usage
    if (!dummy_io) {
        dummy_io = new ImGuiIO();
    }
    return *dummy_io;
}

const ImGuiIO& UIManager::get_io() const {
    if (ImGui::GetCurrentContext()) {
        return ImGui::GetIO();
    }
    // Fallback
    static ImGuiIO dummy_io;
    return dummy_io;
}

bool UIManager::wants_mouse() const {
    if (!impl_ || !impl_->imgui) return false;
    return impl_->imgui->wants_mouse();
}

bool UIManager::wants_keyboard() const {
    if (!impl_ || !impl_->imgui) return false;
    return impl_->imgui->wants_keyboard();
}

void UIManager::resize(uint32_t width, uint32_t height) {
    if (!impl_) return;
    impl_->width = width;
    impl_->height = height;
    if (impl_->imgui) {
        impl_->imgui->resize(width, height);
    }
}

UIManager* UIManager::get() {
    return g_ui_manager;
}

} // namespace gws::renderer::gpu
