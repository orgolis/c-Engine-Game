// ============================================================================
// command_palette (UI) — see command_palette.h for the registry and ranking.
//
// The ranking lives in the header and is unit-tested; this file is only the
// window, which is the part a test cannot judge anyway.
// ============================================================================
#include "command_palette.h"

#include "entity.h"
#include "scene.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace schizo::editor {

namespace {

// Persistent across frames — only one palette exists.
char  g_query[128] = "";
int   g_selected   = 0;
bool  g_focus_next = false;

struct Row {
    std::string label;
    std::string detail;      // category or "Entity"
    std::string shortcut;
    std::function<void()> run;
};

}  // namespace

void open_command_palette(bool& open) {
    open           = true;
    g_query[0]     = '\0';
    g_selected     = 0;
    g_focus_next   = true;
}

bool draw_command_palette(bool& open, const CommandRegistry& registry,
                          const std::shared_ptr<schizo::scene::Scene>& scene,
                          const std::function<void(uint32_t)>& select_entity) {
    if (!open) return false;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->GetCenter().x, vp->Pos.y + vp->Size.y * 0.18f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Always);

    bool ran = false;
    if (ImGui::Begin("##command_palette", &open,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_AlwaysAutoResize)) {

        if (g_focus_next) { ImGui::SetKeyboardFocusHere(); g_focus_next = false; }
        ImGui::SetNextItemWidth(-1.0f);
        const bool submitted = ImGui::InputTextWithHint(
            "##q", "Search commands and entities…", g_query, sizeof g_query,
            ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::Separator();

        // ---- gather rows: commands first, then entities ---------------------
        std::vector<Row> rows;
        for (const Command* c : registry.search(g_query, 10))
            rows.push_back({c->title, c->category, c->shortcut, c->run});

        // Entities are searched with the same matcher as commands, so a scene
        // with a "Guard" is reachable by typing "gu" exactly like a command is.
        // Without this the palette answers half the question and users learn to
        // go back to the hierarchy panel.
        if (scene && select_entity) {
            std::vector<std::pair<int, schizo::scene::Entity*>> hits;
            for (const auto& e : scene->GetEntities()) {
                if (!e) continue;
                const int s = command_score(g_query, e->GetName());
                if (s > 0) hits.push_back({s, e.get()});
            }
            std::stable_sort(hits.begin(), hits.end(),
                             [](const auto& a, const auto& b) { return a.first > b.first; });
            for (size_t i = 0; i < hits.size() && i < 6; ++i) {
                const uint32_t id = hits[i].second->GetId();
                rows.push_back({hits[i].second->GetName(), "Entity", "",
                                [select_entity, id] { select_entity(id); }});
            }
        }

        if (rows.empty()) {
            ImGui::TextDisabled("  no matches");
        } else {
            g_selected = std::clamp(g_selected, 0, static_cast<int>(rows.size()) - 1);

            // Arrow keys move the selection; the input keeps focus throughout,
            // so typing and choosing never need a mouse.
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
                g_selected = (g_selected + 1) % static_cast<int>(rows.size());
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
                g_selected = (g_selected + static_cast<int>(rows.size()) - 1) %
                             static_cast<int>(rows.size());

            for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
                const Row& r = rows[i];
                if (ImGui::Selectable((r.label + "##row" + std::to_string(i)).c_str(),
                                      i == g_selected)) {
                    g_selected = i;
                    if (r.run) r.run();
                    ran  = true;
                    open = false;
                }
                ImGui::SameLine(360.0f);
                ImGui::TextDisabled("%s", r.detail.c_str());
                if (!r.shortcut.empty()) {
                    ImGui::SameLine(470.0f);
                    ImGui::TextDisabled("%s", r.shortcut.c_str());
                }
            }

            if (submitted && g_selected < static_cast<int>(rows.size())) {
                if (rows[g_selected].run) rows[g_selected].run();
                ran  = true;
                open = false;
            }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) open = false;
    }
    ImGui::End();
    return ran;
}

}  // namespace schizo::editor
