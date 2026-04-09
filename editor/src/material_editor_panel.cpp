#include "material_editor_panel.h"
#include "entity.h"
#include <imgui.h>
#include <spdlog/spdlog.h>

namespace schizo::editor {

using namespace schizo::scene;
using namespace schizo::renderer;

// PBR Preset definitions
static const struct {
    const char* name;
    glm::vec3 albedo;
    float metallic;
    float roughness;
} PBR_PRESETS[] = {
    {"Gold",        glm::vec3(1.0f, 0.84f, 0.0f),   1.0f, 0.2f},
    {"Copper",      glm::vec3(0.97f, 0.68f, 0.19f), 1.0f, 0.3f},
    {"Steel",       glm::vec3(0.77f, 0.78f, 0.78f), 1.0f, 0.1f},
    {"Aluminum",    glm::vec3(0.91f, 0.92f, 0.92f), 1.0f, 0.2f},
    {"Plastic",     glm::vec3(0.2f, 0.8f, 0.9f),    0.0f, 0.6f},
    {"Rubber",      glm::vec3(0.1f, 0.1f, 0.1f),    0.0f, 0.9f},
    {"Wood",        glm::vec3(0.55f, 0.37f, 0.09f), 0.0f, 0.8f},
    {"Ceramic",     glm::vec3(0.3f, 0.3f, 0.3f),    0.0f, 0.4f},
    {"Fabric",      glm::vec3(0.4f, 0.4f, 0.4f),    0.0f, 0.95f},
};

static constexpr size_t PRESET_COUNT = sizeof(PBR_PRESETS) / sizeof(PBR_PRESETS[0]);

MaterialEditorPanel::MaterialEditorPanel() = default;

MaterialEditorPanel::~MaterialEditorPanel() = default;

void MaterialEditorPanel::Render(const std::shared_ptr<Entity>& entity) {
    if (!entity) {
        ImGui::TextDisabled("No entity selected");
        return;
    }
    
    ImGui::Text("Material Editor - %s", entity->GetName().c_str());
    ImGui::Separator();
    
    // Tabs for different sections
    static int tab = 0;
    if (ImGui::BeginTabBar("MaterialTabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("PBR##mat")) {
            RenderPBRParameters();
            ImGui::EndTabItem();
        }
        
        if (ImGui::BeginTabItem("Textures##mat")) {
            RenderTextureSlots();
            ImGui::EndTabItem();
        }
        
        if (ImGui::BeginTabItem("Presets##mat")) {
            RenderPresets();
            ImGui::EndTabItem();
        }
        
        if (ImGui::BeginTabItem("Preview##mat")) {
            RenderPreview();
            ImGui::EndTabItem();
        }
        
        ImGui::EndTabBar();
    }
    
    ImGui::Separator();
    
    if (ImGui::Button("Apply Material##mat", ImVec2(-1, 0))) {
        ApplyMaterialToEntity(entity);
        spdlog::info("[MaterialEditor] Applied material to entity '{}'", entity->GetName());
    }
}

void MaterialEditorPanel::RenderPBRParameters() {
    bool changed = false;
    
    ImGui::CollapsingHeader("Albedo (Base Color)", ImGuiTreeNodeFlags_DefaultOpen);
    if (ImGui::ColorEdit3("##albedo", &state_.albedo.r)) {
        changed = true;
    }
    
    ImGui::CollapsingHeader("Metallic", ImGuiTreeNodeFlags_DefaultOpen);
    if (ImGui::SliderFloat("##metallic", &state_.metallic, 0.0f, 1.0f)) {
        changed = true;
    }
    ImGui::SameLine();
    ImGui::ProgressBar(state_.metallic, ImVec2(0, 0));
    
    ImGui::CollapsingHeader("Roughness", ImGuiTreeNodeFlags_DefaultOpen);
    if (ImGui::SliderFloat("##roughness", &state_.roughness, 0.0f, 1.0f)) {
        changed = true;
    }
    ImGui::SameLine();
    ImGui::ProgressBar(state_.roughness, ImVec2(0, 0));
    
    ImGui::CollapsingHeader("Ambient Occlusion", ImGuiTreeNodeFlags_DefaultOpen);
    if (ImGui::SliderFloat("##ao", &state_.ambient_occlusion, 0.0f, 1.0f)) {
        changed = true;
    }
    ImGui::SameLine();
    ImGui::ProgressBar(state_.ambient_occlusion, ImVec2(0, 0));
    
    ImGui::CollapsingHeader("Emissive", ImGuiTreeNodeFlags_DefaultOpen);
    if (ImGui::ColorEdit3("##emissive", &state_.emissive.r)) {
        changed = true;
    }
    
    if (changed) {
        spdlog::debug("[MaterialEditor] PBR parameters changed");
    }
}

void MaterialEditorPanel::RenderTextureSlots() {
    ImGui::TextWrapped("Drag texture files here or click to browse");
    ImGui::Separator();
    
    // Texture slot display
    ImGui::InputText("Albedo Texture##tex", state_.albedo_texture.data(), 256, ImGuiInputTextFlags_ReadOnly);
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_ASSET")) {
            spdlog::info("[MaterialEditor] Assigned albedo texture");
        }
        ImGui::EndDragDropTarget();
    }
    
    ImGui::InputText("Normal Texture##tex", state_.normal_texture.data(), 256, ImGuiInputTextFlags_ReadOnly);
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_ASSET")) {
            spdlog::info("[MaterialEditor] Assigned normal texture");
        }
        ImGui::EndDragDropTarget();
    }
    
    ImGui::InputText("Metallic Texture##tex", state_.metallic_texture.data(), 256, ImGuiInputTextFlags_ReadOnly);
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_ASSET")) {
            spdlog::info("[MaterialEditor] Assigned metallic texture");
        }
        ImGui::EndDragDropTarget();
    }
    
    ImGui::InputText("Roughness Texture##tex", state_.roughness_texture.data(), 256, ImGuiInputTextFlags_ReadOnly);
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_ASSET")) {
            spdlog::info("[MaterialEditor] Assigned roughness texture");
        }
        ImGui::EndDragDropTarget();
    }
    
    ImGui::InputText("AO Texture##tex", state_.ao_texture.data(), 256, ImGuiInputTextFlags_ReadOnly);
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_ASSET")) {
            spdlog::info("[MaterialEditor] Assigned AO texture");
        }
        ImGui::EndDragDropTarget();
    }
    
    ImGui::InputText("Emissive Texture##tex", state_.emissive_texture.data(), 256, ImGuiInputTextFlags_ReadOnly);
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_ASSET")) {
            spdlog::info("[MaterialEditor] Assigned emissive texture");
        }
        ImGui::EndDragDropTarget();
    }
}

