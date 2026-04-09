#include "asset_browser_panel.h"
#include "asset_manager.h"
#include "scene.h"
#include "entity.h"
#include <imgui.h>
#include <glad/glad.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <algorithm>
#include <string>
#include <vector>
#include <cstdlib>
#include <fstream>

namespace fs = std::filesystem;

namespace schizo::editor {

using namespace schizo::assets;

AssetBrowserPanel::AssetBrowserPanel() {
    state_.current_directory = "assets/models";
    RefreshAssets();
}

AssetBrowserPanel::~AssetBrowserPanel() = default;

void AssetBrowserPanel::Render(const std::shared_ptr<schizo::scene::Scene>& scene) {
    ImGuiWindowFlags flags = ImGuiWindowFlags_None;
    
    if (ImGui::Begin("Asset Browser##panel", nullptr, flags)) {
        // Toolbar
        RenderToolbar();
        
        ImGui::Separator();
        
        // Main layout: list on left, details on right
        ImGui::Columns(2, "asset_browser_columns", true);
        
        // Left column: asset grid/list
        ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.6f);
        RenderAssetGrid();
        
        ImGui::NextColumn();
        
        // Right column: asset details
        RenderAssetDetails();
        
        ImGui::Columns(1);
    }
    ImGui::End();
    
    // Render import dialog if open
    RenderImportDialog();
}

void AssetBrowserPanel::RenderToolbar() {
    // Directory navigation
    ImGui::Text("Directory: %s", state_.current_directory.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Refresh##assets")) {
        RefreshAssets();
        spdlog::info("[AssetBrowser] Refreshed asset list");
    }
    ImGui::SameLine();
    if (ImGui::Button("Open in Explorer##assets")) {
        // Open system file explorer
        std::string cmd = "explorer \"" + state_.current_directory + "\"";
        system(cmd.c_str());
        spdlog::info("[AssetBrowser] Opened in explorer: {}", state_.current_directory);
    }
    ImGui::SameLine();
    if (ImGui::Button("Import File##assets")) {
        state_.show_import_dialog = true;
        spdlog::info("[AssetBrowser] Opening import dialog");
    }
    
    ImGui::Separator();
    
    // Filter controls
    if (ImGui::Checkbox("Meshes##filter", &state_.show_meshes)) {
        RefreshAssets();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Textures##filter", &state_.show_textures)) {
        RefreshAssets();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Materials##filter", &state_.show_materials)) {
        RefreshAssets();
    }
    
    // Preview size slider
    ImGui::SliderFloat("Preview Size##assets", &state_.preview_size, 50.0f, 200.0f);
}

