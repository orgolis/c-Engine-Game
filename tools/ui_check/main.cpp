// ====================
// ui_check — headless verification of the game-UI framework (Game-UI pillar)
// ====================
//
//   1. Anchored layout: fixed corner box, full stretch with insets, centered
//      anchor — resolved rects are exact.
//   2. Resolution-independent scaling: the same UI at 1080p and 4K scales pixel
//      offsets by the DPI factor.
//   3. Pointer input: hover tracking, click fires only on press+release over
//      the same button, release-outside does NOT fire, disabled ignores input.
//   4. Focus navigation: next/prev wraps; activate fires the focused button.
//   5. Draw list: a HUD (health bar + label + button) emits the expected commands.
//
// Pure CPU. Prints "ui_check: ALL OK" + exits 0 on pass.

#include "ui/widget.h"
#include "ui/canvas.h"

#include <cmath>
#include <cstdio>

using namespace schizo::ui;

namespace {
int g_checks = 0;
bool check(bool ok, const char* what) {
    ++g_checks;
    std::printf("  [%s] %s\n", ok ? "OK" : "FAIL", what);
    return ok;
}
#define REQUIRE(c, w) do { if (!check((c), (w))) { \
    std::printf("ui_check: FAILED at '%s'\n", w); return 1; } } while (0)
