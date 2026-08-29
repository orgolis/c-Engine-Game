#include "document_bar.h"

#include <imgui.h>

namespace schizo::editor {

void draw_document_bar(DocumentFile& doc, const char* type_label) {
    if (doc.path.empty()) {
        // The honest state, said plainly. Before 4.10 this was the *permanent*
        // state of every one of these documents and nothing said so -- the
        // panel looked exactly the same whether the work was being kept or
        // thrown away on close.
        ImGui::TextDisabled("Unsaved %s - create one from the Asset Browser's New menu, "
                            "then double-click it to edit here.", type_label);
        ImGui::Separator();
        return;
    }

    ImGui::BeginDisabled(!doc.dirty);
    if (ImGui::Button("Save")) doc.save_requested = true;
    ImGui::EndDisabled();

    ImGui::SameLine();
    // The asterisk is the whole point of tracking dirty: without it there is no
    // way to tell a saved document from one with unsaved edits, and the failure
    // is discovered by closing the editor.
    ImGui::Text("%s%s", doc.path.c_str(), doc.dirty ? " *" : "");
    if (!doc.status.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", doc.status.c_str());
    }
    ImGui::Separator();
}

}  // namespace schizo::editor