void AssetBrowserPanel::RenderAssetGrid() {
    ImGui::Text("Assets (%zu)", state_.discovered_assets.size());
    ImGui::Separator();
    
    if (state_.discovered_assets.empty()) {
        ImGui::TextWrapped("No assets found in directory. Drag .gltf, .glb, or .obj files here.");
        return;
    }
    
    // Calculate grid columns
    float item_width = state_.preview_size + 10.0f;
    int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / item_width));
    
    ImGui::BeginChild("AssetGrid", ImVec2(0, -30), true, ImGuiWindowFlags_None);
    
    int item_count = 0;
    for (size_t i = 0; i < state_.discovered_assets.size(); ++i) {
        auto& asset = state_.discovered_assets[i];
        
        // Create unique ID for this asset
        std::string item_id = "asset_item_" + std::to_string(i);
        bool is_selected = (state_.selected_asset_index == static_cast<int>(i));
        
        // Use Button instead to allow both drag/drop and custom rendering
        ImVec2 button_size(state_.preview_size, state_.preview_size);
        ImGui::PushID(i);  // Push ID for this item
        
        if (ImGui::Button("##asset_btn", button_size)) {
            state_.selected_asset_index = i;
            spdlog::info("[AssetBrowser] Selected asset: {}", asset.filename);
        }
        
        // IMPORTANT: Drag-drop MUST be handled immediately after the interactive element (Button)
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            // Store the asset index as drag payload
            size_t asset_index = i;
            if (asset.asset_type == "Mesh") {
                ImGui::SetDragDropPayload("MESH_ASSET", &asset_index, sizeof(size_t));
                ImGui::Text("Dragging: %s", asset.filename.c_str());
            } else if (asset.asset_type == "Texture") {
                ImGui::SetDragDropPayload("TEXTURE_ASSET", &asset_index, sizeof(size_t));
                ImGui::Text("Dragging: %s", asset.filename.c_str());
            } else {
                ImGui::SetDragDropPayload("ASSET_ITEM", &asset_index, sizeof(size_t));
                ImGui::Text("Dragging: %s", asset.filename.c_str());
            }
            ImGui::EndDragDropSource();
        }
        
        ImGui::PopID();
        
        // Draw preview on top of the button using DrawList
        ImVec2 item_min = ImGui::GetItemRectMin();
        ImVec2 item_max = ImGui::GetItemRectMax();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        
        // Draw background color based on type
        ImVec4 bg_color(0.3f, 0.3f, 0.3f, 0.8f);
        if (asset.asset_type == "Mesh") {
            bg_color = ImVec4(0.2f, 0.5f, 0.8f, 0.8f);  // Blue
        } else if (asset.asset_type == "Texture") {
            bg_color = ImVec4(0.8f, 0.6f, 0.2f, 0.8f);  // Orange
        } else if (asset.asset_type == "Material") {
            bg_color = ImVec4(0.6f, 0.2f, 0.8f, 0.8f);  // Purple
        }
        
        ImU32 bg_col = ImGui::GetColorU32(bg_color);
        draw_list->AddRectFilled(item_min, item_max, bg_col);
        
        // Draw border for selection
        if (is_selected) {
            ImU32 border_col = ImGui::GetColorU32(ImVec4(0.2f, 0.8f, 1.0f, 1.0f));
            draw_list->AddRect(item_min, item_max, border_col, 0.0f, 0, 2.0f);
        }
        
        // Draw asset name
        ImVec2 text_pos(item_min.x + 5, item_min.y + 5);
        ImU32 text_col = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        draw_list->AddText(text_pos, text_col, asset.filename.c_str());
        
        // Draw type label
        ImVec2 type_pos(item_min.x + 5, item_min.y + state_.preview_size - 20);
        ImVec4 type_color(1.0f, 1.0f, 1.0f, 0.7f);
        ImU32 type_col = ImGui::GetColorU32(type_color);
        draw_list->AddText(type_pos, type_col, asset.asset_type.c_str());
        
        // Tooltip with asset name
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", asset.filename.c_str());
            ImGui::Text("Path: %s", asset.path.c_str());
            ImGui::Text("Type: %s", asset.asset_type.c_str());
            if (asset.asset_type == "Mesh") {
                ImGui::Text("Vertices: %u", asset.vertex_count);
                ImGui::Text("Indices: %u", asset.index_count);
                ImGui::Text("Meshes: %u", asset.mesh_count);
            } else if (asset.asset_type == "Texture") {
                ImGui::Text("Size: %.1f KB", asset.file_size / 1024.0f);
            }
            ImGui::EndTooltip();
        }
        
        // Layout items in grid
        item_count++;
        if (item_count % columns != 0) {
            ImGui::SameLine();
        }
    }
    
    ImGui::EndChild();
}