bool near(float a, float b, float e = 0.01f) { return std::abs(a - b) < e; }
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== ui_check: game-UI framework ===\n");

    // ---- 1. Anchored layout (no scaling) ----------------------------------
    {
        Canvas ui;
        ui.set_scale_mode(Canvas::ScaleMode::None);
        auto* corner  = ui.root().add<Panel>();
        corner->set_fixed(10, 20, 100, 30, glm::vec2(0, 0));       // top-left box
        auto* stretch = ui.root().add<Panel>();
        stretch->set_stretch(16, 16, 16, 16);                      // fill - 16px inset
        auto* centered = ui.root().add<Panel>();
        centered->anchor_min = centered->anchor_max = glm::vec2(0.5f, 0.5f);
        centered->offset_min = {-50, -25}; centered->offset_max = {50, 25};

        ui.layout(800, 600);
        REQUIRE(near(corner->rect.x, 10) && near(corner->rect.y, 20) &&
                near(corner->rect.w, 100) && near(corner->rect.h, 30),
                "fixed corner box resolves to exact rect");
        REQUIRE(near(stretch->rect.x, 16) && near(stretch->rect.y, 16) &&
                near(stretch->rect.w, 800 - 32) && near(stretch->rect.h, 600 - 32),
                "stretch box fills parent minus insets");
        REQUIRE(near(centered->rect.x, 400 - 50) && near(centered->rect.y, 300 - 25) &&
                near(centered->rect.w, 100) && near(centered->rect.h, 50),
                "centered anchor box is centred with fixed size");
    }

    // ---- 2. Resolution-independent scaling --------------------------------
    {
        Canvas ui;
        ui.set_reference_resolution({1920, 1080});
        ui.set_scale_mode(Canvas::ScaleMode::MatchHeight);
        auto* box = ui.root().add<Panel>();
        box->set_fixed(0, 0, 200, 100, glm::vec2(0, 0));

        ui.layout(1920, 1080);
        REQUIRE(near(ui.scale(), 1.0f) && near(box->rect.w, 200),
                "at reference resolution scale is 1x");
        ui.layout(3840, 2160);   // 4K → 2x height
        REQUIRE(near(ui.scale(), 2.0f) && near(box->rect.w, 400) && near(box->rect.h, 200),
                "at 4K the box scales 2x (resolution-independent)");
    }

    // ---- 3. Pointer input + click semantics -------------------------------
    {
        Canvas ui;
        ui.set_scale_mode(Canvas::ScaleMode::None);
        int clicks = 0;
        auto* btn = ui.root().add<Button>("Play", [&] { ++clicks; });
        btn->set_fixed(100, 100, 120, 40, glm::vec2(0, 0));   // rect [100..220,100..140]
        ui.layout(800, 600);

        ui.pointer_move(160, 120);   // over the button
        REQUIRE(btn->state == Button::State::Hover, "pointer over button → hover");
        ui.pointer_button(true);
        REQUIRE(btn->state == Button::State::Pressed, "press → pressed state");
        ui.pointer_button(false);
        REQUIRE(clicks == 1 && btn->state == Button::State::Hover,
                "press+release inside → one click, back to hover");

        // Press inside, release outside → no click.
        ui.pointer_button(true);
        ui.pointer_move(400, 400);   // off the button
        ui.pointer_button(false);
        REQUIRE(clicks == 1, "release outside → no click");

        // Move off → normal.
        REQUIRE(btn->state == Button::State::Normal, "pointer left button → normal");

        // Disabled button ignores clicks.
        btn->set_enabled(false);
        ui.pointer_move(160, 120);
        ui.pointer_button(true); ui.pointer_button(false);
        REQUIRE(clicks == 1 && btn->state == Button::State::Disabled,
                "disabled button ignores input");
    }

    // ---- 4. Focus navigation ----------------------------------------------
    {
        Canvas ui;
        ui.set_scale_mode(Canvas::ScaleMode::None);
        int a = 0, b = 0, c = 0;
        auto* ba = ui.root().add<Button>("A", [&] { ++a; }); ba->set_fixed(0, 0, 80, 30);
        auto* bb = ui.root().add<Button>("B", [&] { ++b; }); bb->set_fixed(0, 40, 80, 30);
        auto* bc = ui.root().add<Button>("C", [&] { ++c; }); bc->set_fixed(0, 80, 80, 30);
        ui.layout(800, 600);

        ui.navigate(+1);                       // focus A
        REQUIRE(ui.focused() == ba, "navigate(+1) focuses first button");
        ui.navigate(+1); ui.navigate(+1);      // C
        REQUIRE(ui.focused() == bc, "navigate advances to C");
        ui.navigate(+1);                       // wraps to A
        REQUIRE(ui.focused() == ba, "navigate wraps to A");
        ui.navigate(-1);                       // wraps back to C
        REQUIRE(ui.focused() == bc, "navigate(-1) wraps to C");
        ui.activate();
        REQUIRE(c == 1 && a == 0 && b == 0, "activate fires only the focused button");
    }

    // ---- 5. HUD draw list -------------------------------------------------
    {
        Canvas ui;
        ui.set_scale_mode(Canvas::ScaleMode::None);
        auto* hp = ui.root().add<ProgressBar>(UiColor{0.8f, 0.1f, 0.1f, 1});
        hp->set_fixed(20, 20, 300, 24); hp->fraction = 0.5f;
        auto* lbl = ui.root().add<Label>("HP", 20.0f);
        lbl->set_fixed(20, 50, 100, 24);
        auto* btn = ui.root().add<Button>("Menu");
        btn->set_fixed(700, 20, 80, 30);
        ui.layout(800, 600);

        const UiDrawList dl = ui.paint();
        int rects = 0, texts = 0;
        float fill_w = -1.0f;
        for (const auto& c : dl) {
            if (c.type == UiCmdType::Rect) ++rects;
            if (c.type == UiCmdType::Text) ++texts;
        }
        // ProgressBar = bg+fill (2 rects); Label = 1 text; Button = 1 rect + 1 text.
        REQUIRE(rects == 3, "HUD emits 3 rects (bar bg+fill, button bg)");
        REQUIRE(texts == 2, "HUD emits 2 text cmds (label + button caption)");
        // The fill rect is half the bar width.
        for (const auto& c : dl)
            if (c.type == UiCmdType::Rect && near(c.rect.w, 150.0f)) fill_w = c.rect.w;
        REQUIRE(near(fill_w, 150.0f), "health bar fill is 50% width (150 of 300)");
    }

    std::printf("ui_check: ALL OK (%d checks)\n", g_checks);
    return 0;
}
