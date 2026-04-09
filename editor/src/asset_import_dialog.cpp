#include "asset_import_dialog.h"
#include <imgui.h>
#include <spdlog/spdlog.h>

namespace schizo::editor {

AssetImportDialog::AssetImportDialog() = default;

AssetImportDialog::~AssetImportDialog() = default;

void AssetImportDialog::ShowImportDialog(const std::string& asset_path, const std::string& asset_type) {
    current_asset_path_ = asset_path;
    current_asset_type_ = asset_type;
    is_open_ = true;
    confirmed_ = false;
    
    // Reset to defaults
    settings_ = AssetImportSettings();
    
    spdlog::info("[AssetImportDialog] Opening import dialog for: {} ({})", asset_path, asset_type);
}

bool AssetImportDialog::RenderDialog() {
    if (!is_open_) return false;
    
    bool result = false;
    
    if (ImGui::Begin("Import Asset Settings", &is_open_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Asset: %s", current_asset_path_.c_str());
        ImGui::Text("Type: %s", current_asset_type_.c_str());
        ImGui::Separator();
        
        // General Settings
        if (ImGui::CollapsingHeader("General Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Scale##import", &settings_.scale, 0.1f, 10.0f);
            ImGui::Checkbox("Generate Normals##import", &settings_.generate_normals);
            ImGui::Checkbox("Generate Tangents##import", &settings_.generate_tangents);
            ImGui::Checkbox("Optimize Mesh##import", &settings_.optimize_mesh);
        }
        
        // Mesh-specific settings
        if (current_asset_type_ == "Mesh") {
            if (ImGui::CollapsingHeader("Mesh Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("Import Animations##import", &settings_.import_animations);
                ImGui::Checkbox("Import Materials##import", &settings_.import_materials);
                ImGui::Checkbox("Import Textures##import", &settings_.import_textures);
            }
        }
        
        // Scene settings
        if (ImGui::CollapsingHeader("Scene Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Create Entity##import", &settings_.create_entity);
            ImGui::Checkbox("Auto Place in Scene##import", &settings_.auto_place_in_scene);
            
            if (settings_.auto_place_in_scene) {
                ImGui::DragFloat3("Spawn Position##import", &settings_.spawn_position[0], 0.1f);
            }
        }
        
        // Action buttons
        ImGui::Separator();
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 200);
        
        if (ImGui::Button("Import", ImVec2(90, 0))) {
            confirmed_ = true;
            result = true;
            is_open_ = false;
            spdlog::info("[AssetImportDialog] Asset import confirmed");
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Cancel", ImVec2(90, 0))) {
            confirmed_ = false;
            result = false;
            is_open_ = false;
            spdlog::info("[AssetImportDialog] Asset import cancelled");
        }
    }
    ImGui::End();
    
    return result;
}

} // namespace schizo::editor
