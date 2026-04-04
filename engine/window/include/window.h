#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <glm/glm.hpp>

struct GLFWwindow;

namespace schizo::window {

/**
 * @enum KeyCode
 * @brief Keyboard key codes
 */
enum class KeyCode : uint16_t {
    // Special keys
    ESCAPE = 256,
    ENTER,
    TAB,
    BACKSPACE,
    INSERT,
    DELETE,
    RIGHT,
    LEFT,
    DOWN,
    UP,
    
    // Function keys
    F1 = 290,
    F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    
    // Letters and numbers
    A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    NUM_0 = 48, NUM_1, NUM_2, NUM_3, NUM_4, NUM_5, NUM_6, NUM_7, NUM_8, NUM_9,
    
    // Shift/Control/Alt
    LEFT_SHIFT = 340,
    LEFT_CONTROL = 341,
    LEFT_ALT = 342,
    RIGHT_SHIFT = 344,
    RIGHT_CONTROL = 345,
    RIGHT_ALT = 346,
    
    // Spacebar
    SPACE = 32,
    
    UNKNOWN = 0xFFFF
};

/**
 * @enum MouseButton
 * @brief Mouse button codes
 */
enum class MouseButton : uint8_t {
    LEFT = 0,
    RIGHT = 1,
    MIDDLE = 2,
    UNKNOWN = 0xFF
};

/**
 * @enum WindowEvent
 * @brief Types of window events
 */
enum class WindowEvent : uint8_t {
    RESIZED,           // Window was resized
    CLOSED,            // User clicked close
    FOCUS_GAINED,      // Window gained focus
    FOCUS_LOST,        // Window lost focus
    MOVED,             // Window moved
    ICONIFIED,         // Window minimized
    MAXIMIZED,         // Window maximized
    RESTORED,          // Window restored from min/max
};

/**
 * @struct WindowProperties
 * @brief Configuration for window creation
 */
struct WindowProperties {
    uint32_t width = 1280;
    uint32_t height = 720;
    std::string title = "Game Engine";
    bool vsync = true;
    bool fullscreen = false;
    bool resizable = true;
    
    // OpenGL context hints
    int gl_major_version = 4;
    int gl_minor_version = 5;
    int gl_profile = 0; // 0 = Core, 1 = Compat
    bool gl_debug = true;
};

/**
 * @class Window
 * @brief Cross-platform window management
 * 
 * Wraps GLFW and provides a clean interface for window and input handling.
 * This is the primary interface to the windowing system.
 */
class Window {
public:
    /**
     * Create and initialize a new window
     * @param props Window configuration
     * @return Unique pointer to new Window instance
     */
    static std::unique_ptr<Window> Create(const WindowProperties& props);
    
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = default;
    Window& operator=(Window&&) = default;
    
    virtual ~Window() = default;
    
    // Lifecycle
    /**
     * Process pending window events and input
     * Returns false if window should close
     */
    virtual bool Update() = 0;
    
    /**
     * Swap front/back buffers (present rendered frame)
     */
    virtual void SwapBuffers() = 0;
    
    /**
     * Make this window's OpenGL context current
     */
    virtual void MakeCurrent() = 0;
    
    // Properties
    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;
    virtual glm::ivec2 GetSize() const = 0;
    virtual float GetAspectRatio() const = 0;
    virtual bool IsVsyncEnabled() const = 0;
    virtual void SetVsync(bool enabled) = 0;
    
    // Raw GLFW access (for advanced users)
    virtual GLFWwindow* GetNativeHandle() = 0;
    virtual const GLFWwindow* GetNativeHandle() const = 0;
    
    // Input state queries
    /**
     * Check if a key is currently pressed
     */
    virtual bool IsKeyPressed(KeyCode key) const = 0;
    
    /**
     * Check if a mouse button is currently pressed
     */
    virtual bool IsMousePressed(MouseButton button) const = 0;
    
    /**
     * Get current mouse cursor position in window coordinates
     */
    virtual glm::vec2 GetMousePosition() const = 0;
    
    /**
     * Set mouse cursor position
     */
    virtual void SetMousePosition(const glm::vec2& pos) = 0;
    
    /**
     * Show or hide mouse cursor
     */
    virtual void SetMouseVisible(bool visible) = 0;
    
    /**
     * Check if window should close
     */
    virtual bool ShouldClose() const = 0;
    
    /**
     * Request window closure (will close on next Update)
     */
    virtual void RequestClose() = 0;

protected:
    Window() = default;
};

} // namespace schizo::window