void MaterialEditorPanel::RenderPresets() {
    ImGui::Text("Select a preset to apply:");
    ImGui::Separator();
    
    static int selected_preset = 0;
    
    for (size_t i = 0; i < PRESET_COUNT; ++i) {
        if (ImGui::Selectable(PBR_PRESETS[i].name, selected_preset == static_cast<int>(i))) {
            selected_preset = i;
            
            // Apply preset
            state_.albedo = PBR_PRESETS[i].albedo;
            state_.metallic = PBR_PRESETS[i].metallic;
            state_.roughness = PBR_PRESETS[i].roughness;
            
            spdlog::info("[MaterialEditor] Applied preset: {}", PBR_PRESETS[i].name);
        }
        
        // Show preview for preset
        ImVec2 color_size(200, 20);
        ImGui::SameLine();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        
        ImU32 col = ImGui::GetColorU32(
            ImVec4(PBR_PRESETS[i].albedo.r, PBR_PRESETS[i].albedo.g, 
                   PBR_PRESETS[i].albedo.b, 1.0f)
        );
        draw_list->AddRectFilled(pos, ImVec2(pos.x + color_size.x, pos.y + color_size.y), col);
    }
}

void MaterialEditorPanel::RenderPreview() {
    ImGui::Text("Material Preview");
    ImGui::Separator();
    
    // Show material color preview
    ImVec2 preview_size(200, 200);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    
    // Draw base color
    ImU32 col = ImGui::GetColorU32(
        ImVec4(state_.albedo.r, state_.albedo.g, state_.albedo.b, 1.0f)
    );
    draw_list->AddRectFilled(pos, ImVec2(pos.x + preview_size.x, pos.y + preview_size.y), col, 4.0f);
    
    // Add grid to show material properties
    ImColor grid_color(100, 100, 100);
    for (int i = 0; i <= 10; ++i) {
        float x = pos.x + (preview_size.x / 10) * i;
        float y_start = pos.y;
        float y_end = pos.y + preview_size.y;
        draw_list->AddLine(ImVec2(x, y_start), ImVec2(x, y_end), grid_color, 0.5f);
        
        float y = pos.y + (preview_size.y / 10) * i;
        float x_start = pos.x;
        float x_end = pos.x + preview_size.x;
        draw_list->AddLine(ImVec2(x_start, y), ImVec2(x_end, y), grid_color, 0.5f);
    }
    
    // Draw border
    ImColor border_color(200, 200, 200);
    draw_list->AddRect(pos, ImVec2(pos.x + preview_size.x, pos.y + preview_size.y), border_color, 4.0f);
    
    ImGui::Dummy(preview_size);
    
    // Material stats
    ImGui::Separator();
    ImGui::Text("Albedo: (%.2f, %.2f, %.2f)", state_.albedo.r, state_.albedo.g, state_.albedo.b);
    ImGui::Text("Metallic: %.2f", state_.metallic);
    ImGui::Text("Roughness: %.2f", state_.roughness);
    ImGui::Text("AO: %.2f", state_.ambient_occlusion);
    ImGui::Text("Emissive: (%.2f, %.2f, %.2f)", state_.emissive.r, state_.emissive.g, state_.emissive.b);
}

void MaterialEditorPanel::ApplyMaterialToEntity(const std::shared_ptr<Entity>& entity) {
    if (!entity) return;
    
    // TODO: Implement actual material assignment to entity
    // This would integrate with the material system and entity components
    spdlog::info("[MaterialEditor] Applying material with albedo ({:.2f}, {:.2f}, {:.2f})",
                 state_.albedo.r, state_.albedo.g, state_.albedo.b);
}

void MaterialEditorPanel::SetMaterialState(const MaterialEditorState& state) {
    state_ = state;
}

} // namespace schizo::editor