void AssetBrowserPanel::RenderAssetDetails() {
    ImGui::BeginGroup();
    
    if (state_.selected_asset_index < 0 || state_.selected_asset_index >= state_.discovered_assets.size()) {
        ImGui::Text("Select an asset to view details");
        ImGui::EndGroup();
        return;
    }
    
    const auto& asset = state_.discovered_assets[state_.selected_asset_index];
    
    ImGui::Text("Asset Details");
    ImGui::Separator();
    
    ImGui::Text("Name: %s", asset.filename.c_str());
    ImGui::Text("Type: %s", asset.asset_type.c_str());
    ImGui::Text("Path: %s", asset.path.c_str());
    ImGui::Text("Size: %.2f KB", asset.file_size / 1024.0f);
    
    ImGui::Separator();
    ImGui::Text("Preview:");
    
    // Render larger preview
    RenderAssetPreview(asset);
    
    ImGui::Separator();
    
    // Asset-specific details
    if (asset.asset_type == "Mesh") {
        ImGui::Text("Mesh Information:");
        ImGui::Text("  Vertices: %u", asset.vertex_count);
        ImGui::Text("  Indices: %u", asset.index_count);
        ImGui::Text("  Primitives: %u", asset.mesh_count);
        
        ImGui::Separator();
        
        if (ImGui::Button("Create Entity with Mesh##asset", ImVec2(-1, 0))) {
            spdlog::info("[AssetBrowser] Would create entity with mesh: {}", asset.path);
        }
    } else if (asset.asset_type == "Texture") {
        ImGui::Text("Texture Information:");
        ImGui::Text("  Path: %s", asset.path.c_str());
    }
    
    ImGui::EndGroup();
}

void AssetBrowserPanel::RenderAssetPreview(const AssetMetadata& asset) {
    ImVec2 preview_size_large(150.0f, 150.0f);
    
    if (asset.preview_texture != 0) {
        ImGui::Image(
            reinterpret_cast<void*>(static_cast<uintptr_t>(asset.preview_texture)),
            preview_size_large,
            ImVec2(0, 1), ImVec2(1, 0)
        );
    } else {
        ImGui::Dummy(preview_size_large);
        ImGui::SameLine();
        ImGui::Text("[No preview]");
    }
}

void AssetBrowserPanel::RefreshAssets() {
    state_.discovered_assets.clear();
    state_.selected_asset_index = -1;
    DiscoverAssets(state_.current_directory);
    
    spdlog::info("[AssetBrowser] Refreshed: found {} assets", state_.discovered_assets.size());
}

void AssetBrowserPanel::SetDirectory(const std::string& path) {
    state_.current_directory = path;
    RefreshAssets();
}

const AssetMetadata* AssetBrowserPanel::GetSelectedAsset() const {
    if (state_.selected_asset_index < 0 || state_.selected_asset_index >= state_.discovered_assets.size()) {
        return nullptr;
    }
    return &state_.discovered_assets[state_.selected_asset_index];
}

const AssetMetadata* AssetBrowserPanel::GetAssetByIndex(size_t index) const {
    if (index >= state_.discovered_assets.size()) {
        return nullptr;
    }
    return &state_.discovered_assets[index];
}

void AssetBrowserPanel::DiscoverAssets(const std::string& directory) {
    try {
        if (!fs::exists(directory)) {
            spdlog::warn("[AssetBrowser] Directory not found: {}", directory);
            return;
        }
        
        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            if (!entry.is_regular_file()) continue;
            
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            // Filter by type
            bool is_mesh = (ext == ".gltf" || ext == ".glb" || ext == ".obj");
            bool is_texture = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga");
            bool is_material = (ext == ".mat");
            
            if (!(is_mesh && state_.show_meshes) &&
                !(is_texture && state_.show_textures) &&
                !(is_material && state_.show_materials)) {
                continue;
            }
            
            AssetMetadata metadata;
            metadata.path = entry.path().string();
            metadata.filename = entry.path().filename().string();
            metadata.extension = ext;
            metadata.file_size = fs::file_size(entry.path());
            
            // Determine asset type
            if (is_mesh) {
                metadata.asset_type = "Mesh";
                LoadAssetMetadata(metadata.path, metadata);
            } else if (is_texture) {
                metadata.asset_type = "Texture";
                LoadAssetMetadata(metadata.path, metadata);
            } else if (is_material) {
                metadata.asset_type = "Material";
            }
            
            state_.discovered_assets.push_back(metadata);
        }
        
        // Sort alphabetically
        std::sort(state_.discovered_assets.begin(), state_.discovered_assets.end(),
                  [](const AssetMetadata& a, const AssetMetadata& b) {
                      return a.filename < b.filename;
                  });
        
    } catch (const std::exception& e) {
        spdlog::error("[AssetBrowser] Error discovering assets: {}", e.what());
    }
}

