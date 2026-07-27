// ============================================================================
// canvas.cpp — layout scaling + input/focus routing. See canvas.h.
// ============================================================================

#include "ui/canvas.h"
#include <algorithm>

namespace schizo::ui {

void Canvas::layout(float sw, float sh) {
    w_ = sw; h_ = sh;
    switch (mode_) {
        case ScaleMode::None:       scale_ = 1.0f; break;
        case ScaleMode::MatchWidth: scale_ = sw / reference_.x; break;
        case ScaleMode::MatchHeight:scale_ = sh / reference_.y; break;
        case ScaleMode::Expand:     scale_ = std::min(sw / reference_.x, sh / reference_.y); break;
        case ScaleMode::Shrink:     scale_ = std::max(sw / reference_.x, sh / reference_.y); break;
    }
    // The root IS the screen rect (don't resolve it through its own anchors,
    // which are zero → a 0x0 rect that collapses every stretched child).
    root_.rect = {0, 0, sw, sh};
    for (auto& c : root_.children) c->resolve(root_.rect, scale_);

    focusables_.clear();
    collect_focusables(&root_);
    if (focus_ >= static_cast<int>(focusables_.size())) focus_ = -1;
}

void Canvas::collect_focusables(Widget* w) {
    if (!w->visible) return;
    if (w->interactive && w != &root_) focusables_.push_back(w);
    for (auto& c : w->children) collect_focusables(c.get());
}

UiDrawList Canvas::paint() const {
    UiDrawList dl;
    root_.paint(dl);
    return dl;
}

void Canvas::pointer_move(float x, float y) {
    pointer_ = {x, y};
    Widget* hit = root_.hit_test(x, y);
    if (hit != hovered_) {
        if (hovered_) hovered_->on_hover(false);
        hovered_ = hit;
        if (hovered_) hovered_->on_hover(true);
    }
}

void Canvas::pointer_button(bool down) {
    if (down) {
        pressed_ = hovered_;
        if (pressed_) pressed_->on_press(true);
    } else {
        if (pressed_) {
            pressed_->on_press(false);
            if (pressed_ == hovered_) pressed_->on_activate();
        }
        pressed_ = nullptr;
    }
}

void Canvas::navigate(int dir) {
    if (focusables_.empty()) return;
    if (focus_ >= 0 && focus_ < static_cast<int>(focusables_.size()))
        focusables_[focus_]->on_focus(false);
    const int n = static_cast<int>(focusables_.size());
    if (focus_ < 0) focus_ = (dir >= 0) ? 0 : n - 1;
    else            focus_ = ((focus_ + dir) % n + n) % n;
    focusables_[focus_]->on_focus(true);
}

void Canvas::activate() {
    if (focus_ >= 0 && focus_ < static_cast<int>(focusables_.size()))
        focusables_[focus_]->on_activate();
}

Widget* Canvas::focused() const {
    return (focus_ >= 0 && focus_ < static_cast<int>(focusables_.size()))
               ? focusables_[focus_] : nullptr;
}

} // namespace schizo::ui
