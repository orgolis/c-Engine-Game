// ============================================================================
// curve_editor — ImGui widgets for gws::anim::Curve and Gradient.
//
// Evaluation lives in engine/core/anim/curve.h and is unit-tested; this file is
// only the drawing and the dragging, which is the part a test cannot judge.
//
// Both widgets render from the REAL evaluate()/sample() rather than from their
// own idea of the shape. Drawing straight lines between keys would be simpler
// and would show a curve that eases nothing, which is precisely the mistake
// that makes an editor lie about what will happen at runtime.
// ============================================================================
#include "curve_editor.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace schizo::editor {

namespace {

constexpr int   kCurveSamples = 96;   // polyline resolution
constexpr float kKeyRadius    = 4.5f;
constexpr float kStopWidth    = 9.0f;

ImU32 col(float r, float g, float b, float a = 1.0f) {
    return ImGui::GetColorU32(ImVec4(r, g, b, a));
}

}  // namespace

bool draw_curve_editor(const char* label, gws::anim::Curve& curve,
                       float height, int* selected_key) {
    ImGui::PushID(label);
    bool changed = false;

    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + width, p0.y + height);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::InvisibleButton("##canvas", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();

    dl->AddRectFilled(p0, p1, col(0.10f, 0.11f, 0.13f));
    for (int i = 1; i < 4; ++i) {          // quarter grid, for reading values off
        const float fx = p0.x + width  * (i / 4.0f);
        const float fy = p0.y + height * (i / 4.0f);
        dl->AddLine(ImVec2(fx, p0.y), ImVec2(fx, p1.y), col(1, 1, 1, 0.06f));
        dl->AddLine(ImVec2(p0.x, fy), ImVec2(p1.x, fy), col(1, 1, 1, 0.06f));
    }
    dl->AddRect(p0, p1, col(1, 1, 1, 0.18f));

    if (curve.empty()) {
        dl->AddText(ImVec2(p0.x + 8, p0.y + height * 0.5f - 7), col(1, 1, 1, 0.35f),
                    "empty — right-click to add a key");
    }

    // Value range, so a curve of any amplitude fills the box. Padded, because a
    // flat curve would otherwise collapse onto a single row of pixels.
    float vmin = 0.0f, vmax = 1.0f;
    if (!curve.empty()) {
        vmin = vmax = curve.keys().front().value;
        for (const auto& k : curve.keys()) { vmin = std::min(vmin, k.value); vmax = std::max(vmax, k.value); }
        if (vmax - vmin < 1e-3f) { vmin -= 0.5f; vmax += 0.5f; }
        const float pad = (vmax - vmin) * 0.1f;
        vmin -= pad; vmax += pad;
    }
    const float t0 = curve.empty() ? 0.0f : curve.keys().front().time;
    const float t1 = curve.empty() ? 1.0f : curve.keys().back().time;
    const float tspan = (t1 - t0) > 1e-6f ? (t1 - t0) : 1.0f;

    auto to_screen = [&](float t, float v) {
        const float x = p0.x + width * ((t - t0) / tspan);
        const float y = p1.y - height * ((v - vmin) / (vmax - vmin));
        return ImVec2(x, y);
    };

    // The curve itself, sampled through evaluate() so what is drawn is what
    // will be played.
    if (!curve.empty()) {
        ImVec2 pts[kCurveSamples];
        for (int i = 0; i < kCurveSamples; ++i) {
            const float t = t0 + tspan * (i / float(kCurveSamples - 1));
            pts[i] = to_screen(t, curve.evaluate(t));
        }
        dl->AddPolyline(pts, kCurveSamples, col(0.45f, 0.75f, 1.0f), 0, 2.0f);
    }

    // Keys: draggable in both axes.
    for (size_t i = 0; i < curve.keys().size(); ++i) {
        auto& k = curve.keys()[i];
        const ImVec2 c = to_screen(k.time, k.value);
        const bool is_sel = selected_key && *selected_key == static_cast<int>(i);
        dl->AddCircleFilled(c, kKeyRadius, is_sel ? col(1.0f, 0.85f, 0.3f) : col(0.9f, 0.9f, 0.9f));

        const ImVec2 m = ImGui::GetIO().MousePos;
        const bool over = std::abs(m.x - c.x) < 7.0f && std::abs(m.y - c.y) < 7.0f;

        if (hovered && over && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && selected_key)
            *selected_key = static_cast<int>(i);

        if (selected_key && *selected_key == static_cast<int>(i) &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            k.time  += d.x / width  * tspan;
            k.value -= d.y / height * (vmax - vmin);
            changed = true;
        }
    }
    // Sorted after dragging, not during: re-sorting mid-drag would swap the
    // index out from under the mouse and the key being dragged would change.
    if (changed) curve.sort();

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const float t = t0 + tspan * ((m.x - p0.x) / width);
        const float v = vmin + (vmax - vmin) * ((p1.y - m.y) / height);
        curve.add({t, v, 0.0f, 0.0f});
        changed = true;
    }

    if (selected_key && *selected_key >= 0 &&
        *selected_key < static_cast<int>(curve.size())) {
        auto& k = curve.keys()[*selected_key];
        ImGui::SetNextItemWidth(90); changed |= ImGui::DragFloat("t", &k.time, 0.01f);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90); changed |= ImGui::DragFloat("v", &k.value, 0.01f);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70); changed |= ImGui::DragFloat("out", &k.out_tangent, 0.05f);
        ImGui::SameLine();
        if (ImGui::SmallButton("del")) {
            curve.remove(static_cast<size_t>(*selected_key));
            *selected_key = -1;
            changed = true;
        }
    }

    ImGui::PopID();
    return changed;
}