void AssetBrowserPanel::LoadAssetMetadata(const std::string& path, AssetMetadata& metadata) {
    try {
        if (metadata.asset_type == "Texture") {
            // Load texture and create preview
            LoadTexturePreview(path, metadata);
        } else {
            auto& asset_mgr = AssetManager::GetInstance();
            auto mesh = asset_mgr.LoadMesh(path);
            
            if (mesh) {
                // Count vertices and indices
                uint32_t total_vertices = 0;
                uint32_t total_indices = 0;
                
                // Note: MeshAsset structure would have primitives or vertices/indices
                // This is a placeholder - actual implementation depends on MeshAsset structure
                metadata.vertex_count = 1000;  // Placeholder
                metadata.index_count = 3000;   // Placeholder
                metadata.mesh_count = 1;
                
                spdlog::info("[AssetBrowser] Loaded mesh metadata: {}", path);
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("[AssetBrowser] Failed to load asset metadata: {}", e.what());
    }
}

void AssetBrowserPanel::LoadTexturePreview(const std::string& path, AssetMetadata& metadata) {
    try {
        // Create a simple colored placeholder texture for now
        // TODO: Implement actual image loading (stbi_load or similar)
        
        // Create a test texture (simple gradient)
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(0x0DE1, texture);  // GL_TEXTURE_2D
        
        // Generate simple preview texture data
        const int size = 64;
        std::vector<unsigned char> data(size * size * 4);
        
        // Create a simple pattern based on file extension
        unsigned char r = 100, g = 120, b = 140;
        if (path.find(".png") != std::string::npos) {
            r = 80; g = 160; b = 240;  // Blue for PNG
        } else if (path.find(".jpg") != std::string::npos || path.find(".jpeg") != std::string::npos) {
            r = 220; g = 140; b = 80;  // Orange for JPG
        } else if (path.find(".tga") != std::string::npos) {
            r = 160; g = 100; b = 200; // Purple for TGA
        }
        
        for (int i = 0; i < size * size * 4; i += 4) {
            data[i]     = r;
            data[i + 1] = g;
            data[i + 2] = b;
            data[i + 3] = 255;
        }
        
        glTexImage2D(0x0DE1, 0, 0x1908, size, size, 0, 0x1908, 0x1401, data.data());  // GL_TEXTURE_2D, GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE
        glTexParameteri(0x0DE1, 0x2801, 0x2601);  // GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR
        glTexParameteri(0x0DE1, 0x2800, 0x2601);  // GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR
        
        metadata.preview_texture = texture;
        metadata.preview_ready = true;
        
        spdlog::debug("[AssetBrowser] Generated texture preview: {}", path);
    } catch (const std::exception& e) {
        spdlog::warn("[AssetBrowser] Failed to load texture preview: {}", e.what());
    }
}

void AssetBrowserPanel::GeneratePreviewTexture(AssetMetadata& asset) {
    // Generate a placeholder preview texture
    // This would be enhanced to generate proper mesh thumbnails
    
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    
    // Create a simple gradient texture as placeholder
    unsigned char data[16 * 16 * 4];
    for (int i = 0; i < 16 * 16; ++i) {
        data[i * 4 + 0] = (i % 16) * 16;  // Red
        data[i * 4 + 1] = (i / 16) * 16;  // Green
        data[i * 4 + 2] = 128;            // Blue
        data[i * 4 + 3] = 255;            // Alpha
    }
    
    // TODO: Implement preview texture generation with proper OpenGL calls
    // For now: placeholder
    /*
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    */
    
    asset.preview_texture = texture;
    asset.preview_ready = true;
}

void AssetBrowserPanel::RenderImportDialog() {
    if (!state_.show_import_dialog) {
        return;
    }
    
    ImGui::SetNextWindowSize(ImVec2(600, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Import Asset##dialog", &state_.show_import_dialog, ImGuiWindowFlags_Modal)) {
        ImGui::Text("Select a file to import:");
        
        ImGui::InputText("File Path##import", state_.import_file_path.data(), state_.import_file_path.capacity(), ImGuiInputTextFlags_ReadOnly);
        ImGui::SameLine();
        if (ImGui::Button("Browse...##browse")) {
            std::string selected_file = ShowOpenFileDialog();
            if (!selected_file.empty()) {
                state_.import_file_path = selected_file;
            }
        }
        
        ImGui::Separator();
        ImGui::Text("Import to: %s", state_.import_target_path.c_str());
        
        ImGui::Separator();
        
        // Buttons
        if (ImGui::Button("Import##import_btn", ImVec2(120, 0))) {
            if (!state_.import_file_path.empty() && fs::exists(state_.import_file_path)) {
                ImportFile(state_.import_file_path, state_.import_target_path);
                state_.show_import_dialog = false;
                state_.import_file_path.clear();
            } else {
                spdlog::warn("[AssetBrowser] Invalid file path for import");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##import_cancel", ImVec2(120, 0))) {
            state_.show_import_dialog = false;
            state_.import_file_path.clear();
        }
        
        ImGui::End();
    }
}

std::string AssetBrowserPanel::ShowOpenFileDialog() {
    // Use Windows native file dialog (COMDLG32.DLL)
    // For now, use a simpler system cmd approach
    
    // Create a temporary batch file to handle file selection
    const char* batch_file = "temp_file_dialog.bat";
    const char* result_file = "temp_file_result.txt";
    
    // Write batch file that opens file dialog and saves result
    FILE* f = fopen(batch_file, "w");
    if (!f) {
        spdlog::warn("[AssetBrowser] Could not create batch file for file dialog");
        return "";
    }
    
    fprintf(f, "@echo off\n");
    fprintf(f, "set \"filter=3D Models (*.obj;*.gltf;*.glb)|*.obj;*.gltf;*.glb|\"\n");
    fprintf(f, "for /f \"delims=\" %%%%A in ('mshta \"about:<input id=FILE type=file><script>FILE.click();new ActiveXObject('Scripting.FileSystemObject').CreateTextFile('temp_file_result.txt').WriteLine(FILE.value);close();</script>\"') do set \"file=%%%%A\"\n");
    fprintf(f, "if defined file echo %%%%file%%%%>%s\n", result_file);
    fclose(f);
    
    // Execute batch file
    int result = system(batch_file);
    
    std::string selected_path;
    if (result == 0 && fs::exists(result_file)) {
        std::ifstream result_stream(result_file);
        std::getline(result_stream, selected_path);
        result_stream.close();
        
        // Clean up result file
        fs::remove(result_file);
    }
    
    // Clean up batch file
    fs::remove(batch_file);
    
    return selected_path;
}

void AssetBrowserPanel::ImportFile(const std::string& file_path, const std::string& target_path) {
    try {
        if (!fs::exists(file_path)) {
            spdlog::error("[AssetBrowser] Import file not found: {}", file_path);
            return;
        }
        
        // Get filename from path
        std::string filename = fs::path(file_path).filename().string();
        
        // Ensure target directory exists
        if (!fs::exists(target_path)) {
            fs::create_directories(target_path);
        }
        
        // Copy file to target directory
        std::string dest_path = target_path + "/" + filename;
        fs::copy_file(file_path, dest_path, fs::copy_options::overwrite_existing);
        
        spdlog::info("[AssetBrowser] Imported file: {} -> {}", file_path, dest_path);
        
        // Refresh asset list
        RefreshAssets();
        
    } catch (const std::exception& e) {
        spdlog::error("[AssetBrowser] Error importing file: {}", e.what());
    }
}

} // namespace schizo::editor
