#pragma once

// ============================================================================
// GameUiDemo — renders a sample game HUD built with the gws_ui framework, using
// ImGui's foreground draw list as the RASTERISER backend (reuses its font;
// swappable for a native Vulkan UI backend later). Demonstrates the runtime UI
// (anchored layout + resolution scaling + draw-list) live in the editor, over
// the viewport. Toggle in the Post-Processing panel. (Game-UI pillar.)
// ============================================================================

#include "ui/canvas.h"
#include "ui/widget.h"

#include <imgui.h>
#include <cmath>
#include <memory>

namespace schizo::editor {

class GameUiDemo {
public:
    GameUiDemo() { build(); }

    void set_enabled(bool e) { enabled_ = e; }
    bool enabled() const { return enabled_; }

    /// Animate the HUD values and render it to ImGui's foreground draw list at
    /// the current display size. Call once per frame inside the ImGui frame.
    void update_and_render(float dt) {
        if (!enabled_) return;
        t_ += dt;

        // Live-ish values so the HUD visibly moves.
        hp_->fraction = 0.55f + 0.35f * std::sin(t_ * 0.6f);
        xp_->fraction = std::fmod(t_ * 0.15f, 1.0f);
        for (int i = 0; i < 3; ++i) {
            const float period = 2.0f + i;
            cd_[i]->fraction = std::fmod(t_, period) / period;   // cooldown sweep
        }

        const ImGuiIO& io = ImGui::GetIO();
        canvas_.layout(io.DisplaySize.x, io.DisplaySize.y);
        render(canvas_.paint(), ImGui::GetForegroundDrawList());
    }

private:
    void build() {
        canvas_.set_reference_resolution({1920, 1080});
        canvas_.set_scale_mode(ui::Canvas::ScaleMode::Expand);
        ui::Widget& root = canvas_.root();

        // Health bar (top-left) + label.
        auto* hp_bg = root.add<ui::Panel>(ui::UiColor{0, 0, 0, 0.45f});
        hp_bg->set_fixed(24, 24, 340, 34, {0, 0});
        hp_ = root.add<ui::ProgressBar>(ui::UiColor{0.85f, 0.15f, 0.15f, 1.0f});
        hp_->set_fixed(28, 28, 332, 26, {0, 0});
        auto* hp_lbl = root.add<ui::Label>("HEALTH", 16.0f, ui::UiColor{1, 1, 1, 0.9f});
        hp_lbl->set_fixed(30, 29, 100, 24, {0, 0});

        // XP bar (stretched across the bottom).
        auto* xp_bg = root.add<ui::Panel>(ui::UiColor{0, 0, 0, 0.4f});
        xp_bg->anchor_min = {0, 1}; xp_bg->anchor_max = {1, 1};
        xp_bg->offset_min = {24, -34}; xp_bg->offset_max = {-24, -20};
        xp_ = root.add<ui::ProgressBar>(ui::UiColor{0.30f, 0.60f, 0.95f, 1.0f});
        xp_->anchor_min = {0, 1}; xp_->anchor_max = {1, 1};
        xp_->offset_min = {26, -32}; xp_->offset_max = {-26, -22};

        // Three ability cooldown pips (bottom-centre).
        for (int i = 0; i < 3; ++i) {
            auto* pip = root.add<ui::ProgressBar>(ui::UiColor{0.95f, 0.80f, 0.25f, 1.0f},
                                                  ui::UiColor{0.05f, 0.05f, 0.05f, 0.8f});
            pip->anchor_min = pip->anchor_max = {0.5f, 1.0f};
            const float x = -90.0f + i * 62.0f;
            pip->offset_min = {x, -110};
            pip->offset_max = {x + 54, -56};
            cd_[i] = pip;
        }
        auto* ab_lbl = root.add<ui::Label>("ABILITIES", 14.0f, ui::UiColor{1, 1, 1, 0.7f},
                                           ui::UiAlign::Center);
        ab_lbl->anchor_min = ab_lbl->anchor_max = {0.5f, 1.0f};
        ab_lbl->offset_min = {-90, -50}; ab_lbl->offset_max = {90, -30};

        // Reticle (centre).
        auto* dot = root.add<ui::Panel>(ui::UiColor{1, 1, 1, 0.8f});
        dot->anchor_min = dot->anchor_max = {0.5f, 0.5f};
        dot->offset_min = {-2, -2}; dot->offset_max = {2, 2};
    }

    static ImU32 col(const ui::UiColor& c) {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a));
    }

    void render(const ui::UiDrawList& dl, ImDrawList* out) {
        for (const ui::UiDrawCmd& c : dl) {
            const ImVec2 mn(c.rect.x, c.rect.y), mx(c.rect.x + c.rect.w, c.rect.y + c.rect.h);
            if (c.type == ui::UiCmdType::Rect || c.type == ui::UiCmdType::Image) {
                out->AddRectFilled(mn, mx, col(c.color), 3.0f);
            } else { // Text
                ImFont* font = ImGui::GetFont();
                const float sz = c.font_size * canvas_.scale();
                const ImVec2 ts = font->CalcTextSizeA(sz, FLT_MAX, 0.0f, c.text.c_str());
                float tx = c.rect.x + 4.0f;
                if (c.align == ui::UiAlign::Center) tx = c.rect.cx() - ts.x * 0.5f;
                else if (c.align == ui::UiAlign::Right) tx = c.rect.x + c.rect.w - ts.x - 4.0f;
                const float ty = c.rect.cy() - ts.y * 0.5f;
                out->AddText(font, sz, ImVec2(tx, ty), col(c.color), c.text.c_str());
            }
        }
    }

    ui::Canvas canvas_;
    ui::ProgressBar* hp_ = nullptr;
    ui::ProgressBar* xp_ = nullptr;
    ui::ProgressBar* cd_[3] = {nullptr, nullptr, nullptr};
    float t_ = 0.0f;
    bool  enabled_ = false;
};

} // namespace schizo::editor
