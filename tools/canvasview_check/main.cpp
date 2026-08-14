// ============================================================================
// canvasview_check — the node canvas's coordinate maths.
//
// Everything that makes a node editor feel broken lives in these few lines:
// clicks landing beside the cursor, links attaching to the wrong pin, the graph
// sliding away as you scroll. None of it is visible in a screenshot and all of
// it is arithmetic, so it is exactly the part worth testing rather than eyeing.
//
// The zoom assertions matter most. Zooming about the canvas origin instead of
// the cursor is the single most common way one of these feels wrong: the graph
// drifts as you scroll and you end up chasing it around the screen.
// ============================================================================
#include "node_canvas.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

using namespace schizo::editor;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

bool near(float a, float b, float eps = 1e-3f) { return std::abs(a - b) < eps; }

}  // namespace

int main() {
    std::printf("=== canvasview_check ===\n\n");

    constexpr float OX = 100.0f, OY = 50.0f;      // canvas top-left on screen

    // ---- 1. identity ---------------------------------------------------------
    {
        CanvasView v;
        check(near(v.to_screen_x(0.0f, OX), OX) && near(v.to_screen_y(0.0f, OY), OY),
              "at zoom 1 with no pan, canvas origin maps to the canvas corner");
        check(near(v.to_screen_x(25.0f, OX), OX + 25.0f),
              "and a point 25 units in is 25 pixels in");
    }

    // ---- 2. ROUND TRIP -------------------------------------------------------
    // The property every hit-test depends on: screen -> world -> screen must
    // return the same pixel. If it does not, clicks land somewhere other than
    // where they were made, and the error grows with pan and zoom.
    {
        CanvasView v;
        v.pan_x = -37.5f; v.pan_y = 210.25f; v.zoom = 1.75f;
        for (float sx : {100.0f, 233.0f, 640.0f, 1919.0f}) {
            const float w = v.to_world_x(sx, OX);
            check(near(v.to_screen_x(w, OX), sx),
                  "screen->world->screen returns the same x at " + std::to_string((int)sx));
        }
        const float wy = v.to_world_y(444.0f, OY);
        check(near(v.to_screen_y(wy, OY), 444.0f), "and the same y");
    }

    // ---- 3. ZOOM HOLDS THE POINT UNDER THE CURSOR ----------------------------
    // The assertion this whole struct exists for.
    {
        CanvasView v;
        const float cursor_x = 400.0f, cursor_y = 300.0f;
        const float before_wx = v.to_world_x(cursor_x, OX);
        const float before_wy = v.to_world_y(cursor_y, OY);

        v.zoom_at(cursor_x, cursor_y, OX, OY, 1.1f);

        check(!near(v.zoom, 1.0f), "zooming actually changed the zoom");
        check(near(v.to_world_x(cursor_x, OX), before_wx, 1e-2f),
              "the world point under the cursor is unchanged in x");
        check(near(v.to_world_y(cursor_y, OY), before_wy, 1e-2f),
              "and in y — the graph does not slide away as you scroll");
    }

    // ---- 4. it holds across many steps ---------------------------------------
    // One step could hold by luck; twenty accumulating drift is what a user
    // actually experiences while scrolling.
    {
        CanvasView v;
        const float cx = 512.0f, cy = 384.0f;
        const float wx = v.to_world_x(cx, OX);
        for (int i = 0; i < 20; ++i) v.zoom_at(cx, cy, OX, OY, 1.1f);
        check(near(v.to_world_x(cx, OX), wx, 0.05f),
              "twenty zoom steps leave the cursor over the same point");
    }

    // ---- 5. zoom is clamped ---------------------------------------------------
    // Unbounded zoom-out makes nodes sub-pixel and the graph unrecoverable;
    // unbounded zoom-in overflows the bezier control points.
    {
        CanvasView v;
        for (int i = 0; i < 200; ++i) v.zoom_at(400.0f, 300.0f, OX, OY, 1.1f);
        check(v.zoom <= 3.0f + 1e-4f, "zoom in is clamped");
        for (int i = 0; i < 400; ++i) v.zoom_at(400.0f, 300.0f, OX, OY, 1.0f / 1.1f);
        check(v.zoom >= 0.25f - 1e-4f, "zoom out is clamped");
    }

    // ---- 6. a clamped zoom does not shift the view ----------------------------
    // At the limit the factor is refused; refusing it but still applying the pan
    // correction would drift the graph every time the user kept scrolling.
    {
        CanvasView v;
        for (int i = 0; i < 200; ++i) v.zoom_at(400.0f, 300.0f, OX, OY, 1.1f);
        const float px = v.pan_x, py = v.pan_y;
        v.zoom_at(400.0f, 300.0f, OX, OY, 1.1f);       // already at the ceiling
        check(near(v.pan_x, px) && near(v.pan_y, py),
              "scrolling further at max zoom moves nothing at all");
    }

    // ---- 7. panning is in world units ----------------------------------------
    // Pan must be applied before the zoom scale, or dragging moves the graph a
    // different distance than the mouse at every zoom but 1.
    {
        CanvasView v;
        v.zoom = 2.0f;
        const float sx_before = v.to_screen_x(10.0f, OX);
        v.pan_x += 5.0f;
        check(near(v.to_screen_x(10.0f, OX), sx_before + 10.0f),
              "panning 5 world units at zoom 2 moves 10 pixels");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL canvasview_check\n");
    return g_fail ? 1 : 0;
}
