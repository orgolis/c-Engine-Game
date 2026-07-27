#pragma once

// ============================================================================
// Canvas — the root of a game-UI screen. Owns the widget tree, resolves layout
// at the current resolution with a reference-resolution DPI scale, produces a
// UiDrawList, and routes pointer + gamepad/keyboard-focus input. (Game-UI.)
// ============================================================================

#include "ui/widget.h"
#include <glm/glm.hpp>
#include <vector>

namespace schizo::ui {

class Canvas {
public:
    /// How pixel offsets scale to keep the UI consistent across resolutions,
    /// relative to `reference_resolution`.
    enum class ScaleMode { None, MatchWidth, MatchHeight, Expand, Shrink };

    Canvas() { root_.visible = true; }

    Widget& root() { return root_; }
    void set_reference_resolution(glm::vec2 r) { reference_ = r; }
    void set_scale_mode(ScaleMode m) { mode_ = m; }
    float scale() const { return scale_; }

    /// Resolve the tree over the [0,0,w,h] screen and rebuild the focus order.
    void layout(float screen_w, float screen_h);

    /// Draw commands for the current layout (front-to-back).
    UiDrawList paint() const;

    // --- pointer input ---
    void pointer_move(float x, float y);
    /// Press/release at the last pointer position. A press+release over the
    /// same interactive widget fires its activation.
    void pointer_button(bool down);

    // --- focus (gamepad / keyboard) ---
    void navigate(int dir);   // +1 = next, -1 = previous (wraps)
    void activate();          // fire the focused widget
    Widget* focused() const;

private:
    void collect_focusables(Widget* w);

    Widget    root_;
    glm::vec2 reference_{1920.0f, 1080.0f};
    ScaleMode mode_ = ScaleMode::Expand;
    float     scale_ = 1.0f;
    float     w_ = 0.0f, h_ = 0.0f;

    glm::vec2 pointer_{0.0f};
    Widget*   hovered_ = nullptr;
    Widget*   pressed_ = nullptr;

    std::vector<Widget*> focusables_;
    int focus_ = -1;
};

} // namespace schizo::ui
