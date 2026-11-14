#pragma once

// ============================================================================
// Game UI framework — retained widget tree with Unity-style anchored layout and
// a backend-agnostic draw-command list. (Game-UI pillar.) This is the RUNTIME
// game UI (HUD / menus / inventory), separate from the editor's dev-only ImGui.
//
// Layout model (RectTransform): each widget has anchorMin/anchorMax (fractions
// of the parent rect) + offsetMin/offsetMax (pixels, scaled by the canvas DPI
// scale). anchorMin==anchorMax → fixed-size anchored corner; anchor 0..1 →
// stretch with pixel insets. Widgets paint into a UiDrawList (rects / text /
// images) that a GPU backend renders. Pure CPU / headless-testable.
// ============================================================================

#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace schizo::ui {

struct UiRect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
};

struct UiColor {
    float r = 1, g = 1, b = 1, a = 1;
};

enum class UiCmdType : uint8_t { Rect, Text, Image };
enum class UiAlign   : uint8_t { Left, Center, Right };

struct UiDrawCmd {
    UiCmdType   type = UiCmdType::Rect;
    UiRect      rect;
    UiColor     color;
    std::string text;                 // Text
    float       font_size = 18.0f;    // Text
    UiAlign     align = UiAlign::Left;
    uint64_t    texture = 0;          // Image (asset/texture id)
    // Looks (all optional; the defaults draw as before).
    float       rounding = -1.0f;     // Rect: corner radius in reference px (-1: the renderer's own)
    UiColor     border{0, 0, 0, 0};   // Rect: an outline of this colour ...
    float       border_width = 0.0f;  // ... this wide (reference px)
    bool        drop = false;         // Rect: a soft shadow under it
    bool        bold = false;         // Text: the bold face
    bool        shadow = false;       // Text: a dark shadow under it (reads over anything)
};

/// How a box looks besides its colour: rounded corners, an outline, a shadow.
struct UiBoxStyle {
    float   rounding = -1.0f;
    UiColor border{0, 0, 0, 0};
    float   border_width = 0.0f;
    bool    drop = false;
    void apply(UiDrawCmd& c) const { c.rounding = rounding; c.border = border; c.border_width = border_width; c.drop = drop; }
};
using UiDrawList = std::vector<UiDrawCmd>;

// ----------------------------------------------------------------------------

class Widget {
public:
    virtual ~Widget() = default;

    // Layout inputs.
    glm::vec2 anchor_min{0, 0}, anchor_max{0, 0};
    glm::vec2 offset_min{0, 0}, offset_max{0, 0};
    bool visible     = true;
    bool interactive = false;

    UiRect rect;                        // resolved screen rect (post-layout)
    std::vector<std::unique_ptr<Widget>> children;

    /// Add a child of type T, forwarding ctor args; returns a raw pointer.
    template <class T, class... A>
    T* add(A&&... args) {
        auto p = std::make_unique<T>(std::forward<A>(args)...);
        T* raw = p.get();
        children.push_back(std::move(p));
        return raw;
    }

    // --- layout helpers ---
    /// Fixed-size box anchored to a parent corner (default top-left).
    Widget& set_fixed(float px, float py, float w, float h,
                      glm::vec2 anchor = {0, 0}) {
        anchor_min = anchor_max = anchor;
        offset_min = {px, py};
        offset_max = {px + w, py + h};
        return *this;
    }
    /// Stretch to fill the parent with pixel insets (l,t,r,b).
    Widget& set_stretch(float l = 0, float t = 0, float r = 0, float b = 0) {
        anchor_min = {0, 0}; anchor_max = {1, 1};
        offset_min = {l, t}; offset_max = {-r, -b};
        return *this;
    }

    /// Compute `rect` from the parent rect + DPI `scale`, then recurse.
    void resolve(const UiRect& parent, float scale) {
        const float xmin = parent.x + anchor_min.x * parent.w + offset_min.x * scale;
        const float ymin = parent.y + anchor_min.y * parent.h + offset_min.y * scale;
        const float xmax = parent.x + anchor_max.x * parent.w + offset_max.x * scale;
        const float ymax = parent.y + anchor_max.y * parent.h + offset_max.y * scale;
        rect = {xmin, ymin, xmax - xmin, ymax - ymin};
        for (auto& c : children) c->resolve(rect, scale);
    }

    /// Append this widget's + descendants' draw commands (front-to-back).
    void paint(UiDrawList& dl) const {
        if (!visible) return;
        paint_self(dl);
        for (const auto& c : children) c->paint(dl);
    }

    /// Topmost interactive widget under (px,py) — children (reverse = on top)
    /// first, then self.
    Widget* hit_test(float px, float py) {
        if (!visible) return nullptr;
        for (auto it = children.rbegin(); it != children.rend(); ++it)
            if (Widget* h = (*it)->hit_test(px, py)) return h;
        return (interactive && rect.contains(px, py)) ? this : nullptr;
    }

    // Interaction hooks (Canvas drives these on the hit widget).
    virtual void paint_self(UiDrawList&) const {}
    virtual void on_hover(bool) {}
    virtual void on_pointer(float, float) {}   // where the pointer is, while over this widget
    virtual void on_press(bool) {}
    virtual void on_focus(bool) {}
    virtual void on_activate() {}
};

// ---- Concrete widgets ------------------------------------------------------