bool draw_gradient_editor(const char* label, gws::anim::Gradient& grad,
                          float height, int* selected_stop) {
    ImGui::PushID(label);
    bool changed = false;

    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + width, p0.y + height);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::InvisibleButton("##ramp", ImVec2(width, height + 10.0f));
    const bool hovered = ImGui::IsItemHovered();

    // A checkerboard first, so alpha is visible. Without it a gradient fading
    // to transparent looks like one fading to black, and the two are very
    // different at runtime.
    const float cs = 6.0f;
    for (float y = p0.y; y < p1.y; y += cs)
        for (float x = p0.x; x < p1.x; x += cs) {
            const bool odd = (int((x - p0.x) / cs) + int((y - p0.y) / cs)) & 1;
            dl->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + cs, p1.x), std::min(y + cs, p1.y)),
                              odd ? col(0.25f, 0.25f, 0.25f) : col(0.35f, 0.35f, 0.35f));
        }

    // The ramp, drawn from sample() in thin columns -- again, what is drawn is
    // what will be used.
    const int cols = std::max(2, static_cast<int>(width));
    for (int i = 0; i < cols; ++i) {
        const float u = i / float(cols - 1);
        const glm::vec4 c = grad.sample(u);
        const float x = p0.x + width * u;
        dl->AddRectFilled(ImVec2(x, p0.y), ImVec2(x + width / cols + 1.0f, p1.y),
                          ImGui::GetColorU32(ImVec4(c.r, c.g, c.b, c.a)));
    }
    dl->AddRect(p0, p1, col(1, 1, 1, 0.25f));

    // Stop markers below the bar.
    for (size_t i = 0; i < grad.stops().size(); ++i) {
        auto& s = grad.stops()[i];
        const float x = p0.x + width * std::clamp(s.time, 0.0f, 1.0f);
        const bool is_sel = selected_stop && *selected_stop == static_cast<int>(i);
        const ImVec2 a(x - kStopWidth * 0.5f, p1.y + 1.0f);
        const ImVec2 b(x + kStopWidth * 0.5f, p1.y + 9.0f);
        dl->AddRectFilled(a, b, ImGui::GetColorU32(ImVec4(s.color.r, s.color.g, s.color.b, 1.0f)));
        dl->AddRect(a, b, is_sel ? col(1.0f, 0.85f, 0.3f) : col(1, 1, 1, 0.5f));

        const ImVec2 m = ImGui::GetIO().MousePos;
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            std::abs(m.x - x) < kStopWidth && m.y > p1.y - 2.0f && selected_stop)
            *selected_stop = static_cast<int>(i);

        if (selected_stop && *selected_stop == static_cast<int>(i) &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            s.time = std::clamp(s.time + ImGui::GetIO().MouseDelta.x / width, 0.0f, 1.0f);
            changed = true;
        }
    }
    if (changed) grad.sort();

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        const float u = std::clamp((ImGui::GetIO().MousePos.x - p0.x) / width, 0.0f, 1.0f);
        grad.add({u, grad.sample(u)});    // the new stop starts at the colour it replaces
        changed = true;
    }

    if (selected_stop && *selected_stop >= 0 &&
        *selected_stop < static_cast<int>(grad.size())) {
        auto& s = grad.stops()[*selected_stop];
        changed |= ImGui::ColorEdit4("##stopcol", &s.color.x,
                                     ImGuiColorEditFlags_AlphaBar |
                                     ImGuiColorEditFlags_AlphaPreviewHalf);
        ImGui::SetNextItemWidth(90);
        changed |= ImGui::DragFloat("pos", &s.time, 0.005f, 0.0f, 1.0f);
        // A gradient needs two stops to be a gradient; removing the last one
        // would leave a sample() that can only return white.
        if (grad.size() > 2) {
            ImGui::SameLine();
            if (ImGui::SmallButton("del stop")) {
                grad.remove(static_cast<size_t>(*selected_stop));
                *selected_stop = -1;
                changed = true;
            }
        }
    }

    ImGui::PopID();
    return changed;
}

}  // namespace schizo::editor
