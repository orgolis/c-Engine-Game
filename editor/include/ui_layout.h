#pragma once

#include <imgui.h>

#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <string>
#include <utility>

// Shared responsive layout for dockable editor panels. Labels belong above
// property fields, not outside a full-width field at the right-hand edge.
namespace schizo::editor::ui {

inline float AvailableWidth() {
    return std::max(1.0f, ImGui::GetContentRegionAvail().x);
}

inline void SetNextItemWidth(float preferred = -1.0f) {
    const float available = AvailableWidth();
    const float width = preferred < 0.0f ? available + preferred : preferred;
    ImGui::SetNextItemWidth(std::clamp(width, 1.0f, available));
}

inline float ButtonWidth(const char* label) {
    return ImGui::CalcTextSize(label, nullptr, true).x + 2.0f * ImGui::GetStyle().FramePadding.x;
}

inline void SameLineIfFits(float next_width = 0.0f) {
    if (next_width <= 0.0f) next_width = ImGui::GetFontSize() * 10.0f;
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + next_width <= right)
        ImGui::SameLine();
}

inline void SameLineIfFits(const char* next_label) {
    SameLineIfFits(ButtonWidth(next_label));
}

inline void SetNextItemWidthForAction(const char* action) {
    const float available = AvailableWidth();
    const float reserved = ButtonWidth(action) + ImGui::GetStyle().ItemSpacing.x;
    // At tiny widths the action moves to its own line instead of crushing the
    // path field down to a few pixels. Call SameLineIfFits before the action.
    ImGui::SetNextItemWidth(available - reserved >= ImGui::GetFontSize() * 8.0f
        ? available - reserved : available);
}

inline std::string Field(const char* label) {
    const char* end = std::strstr(label, "##");
    if (!end) end = label + std::strlen(label);
    if (end != label) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(label, end);
        ImGui::PopTextWrapPos();
        SetNextItemWidth();
    } else {
        // Anonymous toolbar fields retain the caller's width, but may not
        // exceed the current row. CalcItemWidth also respects pending widths.
        SetNextItemWidth(ImGui::CalcItemWidth());
        return label;
    }
    // ### resets hashing to the original label: move its visible text without
    // changing widget IDs, active edits, undo coalescing or drag/drop targets.
    return std::string("###") + label;
}

inline void Label(const char* text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    SetNextItemWidth();
}

inline bool Checkbox(const char* label, bool* value) {
    const float needed = ImGui::CalcTextSize(label, nullptr, true).x +
        ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
    if (needed <= AvailableWidth()) return ImGui::Checkbox(label, value);
    const std::string id = Field(label);
    return ImGui::Checkbox(id.c_str(), value);
}

template <typename... Args>
inline bool InputTextWithAction(const char* label, const char* action, Args&&... args) {
    const std::string id = Field(label);
    SetNextItemWidthForAction(action);
    return ImGui::InputText(id.c_str(), std::forward<Args>(args)...);
}

#define GWS_UI_FIELD(name) \
    template <typename... Args> \
    inline bool name(const char* label, Args&&... args) { \
        const std::string id = Field(label); \
        return ImGui::name(id.c_str(), std::forward<Args>(args)...); \
    }

GWS_UI_FIELD(InputText)
GWS_UI_FIELD(InputTextWithHint)
GWS_UI_FIELD(InputFloat)
GWS_UI_FIELD(InputFloat2)
GWS_UI_FIELD(InputFloat3)
GWS_UI_FIELD(InputInt)
GWS_UI_FIELD(DragFloat)
GWS_UI_FIELD(DragFloat2)
GWS_UI_FIELD(DragFloat3)
GWS_UI_FIELD(DragFloat4)
GWS_UI_FIELD(DragFloatRange2)
GWS_UI_FIELD(DragInt)
GWS_UI_FIELD(DragInt2)
GWS_UI_FIELD(DragInt3)
GWS_UI_FIELD(SliderFloat)
GWS_UI_FIELD(SliderFloat2)
GWS_UI_FIELD(SliderFloat3)
GWS_UI_FIELD(SliderFloat4)
GWS_UI_FIELD(SliderInt)
GWS_UI_FIELD(ColorEdit3)
GWS_UI_FIELD(ColorEdit4)
GWS_UI_FIELD(Combo)
GWS_UI_FIELD(BeginCombo)
#undef GWS_UI_FIELD

inline bool Button(const char* label, ImVec2 size = ImVec2(0, 0)) {
    const float desired = size.x == 0.0f ? ButtonWidth(label) : size.x;
    size.x = desired < 0.0f ? AvailableWidth() : std::min(desired, AvailableWidth());
    const bool clicked = ImGui::Button(label, size);
    if (desired > size.x && ImGui::IsItemHovered()) {
        const char* end = std::strstr(label, "##");
        ImGui::SetTooltip("%.*s", static_cast<int>(end ? end - label : std::strlen(label)), label);
    }
    return clicked;
}

inline void TextDisabledWrapped(const char* format, ...) IM_FMTARGS(1);
inline void TextDisabledWrapped(const char* format, ...) {
    va_list args;
    va_start(args, format);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrappedV(format, args);
    ImGui::PopStyleColor();
    va_end(args);
}

}  // namespace schizo::editor::ui
