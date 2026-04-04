#include "window.h"
#include <spdlog/spdlog.h>
#include <GLFW/glfw3.h>

namespace schizo::window {

/**
 * @class WindowImpl
 * @brief GLFW-based implementation of Window
 */
class WindowImpl : public Window {
public:
    WindowImpl(const WindowProperties& props);
    ~WindowImpl() override;
    
    bool Initialize();
    
    bool Update() override;
    void SwapBuffers() override;
    void MakeCurrent() override;
    
    uint32_t GetWidth() const override { return width_; }
    uint32_t GetHeight() const override { return height_; }
    glm::ivec2 GetSize() const override { return glm::ivec2(width_, height_); }
    float GetAspectRatio() const override;
    bool IsVsyncEnabled() const override { return vsync_; }
    void SetVsync(bool enabled) override;
    
    GLFWwindow* GetNativeHandle() override { return window_; }
    const GLFWwindow* GetNativeHandle() const override { return window_; }
    
    bool IsKeyPressed(KeyCode key) const override;
    bool IsMousePressed(MouseButton button) const override;
    glm::vec2 GetMousePosition() const override;
    void SetMousePosition(const glm::vec2& pos) override;
    void SetMouseVisible(bool visible) override;
    
    bool ShouldClose() const override;
    void RequestClose() override;

private:
    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void CursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    
    GLFWwindow* window_ = nullptr;
    uint32_t width_;
    uint32_t height_;
    std::string title_;
    bool vsync_;
    bool should_close_ = false;
    glm::vec2 mouse_pos_ = glm::vec2(0.0f);
};

WindowImpl::WindowImpl(const WindowProperties& props)
    : width_(props.width), height_(props.height), title_(props.title), vsync_(props.vsync) {
}

WindowImpl::~WindowImpl() {
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

bool WindowImpl::Initialize() {
    // Initialize GLFW if not already done
    if (!glfwInit()) {
        spdlog::error("Failed to initialize GLFW");
        return false;
    }
    
    // Set GLFW hints for OpenGL context
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    
    // Create window
    window_ = glfwCreateWindow(width_, height_, title_.c_str(), nullptr, nullptr);
    if (!window_) {
        spdlog::error("Failed to create GLFW window");
        glfwTerminate();
        return false;
    }
    
    spdlog::info("Window created: {}x{} - {}", width_, height_, title_);
    
    // Make OpenGL context current
    glfwMakeContextCurrent(window_);
    
    // Set window user pointer to retrieve 'this' in callbacks
    glfwSetWindowUserPointer(window_, this);
    
    // Register callbacks
    glfwSetFramebufferSizeCallback(window_, FramebufferSizeCallback);
    glfwSetKeyCallback(window_, KeyCallback);
    glfwSetMouseButtonCallback(window_, MouseButtonCallback);
    glfwSetCursorPosCallback(window_, CursorPosCallback);
    
    // Set vsync
    glfwSwapInterval(vsync_ ? 1 : 0);
    
    return true;
}

bool WindowImpl::Update() {
    glfwPollEvents();
    
    if (glfwWindowShouldClose(window_)) {
        should_close_ = true;
    }
    
    return !should_close_;
}

void WindowImpl::SwapBuffers() {
    if (window_) {
        glfwSwapBuffers(window_);
    }
}

void WindowImpl::MakeCurrent() {
    if (window_) {
        glfwMakeContextCurrent(window_);
    }
}

float WindowImpl::GetAspectRatio() const {
    return static_cast<float>(width_) / static_cast<float>(height_);
}

void WindowImpl::SetVsync(bool enabled) {
    vsync_ = enabled;
    glfwSwapInterval(enabled ? 1 : 0);
}

bool WindowImpl::IsKeyPressed(KeyCode key) const {
    // TODO: Query GLFW key state
    (void)key;  // Suppress unused parameter warning
    return false;
}

bool WindowImpl::IsMousePressed(MouseButton button) const {
    // TODO: Query GLFW mouse button state
    (void)button;  // Suppress unused parameter warning
    return false;
}

glm::vec2 WindowImpl::GetMousePosition() const {
    return mouse_pos_;
}

void WindowImpl::SetMousePosition(const glm::vec2& pos) {
    // TODO: Use glfwSetCursorPos
    (void)pos;  // Suppress unused parameter warning
}

void WindowImpl::SetMouseVisible(bool visible) {
    // TODO: Use glfwSetInputMode with GLFW_CURSOR
    (void)visible;  // Suppress unused parameter warning
}

bool WindowImpl::ShouldClose() const {
    return should_close_;
}

void WindowImpl::RequestClose() {
    should_close_ = true;
}

// Static callbacks
void WindowImpl::FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    auto* win = static_cast<WindowImpl*>(glfwGetWindowUserPointer(window));
    if (win) {
        win->width_ = width;
        win->height_ = height;
        spdlog::debug("Window resized to {}x{}", width, height);
    }
}

void WindowImpl::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    // TODO: Implement key event handling
    (void)window;
    (void)key;
    (void)scancode;
    (void)action;
    (void)mods;
}

void WindowImpl::MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    // TODO: Implement mouse button event handling
    (void)window;
    (void)button;
    (void)action;
    (void)mods;
}

void WindowImpl::CursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    auto* win = static_cast<WindowImpl*>(glfwGetWindowUserPointer(window));
    if (win) {
        win->mouse_pos_ = glm::vec2(xpos, ypos);
    }
}

// Factory function
std::unique_ptr<Window> Window::Create(const WindowProperties& props) {
    auto window = std::make_unique<WindowImpl>(props);
    if (!window->Initialize()) {
        spdlog::error("Failed to initialize window");
        return nullptr;
    }
    return window;
}

} // namespace schizo::window
