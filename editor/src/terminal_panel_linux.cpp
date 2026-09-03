#include "terminal_panel.h"

#include <imgui.h>

#include <memory>

namespace schizo::editor {

struct TerminalPanel::Impl {};

TerminalPanel::TerminalPanel() : impl_(std::make_unique<Impl>()) {}
TerminalPanel::~TerminalPanel() = default;

void TerminalPanel::Render(bool* open) {
    if (!open || !*open) return;

    if (ImGui::Begin("Terminal", open)) {
        ImGui::TextWrapped(
            "The embedded terminal currently uses Windows ConPTY and is not yet "
            "implemented on Linux. Use your system terminal for now.");
    }
    ImGui::End();
}

} // namespace schizo::editor
