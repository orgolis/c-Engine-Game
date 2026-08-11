#pragma once
// ============================================================================
// curve_editor — ImGui widgets for gws::anim::Curve and Gradient (4.5).
//
// Small, used constantly. Their absence is why values that vary over time end
// up as magic numbers in data files: an effect's colour is a start and an end
// because there is no type or widget that can say "this, then this, then that".
//
// Both widgets draw from the real evaluate()/sample(), so the editor cannot
// show a shape the runtime will not produce.
// ============================================================================

#include "anim/curve.h"

namespace schizo::editor {

/// Curve editor. Left-drag moves a key, right-click adds one. `selected_key`
/// persists the selection across frames; pass nullptr for a read-only plot.
/// Returns true if the curve changed.
bool draw_curve_editor(const char* label, gws::anim::Curve& curve,
                       float height = 120.0f, int* selected_key = nullptr);

/// Gradient editor. Left-drag moves a stop, right-click adds one at the colour
/// it replaces. Alpha is drawn over a checkerboard: without it a gradient
/// fading to transparent looks exactly like one fading to black, and the two
/// behave very differently. Returns true if the gradient changed.
bool draw_gradient_editor(const char* label, gws::anim::Gradient& grad,
                          float height = 22.0f, int* selected_stop = nullptr);

}  // namespace schizo::editor