class Panel : public Widget {
public:
    explicit Panel(UiColor c = {0, 0, 0, 0.5f}) : color(c) {}
    UiColor color;
    UiBoxStyle box;
    void paint_self(UiDrawList& dl) const override {
        UiDrawCmd c{UiCmdType::Rect, rect, color, {}, 0.0f, UiAlign::Left, 0};
        box.apply(c);
        dl.push_back(c);
    }
};

class Image : public Widget {
public:
    Image(uint64_t tex, UiColor tint = {1, 1, 1, 1}) : texture(tex), tint(tint) {}
    uint64_t texture; UiColor tint;
    void paint_self(UiDrawList& dl) const override {
        dl.push_back({UiCmdType::Image, rect, tint, {}, 0.0f, UiAlign::Left, texture});
    }
};

class Label : public Widget {
public:
    Label(std::string t, float size = 18.0f, UiColor c = {1, 1, 1, 1},
          UiAlign a = UiAlign::Left)
        : text(std::move(t)), font_size(size), color(c), align(a) {}
    std::string text; float font_size; UiColor color; UiAlign align;
    bool bold = false, shadow = false;
    void paint_self(UiDrawList& dl) const override {
        UiDrawCmd c{UiCmdType::Text, rect, color, text, font_size, align, 0};
        c.bold = bold;
        c.shadow = shadow;
        dl.push_back(c);
    }
};

/// Horizontal fill bar (health / cooldown / XP). `fraction` in [0,1].
class ProgressBar : public Widget {
public:
    ProgressBar(UiColor fill, UiColor bg = {0, 0, 0, 0.6f})
        : fill(fill), bg(bg) {}
    float fraction = 1.0f;
    UiColor fill, bg;
    UiBoxStyle box;
    void paint_self(UiDrawList& dl) const override {
        UiDrawCmd b{UiCmdType::Rect, rect, bg, {}, 0.0f, UiAlign::Left, 0};
        box.apply(b);
        b.border_width = 0.0f;
        dl.push_back(b);
        UiRect fr = rect;
        fr.w = rect.w * glm::clamp(fraction, 0.0f, 1.0f);
        if (fr.w > 0.5f) {
            UiDrawCmd f{UiCmdType::Rect, fr, fill, {}, 0.0f, UiAlign::Left, 0};
            f.rounding = box.rounding;
            dl.push_back(f);
        }
        if (box.border_width > 0.0f && box.border.a > 0.0f) {   // the outline over both
            UiDrawCmd o{UiCmdType::Rect, rect, UiColor{0, 0, 0, 0}, {}, 0.0f, UiAlign::Left, 0};
            o.rounding = box.rounding;
            o.border = box.border;
            o.border_width = box.border_width;
            dl.push_back(o);
        }
    }
};

class Button : public Widget {
public:
    enum class State { Normal, Hover, Pressed, Disabled };
    explicit Button(std::string label, std::function<void()> on_click = {})
        : text(std::move(label)), on_click(std::move(on_click)) { interactive = true; }

    std::string text;
    std::function<void()> on_click;
    UiColor color_normal{0.20f, 0.22f, 0.28f, 0.95f};
    UiColor color_hover {0.30f, 0.34f, 0.42f, 0.95f};
    UiColor color_press {0.14f, 0.16f, 0.20f, 0.95f};
    UiColor color_disabled{0.15f, 0.15f, 0.15f, 0.6f};
    UiColor text_color{1, 1, 1, 1};
    float font_size = 18.0f;
    UiBoxStyle box;
    UiColor border_hover{0, 0, 0, 0};   // the outline while hovered (none: as `box.border`)
    bool bold = false, shadow = false;
    State state = State::Normal;

    void set_enabled(bool e) {
        interactive = e;
        state = e ? State::Normal : State::Disabled;
    }

    void on_hover(bool h) override {
        if (state == State::Disabled) return;
        if (state != State::Pressed) state = h ? State::Hover : State::Normal;
        hovered_ = h;
    }
    void on_press(bool p) override {
        if (state == State::Disabled) return;
        state = p ? State::Pressed : (hovered_ || focused_ ? State::Hover : State::Normal);
    }
    void on_focus(bool f) override { focused_ = f; }
    void on_activate() override { if (interactive && on_click) on_click(); }

    void paint_self(UiDrawList& dl) const override {
        UiColor c = color_normal;
        switch (state) {
            case State::Hover:    c = color_hover; break;
            case State::Pressed:  c = color_press; break;
            case State::Disabled: c = color_disabled; break;
            default: c = (focused_ ? color_hover : color_normal); break;
        }
        UiDrawCmd bg{UiCmdType::Rect, rect, c, {}, 0.0f, UiAlign::Left, 0};
        box.apply(bg);
        if ((state == State::Hover || state == State::Pressed || focused_) && border_hover.a > 0.0f) {
            bg.border = border_hover;
            if (bg.border_width <= 0.0f) bg.border_width = 2.0f;
        }
        dl.push_back(bg);
        UiDrawCmd tx{UiCmdType::Text, rect, text_color, text, font_size, UiAlign::Center, 0};
        tx.bold = bold;
        tx.shadow = shadow;
        if (state == State::Disabled) tx.color.a *= 0.45f;
        dl.push_back(tx);
    }

private:
    bool hovered_ = false;
    bool focused_ = false;
};

} // namespace schizo::ui
