// ============================================================================
// The Extensions window (Phase 4.8).
//
// Kept apart from editor_extensions.cpp so the loader stays ImGui-free and
// testable. This file is the reachable path: an extension system with no window
// is a folder people have to be told about, and the whole lesson of v0.7.15 is
// that a feature nobody can navigate to is not shipped.
// ============================================================================

#include "editor_extensions.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace schizo::editor {

void ShowExtensionsPanel(bool* open, ExtensionSystem& sys, CommandRegistry& cmds,
                         const ScriptApi& api) {
    if (!*open) return;

    ImGui::SetNextWindowSize(ImVec2(560, 380), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Extensions", open)) { ImGui::End(); return; }

    ImGui::TextDisabled("Editor scripts in %s", sys.dir().string().c_str());
    ImGui::Text("%zu extension(s), %zu command(s)", sys.extensions().size(), sys.total_commands());
    if (sys.failed_count() > 0) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "- %zu failing", sys.failed_count());
    }

    if (ImGui::Button("Reload All")) sys.reload_all(cmds, api);
    ImGui::SameLine();
    if (ImGui::Button("New Extension...")) {
        // Numbered rather than overwriting: the template refuses to clobber, and
        // a button that silently does nothing the second time is worse than one
        // that makes a second file.
        std::string err;
        for (int i = 0; i < 100; ++i) {
            char name[64];
            if (i == 0) std::snprintf(name, sizeof name, "my_extension.py");
            else        std::snprintf(name, sizeof name, "my_extension_%d.py", i);
            const fs::path p = sys.dir() / name;
            if (ExtensionSystem::write_template(p, &err)) {
                spdlog::info("[ext] wrote starter extension {}", p.string());
                sys.reload_all(cmds, api);
                break;
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Open Folder")) {
        std::error_code ec;
        fs::create_directories(sys.dir(), ec);
#ifdef _WIN32
        const std::string cmd = "explorer \"" + sys.dir().string() + "\"";
        std::system(cmd.c_str());
#endif
    }

    ImGui::Separator();

    if (sys.extensions().empty()) {
        ImGui::TextWrapped(
            "No editor extensions yet.\n\n"
            "An extension is an ordinary script that does \"import engine\" and, from its "
            "on_start(e) hook, calls engine.register_command(title, category, token). Its "
            "commands appear here and in "
            "the command palette (Ctrl+P), and the file reloads whenever you save it.\n\n"
            "Press \"New Extension...\" for a working starting point.");
        ImGui::End();
        return;
    }

    for (size_t i = 0; i < sys.extensions().size(); ++i) {
        const LoadedExtension& e = sys.extensions()[i];
        ImGui::PushID(static_cast<int>(i));

        const bool healthy = e.error.empty();
        if (!healthy) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.45f, 1.0f));
        const bool openNode = ImGui::TreeNodeEx(
            e.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen, "%s  (%s)  -  %zu command(s)%s",
            e.name.c_str(), e.language.empty() ? "?" : e.language.c_str(),
            e.commands.size(), healthy ? "" : "  [FAILED]");
        if (!healthy) ImGui::PopStyleColor();

        if (openNode) {
            if (!healthy) {
                // The error text is the whole point of the panel: a script that
                // fails to compile otherwise just does not appear, and the author
                // has no idea whether the editor even saw the file.
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.45f, 1.0f), "%s", e.error.c_str());
                ImGui::PopTextWrapPos();
            }
            for (const std::string& title : e.commands) {
                ImGui::Bullet();
                ImGui::TextUnformatted(title.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton(("Run##" + title).c_str())) cmds.run_by_title(title);
            }
            if (e.commands.empty() && healthy)
                ImGui::TextDisabled("loaded, but registered no commands");

            if (ImGui::SmallButton("Reload")) sys.reload(i, cmds, api);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", e.path.c_str());
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::End();
}

}  // namespace schizo::editor
