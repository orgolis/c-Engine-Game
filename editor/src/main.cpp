// Project Schizo Editor - Main entry point
// Phase 6: Vulkan renderer backend

#define GLM_ENABLE_EXPERIMENTAL
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

// gws Vulkan renderer
#include "vulkan/vulkan_device.h"
#include "vulkan/vulkan_swapchain.h"
#include "vulkan/vulkan_g_buffer.h"
#include "vulkan/vulkan_lighting_pass.h"
#include "vulkan/vulkan_shadow_map.h"
#include "vulkan/vulkan_post_processing.h"
#include "vulkan/vulkan_render_graph.h"
#include "vulkan/vulkan_scene_mesh.h"
#include "vulkan/vulkan_scene_material.h"
#include "vulkan/imgui_vulkan.h"

// ImGui headers
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

// Editor / scene headers
#include "editor_scene.h"
#include "scene.h"
#include "entity_factory.h"
#include "transform_component.h"
#include "light_component.h"
#include "viewport_camera.h"
#include "mesh_renderer_component.h"
#include "asset_browser_panel.h"
#include "material_editor_panel.h"
#include "asset_import_dialog.h"
#include "transform_gizmo.h"
#include "undo_redo_manager.h"
#include "asset_manager.h"
#include "scene_playback_manager.h"
#include "primitive_meshes.h"
#include "scene_render_bridge.h"
#include "character_controller_panel.h"
#include "ability_system_panel.h"
#include "network_system_panel.h"

#include <spdlog/spdlog.h>
#include <iostream>
#include <functional>
#include <cstdint>
#include <memory>
#include <limits>
#include <algorithm>
#include <vector>
#include <filesystem>
#include <cctype>
#include <system_error>

// GLM headers
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

using namespace gws::renderer::gpu;

// Editor state
struct EditorState {
    schizo::editor::EditorScene* editor_scene = nullptr;
    
    bool show_scene_hierarchy = true;
    bool show_inspector = true;
    bool show_asset_browser = true;
    bool show_viewport = true;
    bool show_demo_window = false;
    bool show_preferences = false;
    
    // Scene/Entity data
    uint32_t selected_entity_id = 0;  // 0 = no selection

    // Viewport camera + input state
    schizo::editor::ViewportCamera viewport_camera;
    bool viewport_camera_rotating = false;
    glm::vec2 last_mouse_pos = glm::vec2(0.0f);

    // Actual viewport panel size (updated each frame by ShowViewport)
    glm::vec2 viewport_panel_size = glm::vec2(1920.0f, 1080.0f);

    // Viewport display options
    bool show_grid = true;
    bool wireframe_mode = false;
    
    // File dialog state
    bool show_save_dialog = false;
    bool show_open_dialog = false;
    char save_filename[256] = "untitled.scene";
    char open_filename[256] = "";
    
    // Rename dialog state
    bool show_rename_dialog = false;
    uint32_t rename_entity_id = 0;
    char rename_buffer[256] = "";
    
    // Viewport texture (Vulkan post-processing output displayed in ImGui)
    VkDescriptorSet viewport_texture_id = VK_NULL_HANDLE;

    // Transform Gizmo
    schizo::editor::TransformGizmo transform_gizmo;
    bool show_gizmo = true;
    
    // Asset Browser
    std::unique_ptr<schizo::editor::AssetBrowserPanel> asset_browser;
    
    // Material Editor
    std::unique_ptr<schizo::editor::MaterialEditorPanel> material_editor;
    
    // Asset Import Dialog
    std::unique_ptr<schizo::editor::AssetImportDialog> asset_import_dialog;
    
    // Gizmo dragging state
    bool gizmo_dragging = false;
    char gizmo_axis = 0;  // 0=none, 'x', 'y', 'z'
    glm::vec2 gizmo_drag_start = glm::vec2(0.0f);
    glm::vec3 gizmo_drag_offset = glm::vec3(0.0f);
    
    // Play Mode
    bool is_playing = false;
    float play_time = 0.0f;
    
    // Undo/Redo
    schizo::editor::UndoRedoManager undo_redo_manager;
    
    // Drag and drop
    std::vector<std::string> dropped_files;
    
    // Scene Playback System
    std::unique_ptr<schizo::editor::ScenePlaybackManager> scene_playback_manager;
    bool show_playback_controls = true;
    bool show_debug_panels = true;
    
    // Debug Panels
    std::unique_ptr<schizo::editor::CharacterControllerPanel> character_panel;
    std::unique_ptr<schizo::editor::AbilitySystemPanel> ability_panel;
    std::unique_ptr<schizo::editor::NetworkSystemPanel> network_panel;
    
    // Placeholder pointers for demo (would come from scene entities)
    engine::character::CharacterController* selected_character_controller = nullptr;
    engine::ability::AbilitySystem* selected_ability_system = nullptr;
    engine::network::NetworkManager* network_manager = nullptr;
};

// ============================================================================
// Global State for GLFW Callbacks
// ============================================================================

static EditorState* g_editor_state = nullptr;

// GLFW drop callback for drag-and-drop file support
static void DropCallback(GLFWwindow* window, int count, const char** paths) {
    if (!g_editor_state) return;
    
    spdlog::info("Dropped {} files into editor", count);
    for (int i = 0; i < count; ++i) {
        std::string full_path(paths[i]);
        
        // Extract just the filename for display
        size_t last_slash = full_path.find_last_of("/\\");
        std::string filename = (last_slash != std::string::npos) ? full_path.substr(last_slash + 1) : full_path;
        
        spdlog::info("  - File {}: {} (full path: {})", i + 1, filename, full_path);
        // Store the full path so asset manager can find the file
        g_editor_state->dropped_files.push_back(full_path);
    }
}

// ============================================================================
// File Dialog Functions
// ============================================================================

void ShowSaveDialog(EditorState& editor_state) {
    if (!editor_state.show_save_dialog) return;
    
    ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Save Scene As", &editor_state.show_save_dialog)) {
        ImGui::Text("Enter filename:");
        ImGui::InputText("##filename", editor_state.save_filename, sizeof(editor_state.save_filename));
        
        ImGui::Spacing();
        ImGui::Separator();
        
        if (ImGui::Button("Save", ImVec2(80, 0))) {
            std::string filepath = std::string("scenes/") + editor_state.save_filename;
            if (filepath.find(".scene") == std::string::npos) {
                filepath += ".scene";
            }
            editor_state.editor_scene->SaveScene(filepath);
            spdlog::info("Scene saved to: {}", filepath);
            editor_state.show_save_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0))) {
            editor_state.show_save_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
}

void ShowOpenDialog(EditorState& editor_state) {
    if (!editor_state.show_open_dialog) return;
    
    ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Open Scene", &editor_state.show_open_dialog)) {
        ImGui::Text("Enter filename:");
        ImGui::InputText("##openfilename", editor_state.open_filename, sizeof(editor_state.open_filename));
        
        ImGui::Spacing();
        ImGui::Text("Recent scenes:");
        if (ImGui::MenuItem("scenes/default.scene")) {
            editor_state.editor_scene->LoadScene("scenes/default.scene");
            editor_state.show_open_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        
        if (ImGui::Button("Open", ImVec2(80, 0))) {
            std::string filepath = std::string("scenes/") + editor_state.open_filename;
            if (filepath.find(".scene") == std::string::npos) {
                filepath += ".scene";
            }
            editor_state.editor_scene->LoadScene(filepath);
            spdlog::info("Scene loaded from: {}", filepath);
            editor_state.show_open_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0))) {
            editor_state.show_open_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
}

void ShowRenameDialog(EditorState& editor_state) {
    if (!editor_state.show_rename_dialog) return;
    
    ImGui::SetNextWindowSize(ImVec2(350, 150), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Rename Entity", &editor_state.show_rename_dialog)) {
        ImGui::Text("Enter new name:");
        ImGui::InputText("##entityname", editor_state.rename_buffer, sizeof(editor_state.rename_buffer));
        
        ImGui::Spacing();
        ImGui::Separator();
        
        if (ImGui::Button("Rename", ImVec2(80, 0))) {
            auto scene = editor_state.editor_scene->GetScene();
            if (scene) {
                auto entity = scene->GetEntityById(editor_state.rename_entity_id);
                if (entity && strlen(editor_state.rename_buffer) > 0) {
                    std::string old_name = entity->GetName();
                    std::string new_name = editor_state.rename_buffer;
                    
                    if (old_name != new_name) {
                        // Create undo/redo command for rename
                        auto rename_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                            [entity, new_name]() { entity->SetName(new_name); },
                            [entity, old_name]() { entity->SetName(old_name); },
                            "Rename to " + new_name
                        );
                        
                        editor_state.undo_redo_manager.ExecuteCommand(std::move(rename_cmd));
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Renamed entity: {} -> {}", old_name, new_name);
                    }
                }
            }
            editor_state.show_rename_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0))) {
            editor_state.show_rename_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
}

// ============================================================================
// Menu Functions
// ============================================================================

void ShowMainMenuBar(EditorState& editor_state) {
    if (ImGui::BeginMainMenuBar()) {
        // File menu
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                editor_state.editor_scene->NewScene("Untitled");
                spdlog::info("New scene created");
            }
            if (ImGui::MenuItem("Open Scene", "Ctrl+O")) {
                editor_state.show_open_dialog = true;
                ImGui::OpenPopup("Open Scene");
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                auto filepath = editor_state.editor_scene->GetSceneFilepath();
                if (filepath.empty()) {
                    filepath = "scenes/untitled.scene";
                }
                editor_state.editor_scene->SaveScene(filepath);
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
                editor_state.show_save_dialog = true;
                ImGui::OpenPopup("Save Scene As");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                spdlog::info("Exit requested");
            }
            ImGui::EndMenu();
        }
        
        // Edit menu
        if (ImGui::BeginMenu("Edit")) {
            bool can_undo = editor_state.undo_redo_manager.CanUndo();
            if (ImGui::MenuItem(editor_state.undo_redo_manager.GetUndoDescription().c_str(), 
                              "Ctrl+Z", false, can_undo)) {
                editor_state.undo_redo_manager.Undo();
                spdlog::info("Undo: {}", editor_state.undo_redo_manager.GetRedoDescription());
            }
            
            bool can_redo = editor_state.undo_redo_manager.CanRedo();
            if (ImGui::MenuItem(editor_state.undo_redo_manager.GetRedoDescription().c_str(),
                              "Ctrl+Y", false, can_redo)) {
                editor_state.undo_redo_manager.Redo();
                spdlog::info("Redo: {}", editor_state.undo_redo_manager.GetUndoDescription());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Preferences...")) {
                editor_state.show_preferences = true;
            }
            ImGui::EndMenu();
        }
        
        // View menu
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Scene Hierarchy", nullptr, &editor_state.show_scene_hierarchy);
            ImGui::MenuItem("Inspector", nullptr, &editor_state.show_inspector);
            ImGui::MenuItem("Asset Browser", nullptr, &editor_state.show_asset_browser);
            ImGui::MenuItem("Viewport", nullptr, &editor_state.show_viewport);
            ImGui::Separator();
            ImGui::MenuItem("Playback Controls", nullptr, &editor_state.show_playback_controls);
            ImGui::MenuItem("Debug Panels (Phase 6)", nullptr, &editor_state.show_debug_panels);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout")) {
                spdlog::info("Reset layout");
            }
            ImGui::EndMenu();
        }
        
        // Tools menu
        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Build Scene")) {
                spdlog::info("Build Scene");
            }
            
            const char* play_label = editor_state.is_playing ? "Stop (F5)" : "Play (F5)";
            if (ImGui::MenuItem(play_label)) {
                editor_state.is_playing = !editor_state.is_playing;
                editor_state.play_time = 0.0f;
                if (editor_state.is_playing) {
                    spdlog::info("Play mode started");
                } else {
                    spdlog::info("Play mode stopped");
                }
            }
            ImGui::EndMenu();
        }
        
        // Help menu
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Show Demo Window")) {
                editor_state.show_demo_window = !editor_state.show_demo_window;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("About...")) {
                spdlog::info("About");
            }
            ImGui::EndMenu();
        }
        
        ImGui::EndMainMenuBar();
    }
}

void ShowSceneHierarchy(EditorState& editor_state) {
    if (!editor_state.show_scene_hierarchy) return;
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove;  // Fixed position
    if (editor_state.gizmo_dragging) {
        flags |= ImGuiWindowFlags_NoInputs;  // Disable input when dragging in viewport
    }
    
    if (ImGui::Begin("Scene Hierarchy", &editor_state.show_scene_hierarchy, flags)) {
        auto scene = editor_state.editor_scene->GetScene();
        if (!scene) {
            ImGui::Text("No scene loaded");
            ImGui::End();
            return;
        }
        
        ImGui::Text("Scene: %s", scene->GetName().c_str());
        ImGui::Separator();
        
        // Create entity button dropdown
        if (ImGui::Button("+ Add Entity")) {
            ImGui::OpenPopup("AddEntityPopup");
        }
        
        if (ImGui::BeginPopup("AddEntityPopup")) {
            // Primitive entities
            if (ImGui::MenuItem("Cube")) {
                auto create_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                    [scene, &editor_state]() {
                        auto ent = schizo::scene::EntityFactory::CreateCube(scene);
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Created Cube entity");
                    },
                    [scene, &editor_state]() {
                        auto ent = scene->GetEntityByName("Cube");
                        if (ent) {
                            scene->RemoveEntity(ent);
                            spdlog::info("Removed Cube entity");
                        }
                    },
                    "Create Cube"
                );
                editor_state.undo_redo_manager.ExecuteCommand(std::move(create_cmd));
                ImGui::CloseCurrentPopup();
            }
            
            if (ImGui::MenuItem("Sphere")) {
                auto create_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                    [scene, &editor_state]() {
                        auto ent = schizo::scene::EntityFactory::CreateSphere(scene);
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Created Sphere entity");
                    },
                    [scene, &editor_state]() {
                        auto ent = scene->GetEntityByName("Sphere");
                        if (ent) {
                            scene->RemoveEntity(ent);
                            spdlog::info("Removed Sphere entity");
                        }
                    },
                    "Create Sphere"
                );
                editor_state.undo_redo_manager.ExecuteCommand(std::move(create_cmd));
                ImGui::CloseCurrentPopup();
            }
            
            if (ImGui::MenuItem("Capsule")) {
                auto create_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                    [scene, &editor_state]() {
                        auto ent = schizo::scene::EntityFactory::CreateCapsule(scene);
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Created Capsule entity");
                    },
                    [scene, &editor_state]() {
                        auto ent = scene->GetEntityByName("Capsule");
                        if (ent) {
                            scene->RemoveEntity(ent);
                            spdlog::info("Removed Capsule entity");
                        }
                    },
                    "Create Capsule"
                );
                editor_state.undo_redo_manager.ExecuteCommand(std::move(create_cmd));
                ImGui::CloseCurrentPopup();
            }
            
            if (ImGui::MenuItem("Cylinder")) {
                auto create_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                    [scene, &editor_state]() {
                        auto ent = schizo::scene::EntityFactory::CreateCylinder(scene);
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Created Cylinder entity");
                    },
                    [scene, &editor_state]() {
                        auto ent = scene->GetEntityByName("Cylinder");
                        if (ent) {
                            scene->RemoveEntity(ent);
                            spdlog::info("Removed Cylinder entity");
                        }
                    },
                    "Create Cylinder"
                );
                editor_state.undo_redo_manager.ExecuteCommand(std::move(create_cmd));
                ImGui::CloseCurrentPopup();
            }
            
            if (ImGui::MenuItem("Plane")) {
                auto create_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                    [scene, &editor_state]() {
                        auto ent = schizo::scene::EntityFactory::CreatePlane(scene);
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Created Plane entity");
                    },
                    [scene, &editor_state]() {
                        auto ent = scene->GetEntityByName("Plane");
                        if (ent) {
                            scene->RemoveEntity(ent);
                            spdlog::info("Removed Plane entity");
                        }
                    },
                    "Create Plane"
                );
                editor_state.undo_redo_manager.ExecuteCommand(std::move(create_cmd));
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::Separator();
            
            // Special entities
            if (ImGui::MenuItem("Player")) {
                auto create_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                    [scene, &editor_state]() {
                        auto ent = schizo::scene::EntityFactory::CreatePlayer(scene);
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Created Player entity");
                    },
                    [scene, &editor_state]() {
                        auto ent = scene->GetEntityByName("Player");
                        if (ent) {
                            scene->RemoveEntity(ent);
                            spdlog::info("Removed Player entity");
                        }
                    },
                    "Create Player"
                );
                editor_state.undo_redo_manager.ExecuteCommand(std::move(create_cmd));
                ImGui::CloseCurrentPopup();
            }
            
            if (ImGui::MenuItem("Camera")) {
                auto create_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                    [scene, &editor_state]() {
                        auto ent = schizo::scene::EntityFactory::CreateCamera(scene);
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Created Camera entity");
                    },
                    [scene, &editor_state]() {
                        auto ent = scene->GetEntityByName("Camera");
                        if (ent) {
                            scene->RemoveEntity(ent);
                            spdlog::info("Removed Camera entity");
                        }
                    },
                    "Create Camera"
                );
                editor_state.undo_redo_manager.ExecuteCommand(std::move(create_cmd));
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::Separator();
            
            // Light sources
            if (ImGui::MenuItem("Directional Light")) {
                auto create_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                    [scene, &editor_state]() {
                        auto ent = schizo::scene::EntityFactory::CreateDirectionalLight(scene);
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Created DirectionalLight entity");
                    },
                    [scene, &editor_state]() {
                        auto ent = scene->GetEntityByName("DirectionalLight");
                        if (ent) {
                            scene->RemoveEntity(ent);
                            spdlog::info("Removed DirectionalLight entity");
                        }
                    },
                    "Create Directional Light"
                );
                editor_state.undo_redo_manager.ExecuteCommand(std::move(create_cmd));
                ImGui::CloseCurrentPopup();
            }
            
            if (ImGui::MenuItem("Global Light")) {
                auto create_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                    [scene, &editor_state]() {
                        auto ent = schizo::scene::EntityFactory::CreateGlobalLight(scene);
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Created GlobalLight entity");
                    },
                    [scene, &editor_state]() {
                        auto ent = scene->GetEntityByName("GlobalLight");
                        if (ent) {
                            scene->RemoveEntity(ent);
                            spdlog::info("Removed GlobalLight entity");
                        }
                    },
                    "Create Global Light"
                );
                editor_state.undo_redo_manager.ExecuteCommand(std::move(create_cmd));
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::Separator();
            
            if (ImGui::MenuItem("Empty Entity")) {
                auto entity_name = "Entity_" + std::to_string(scene->GetEntityCount());
                
                // Create undo/redo command for entity creation
                auto create_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                    [scene, entity_name, &editor_state]() {
                        auto ent = scene->CreateEntity(entity_name);
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Created entity: {}", entity_name);
                    },
                    [scene, entity_name, &editor_state]() {
                        // Find and remove entity by name
                        auto ent = scene->GetEntityByName(entity_name);
                        if (ent) {
                            scene->RemoveEntity(ent);
                            spdlog::info("Removed entity: {}", entity_name);
                        }
                    },
                    "Create entity: " + entity_name
                );
                editor_state.undo_redo_manager.ExecuteCommand(std::move(create_cmd));
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::EndPopup();
        }
        
        ImGui::SameLine();
        ImGui::Text("(%zu entities)", scene->GetEntityCount());
        ImGui::Separator();
        
        ImGui::BeginChild("EntityList", ImVec2(0, 0), true);
        
        // Helper lambda to recursively display entity hierarchy
        std::function<void(const std::shared_ptr<schizo::scene::Entity>&)> draw_entity_node = 
            [&](const std::shared_ptr<schizo::scene::Entity>& entity) {
                if (!entity) return;
                
                const auto& children = entity->GetChildren();
                bool has_children = !children.empty();
                
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
                if (!has_children) {
                    flags |= ImGuiTreeNodeFlags_Leaf;
                }
                if (editor_state.selected_entity_id == entity->GetId()) {
                    flags |= ImGuiTreeNodeFlags_Selected;
                }
                
                // Build display string with name, ID, and active status
                std::string label = entity->GetName() + " (ID: " + std::to_string(entity->GetId()) + ")";
                if (!entity->IsActive()) {
                    label += " [inactive]";
                }
                
                bool node_open = ImGui::TreeNodeEx((void*)(intptr_t)entity->GetId(), flags, "%s", label.c_str());
                
                // Handle selection on click
                if (ImGui::IsItemClicked()) {
                    editor_state.selected_entity_id = entity->GetId();
                    spdlog::debug("Selected entity: {} (ID: {})", entity->GetName(), entity->GetId());
                }
                
                // DRAG SOURCE - Entity can be dragged to reparent
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    // Store entity ID as payload
                    uint32_t entity_id = entity->GetId();
                    ImGui::SetDragDropPayload("ENTITY_NODE", &entity_id, sizeof(uint32_t));
                    ImGui::Text("Reparenting: %s", entity->GetName().c_str());
                    ImGui::EndDragDropSource();
                }
                
                // DROP TARGET - Entity can receive other entities as children
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_NODE")) {
                        IM_ASSERT(payload->DataSize == sizeof(uint32_t));
                        uint32_t dragged_entity_id = *(const uint32_t*)payload->Data;
                        
                        // Find and reparent the dragged entity
                        auto dragged = scene->GetEntityById(dragged_entity_id);
                        if (dragged && dragged != entity) {  // Can't parent to self
                            dragged->SetParent(entity);
                            spdlog::info("Reparented {} to {}", dragged->GetName(), entity->GetName());
                            editor_state.editor_scene->MarkModified();
                        }
                    }
                    
                    // Also accept mesh assets — payload is a size_t index
                    // into the asset browser's discovered list.
                    if (const ImGuiPayload* mesh_payload = ImGui::AcceptDragDropPayload("MESH_ASSET")) {
                        IM_ASSERT(mesh_payload->DataSize == sizeof(size_t));
                        size_t asset_idx = *(const size_t*)mesh_payload->Data;
                        if (editor_state.asset_browser) {
                            const auto* asset = editor_state.asset_browser->GetAssetByIndex(asset_idx);
                            if (asset && asset->asset_type == "Mesh") {
                                entity->SetMesh(asset->path);
                                spdlog::info("Assigned mesh '{}' to entity '{}'",
                                             asset->path, entity->GetName());
                                editor_state.editor_scene->MarkModified();
                            }
                        }
                    }
                    
                    ImGui::EndDragDropTarget();
                }
                
                // Right-click context menu
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Rename", nullptr, false)) {
                        editor_state.show_rename_dialog = true;
                        editor_state.rename_entity_id = entity->GetId();
                        strncpy_s(editor_state.rename_buffer, sizeof(editor_state.rename_buffer), 
                                 entity->GetName().c_str(), _TRUNCATE);
                        ImGui::OpenPopup("Rename Entity");
                    }
                    
                    if (ImGui::MenuItem("Duplicate")) {
                        // Create a copy of this entity
                        auto duplicated = scene->CreateEntity(entity->GetName() + "_copy");
                        auto src_transform = entity->GetTransform();
                        auto dst_transform = duplicated->GetTransform();
                        dst_transform->SetLocalPosition(src_transform->GetLocalPosition());
                        dst_transform->SetLocalRotation(src_transform->GetLocalRotation());
                        dst_transform->SetLocalScale(src_transform->GetLocalScale());
                        
                        // If entity has a parent, set the same parent for the copy
                        if (entity->GetParent()) {
                            duplicated->SetParent(entity->GetParent());
                        }
                        
                        spdlog::info("Duplicated entity: {} -> {}", entity->GetName(), duplicated->GetName());
                        editor_state.editor_scene->MarkModified();
                    }
                    
                    if (ImGui::MenuItem("Create Child")) {
                        auto child = scene->CreateEntity("Child_" + std::to_string(scene->GetEntityCount()));
                        child->SetParent(entity);
                        spdlog::info("Created child entity: {}", child->GetName());
                        editor_state.editor_scene->MarkModified();
                    }
                    
                    ImGui::Separator();
                    
                    if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                        // Copy entity ID to clipboard (simplified copy)
                        spdlog::info("Copied entity: {}", entity->GetName());
                    }
                    
                    ImGui::Separator();
                    
                    bool is_active = entity->IsActive();
                    if (ImGui::MenuItem(is_active ? "Deactivate" : "Activate")) {
                        entity->SetActive(!is_active);
                        spdlog::info("Toggled active state for: {}", entity->GetName());
                        editor_state.editor_scene->MarkModified();
                    }
                    
                    ImGui::Separator();
                    
                    if (ImGui::MenuItem("Delete", "Delete", false)) {
                        uint32_t deleted_id = entity->GetId();
                        scene->RemoveEntity(entity);
                        if (editor_state.selected_entity_id == deleted_id) {
                            editor_state.selected_entity_id = 0;
                        }
                        spdlog::info("Deleted entity: {}", entity->GetName());
                        editor_state.editor_scene->MarkModified();
                    }
                    
                    ImGui::EndPopup();
                }
                
                // Recursively draw children
                if (node_open) {
                    for (const auto& child : children) {
                        draw_entity_node(child);
                    }
                    ImGui::TreePop();
                }
            };
        
        // Draw root entities (those without parents)
        const auto& entities = scene->GetEntities();
        for (const auto& entity : entities) {
            if (!entity->GetParent()) {
                draw_entity_node(entity);
            }
        }
        
        ImGui::EndChild();
        ImGui::End();
    }
}

void ShowInspector(EditorState& editor_state) {
    if (!editor_state.show_inspector) return;
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove;  // Fixed position
    if (editor_state.gizmo_dragging) {
        flags |= ImGuiWindowFlags_NoInputs;  // Disable input when dragging in viewport
    }
    
    if (ImGui::Begin("Inspector", &editor_state.show_inspector, flags)) {
        auto scene = editor_state.editor_scene->GetScene();
        if (!scene || editor_state.selected_entity_id == 0) {
            ImGui::Text("No entity selected");
            ImGui::End();
            return;
        }
        
        // Get selected entity by ID
        auto selected_entity = scene->GetEntityById(editor_state.selected_entity_id);
        if (!selected_entity) {
            ImGui::Text("Entity not found");
            ImGui::End();
            return;
        }
        
        ImGui::Text("Entity: %s (ID: %u)", selected_entity->GetName().c_str(), selected_entity->GetId());
        ImGui::Separator();
        
        // Display current mesh model
        auto mesh_comp = selected_entity->GetMeshComponent();
        if (mesh_comp && !mesh_comp->mesh_path.empty()) {
            ImGui::Text("Model: %s", mesh_comp->mesh_path.c_str());
        } else {
            ImGui::TextDisabled("Model: [default cube]");
        }
        ImGui::Separator();
        
        // Entity name editing
        static char entity_name_buf[256];
        if (ImGui::InputText("Name", entity_name_buf, sizeof(entity_name_buf))) {
            selected_entity->SetName(entity_name_buf);
            editor_state.editor_scene->MarkModified();
        }
        
        // Active toggle
        bool is_active = selected_entity->IsActive();
        if (ImGui::Checkbox("Active", &is_active)) {
            selected_entity->SetActive(is_active);
            editor_state.editor_scene->MarkModified();
        }
        
        ImGui::Separator();
        
        // Model and Texture Preview Section
        if (ImGui::TreeNode("Model Preview")) {
            ImGui::Columns(2, "model_preview_cols", true);
            
            // Left column: preview area
            ImVec2 preview_size(150, 150);
            ImGui::BeginChild("ModelPreview", preview_size, true, ImGuiWindowFlags_NoScrollbar);
            
            // Display a simple representation of the model
            if (mesh_comp && !mesh_comp->mesh_path.empty()) {
                ImGui::Text("Model: %s", mesh_comp->mesh_path.c_str());
            } else {
                ImGui::TextDisabled("[Default Cube]");
            }
            
            ImGui::EndChild();
            ImGui::NextColumn();
            
            // Right column: controls
            ImGui::Text("Model Scale");
            static float mesh_scale = 1.0f;
            if (ImGui::SliderFloat("Mesh Scale##separate", &mesh_scale, 0.1f, 5.0f)) {
                // This scale affects only the mesh rendering, not the entity transform
                // Store in mesh component or as a separate property
                editor_state.editor_scene->MarkModified();
            }
            ImGui::TextWrapped("Adjusts model size without affecting entity transform or colliders.");
            
            ImGui::Columns(1);
            ImGui::TreePop();
        }
        
        ImGui::Separator();
        ImGui::Text("Transform");
        
        auto transform = selected_entity->GetTransform();
        glm::vec3 pos = transform->GetLocalPosition();
        glm::vec3 rot = glm::degrees(glm::eulerAngles(transform->GetLocalRotation()));
        glm::vec3 scale = transform->GetLocalScale();
        
        if (ImGui::DragFloat3("Position##local", &pos[0], 0.1f)) {
            transform->SetLocalPosition(pos);
            editor_state.editor_scene->MarkModified();
        }
        
        if (ImGui::DragFloat3("Rotation##local", &rot[0], 0.5f)) {
            // Convert Euler angles back to quaternion
            glm::quat new_rot = glm::quat(glm::radians(rot));
            transform->SetLocalRotation(new_rot);
            editor_state.editor_scene->MarkModified();
        }
        
        if (ImGui::DragFloat3("Scale##local", &scale[0], 0.1f, 0.1f)) {
            transform->SetLocalScale(scale);
            editor_state.editor_scene->MarkModified();
        }
        
        // World space transform (read-only)
        if (ImGui::TreeNode("World Transform")) {
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
            
            glm::vec3 world_pos = transform->GetWorldPosition();
            glm::vec3 world_rot = glm::degrees(glm::eulerAngles(transform->GetWorldRotation()));
            glm::vec3 world_scale = transform->GetWorldScale();
            
            ImGui::DragFloat3("Position##world", &world_pos[0], 0.1f);
            ImGui::DragFloat3("Rotation##world", &world_rot[0], 0.5f);
            ImGui::DragFloat3("Scale##world", &world_scale[0], 0.1f);
            
            ImGui::PopStyleVar();
            ImGui::PopItemFlag();
            ImGui::TreePop();
        }
        
        // Hierarchy
        if (ImGui::TreeNode("Hierarchy")) {
            auto parent = selected_entity->GetParent();
            if (parent) {
                ImGui::Text("Parent: %s (ID: %u)", parent->GetName().c_str(), parent->GetId());
            } else {
                ImGui::Text("Parent: [none]");
            }
            
            const auto& children = selected_entity->GetChildren();
            ImGui::Text("Children: %zu", children.size());
            // BeginChild() must always be paired with EndChild() since ImGui
            // 1.89.5 — even when BeginChild returns false. Don't gate EndChild
            // on the BeginChild return value.
            if (!children.empty()) {
                ImGui::BeginChild("ChildrenList", ImVec2(0, 100), true);
                for (const auto& child : children) {
                    ImGui::Text("- %s (ID: %u)", child->GetName().c_str(), child->GetId());
                }
                ImGui::EndChild();
            }
            ImGui::TreePop();
        }
        
        // Components section
        ImGui::Separator();
        if (ImGui::TreeNode("Components")) {
            const auto& components = selected_entity->GetComponents();
            ImGui::Text("Total components: %zu", components.size());
            
            if (!components.empty()) {
                ImGui::Separator();
                ImGui::Text("Attached Components:");
                for (size_t i = 0; i < components.size(); ++i) {
                    ImGui::Text("  [%zu] %s", i, typeid(*components[i]).name());
                }
            }
            
            // Add component button
            ImGui::Separator();
            if (ImGui::Button("+ Add Component", ImVec2(-1, 0))) {
                ImGui::OpenPopup("AddComponentMenu");
            }
            
            // Add component popup menu
            if (ImGui::BeginPopupContextItem("AddComponentMenu", ImGuiPopupFlags_NoOpenOverExistingPopup)) {
                if (ImGui::MenuItem("Transform Component")) {
                    selected_entity->AddComponent<schizo::scene::TransformComponent>();
                    editor_state.editor_scene->MarkModified();
                    spdlog::info("Added TransformComponent to entity: {}", selected_entity->GetName());
                    ImGui::CloseCurrentPopup();
                }
                
                if (ImGui::BeginMenu("Light##add_light")) {
                    if (ImGui::MenuItem("Directional Light")) {
                        selected_entity->AddComponent<schizo::scene::LightComponent>(
                            schizo::scene::LightType::Directional,
                            "Directional Light"
                        );
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Added Directional LightComponent to entity: {}", selected_entity->GetName());
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::MenuItem("Point Light")) {
                        selected_entity->AddComponent<schizo::scene::LightComponent>(
                            schizo::scene::LightType::Point,
                            "Point Light"
                        );
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Added Point LightComponent to entity: {}", selected_entity->GetName());
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::MenuItem("Spot Light")) {
                        selected_entity->AddComponent<schizo::scene::LightComponent>(
                            schizo::scene::LightType::Spot,
                            "Spot Light"
                        );
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Added Spot LightComponent to entity: {}", selected_entity->GetName());
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndMenu();
                }
                
                ImGui::EndPopup();
            }
            
            ImGui::TreePop();
        }
        
        // Light Component Properties
        ImGui::Separator();
        auto light_comp = selected_entity->GetComponent<schizo::scene::LightComponent>();
        if (light_comp) {
            if (ImGui::TreeNode("Light")) {
                // Light type display
                const char* light_type_str;
                if (light_comp->GetType() == schizo::scene::LightType::Directional) {
                    light_type_str = "Directional (Sun)";
                } else if (light_comp->GetType() == schizo::scene::LightType::Point) {
                    light_type_str = "Point Light";
                } else {
                    light_type_str = "Spot Light";
                }
                ImGui::Text("Type: %s", light_type_str);
                
                // Enable/Disable
                bool enabled = light_comp->IsEnabled();
                if (ImGui::Checkbox("Enabled##light", &enabled)) {
                    light_comp->SetEnabled(enabled);
                    editor_state.editor_scene->MarkModified();
                }
                    
                    ImGui::Separator();
                    ImGui::Text("Color & Intensity");
                    
                    // Color picker
                    glm::vec3 color = light_comp->GetColor();
                    float color_arr[3] = {color.r, color.g, color.b};
                    if (ImGui::ColorEdit3("Color##light", color_arr)) {
                        light_comp->SetColor(color_arr[0], color_arr[1], color_arr[2]);
                        editor_state.editor_scene->MarkModified();
                    }
                    
                    // Intensity
                    float intensity = light_comp->GetIntensity();
                    if (ImGui::SliderFloat("Intensity##light", &intensity, 0.0f, 5.0f)) {
                        light_comp->SetIntensity(intensity);
                        editor_state.editor_scene->MarkModified();
                    }
                    
                    // Temperature
                    float temperature = light_comp->GetTemperature();
                    if (ImGui::SliderFloat("Temperature (K)##light", &temperature, 1000.0f, 10000.0f)) {
                        light_comp->SetTemperature(temperature);
                        editor_state.editor_scene->MarkModified();
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("(?)");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Color temperature in Kelvin\n1000K=Fire | 3000K=Warm | 6500K=Daylight | 9000K=Cool");
                    }
                    
                    // Light-specific properties
                    if (light_comp->GetType() != schizo::scene::LightType::Directional) {
                        ImGui::Separator();
                        ImGui::Text("Range");
                        
                        float range = light_comp->GetRange();
                        if (ImGui::SliderFloat("Range##light", &range, 0.1f, 100.0f)) {
                            light_comp->SetRange(range);
                            editor_state.editor_scene->MarkModified();
                        }
                    }
                    
                    if (light_comp->GetType() == schizo::scene::LightType::Spot) {
                        ImGui::Separator();
                        ImGui::Text("Spot Light");
                        
                        glm::vec2 angles = light_comp->GetSpotAngles();
                        if (ImGui::SliderFloat("Inner Angle##spot", &angles.x, 0.0f, 90.0f)) {
                            light_comp->SetSpotAngles(angles.x, angles.y);
                            editor_state.editor_scene->MarkModified();
                        }
                        if (ImGui::SliderFloat("Outer Angle##spot", &angles.y, 0.0f, 90.0f)) {
                            light_comp->SetSpotAngles(angles.x, angles.y);
                            editor_state.editor_scene->MarkModified();
                        }
                        
                        float falloff = light_comp->GetSpotFalloff();
                        if (ImGui::SliderFloat("Falloff##spot", &falloff, 0.1f, 10.0f)) {
                            light_comp->SetSpotFalloff(falloff);
                            editor_state.editor_scene->MarkModified();
                        }
                    }
                    
                    // Shadows section
                    ImGui::Separator();
                    if (ImGui::TreeNode("Shadows##light")) {
                        bool cast_shadow = light_comp->GetCastShadow();
                        if (ImGui::Checkbox("Cast Shadows##light", &cast_shadow)) {
                            light_comp->SetCastShadow(cast_shadow);
                            editor_state.editor_scene->MarkModified();
                        }
                        
                        if (cast_shadow) {
                            // Shadow quality
                            static const char* shadow_quality_names[] = {
                                "None", "Low (512x512)", "Medium (1024x1024)", "High (2048x2048)", "Ultra (4096x4096)"
                            };
                            static int shadow_quality_idx = 2;  // Default: Medium
                            
                        if (light_comp->GetShadowQuality() == schizo::scene::ShadowQuality::None) {
                            shadow_quality_idx = 0;
                        } else if (light_comp->GetShadowQuality() == schizo::scene::ShadowQuality::Low) {
                            shadow_quality_idx = 1;
                        } else if (light_comp->GetShadowQuality() == schizo::scene::ShadowQuality::Medium) {
                            shadow_quality_idx = 2;
                        } else if (light_comp->GetShadowQuality() == schizo::scene::ShadowQuality::High) {
                            shadow_quality_idx = 3;
                        } else if (light_comp->GetShadowQuality() == schizo::scene::ShadowQuality::Ultra) {
                            shadow_quality_idx = 4;
                        }
                        
                        if (ImGui::Combo("Resolution##shadow_quality", &shadow_quality_idx, shadow_quality_names, IM_ARRAYSIZE(shadow_quality_names))) {
                            schizo::scene::ShadowQuality qualities[] = {
                                schizo::scene::ShadowQuality::None,
                                schizo::scene::ShadowQuality::Low,
                                schizo::scene::ShadowQuality::Medium,
                                schizo::scene::ShadowQuality::High,
                                schizo::scene::ShadowQuality::Ultra
                            };
                            light_comp->SetShadowQuality(qualities[shadow_quality_idx]);
                            editor_state.editor_scene->MarkModified();
                        }
                        
                        ImGui::Separator();
                        ImGui::Text("Shadow Parameters");
                            
                            // Shadow bias
                            glm::vec2 shadow_bias = light_comp->GetShadowBias();
                            if (ImGui::SliderFloat("Bias##shadow", &shadow_bias.x, 0.0f, 0.1f)) {
                                light_comp->SetShadowBias(shadow_bias.x, shadow_bias.y);
                                editor_state.editor_scene->MarkModified();
                            }
                            ImGui::SameLine();
                            ImGui::TextDisabled("(?)");
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Reduces shadow acne artifacts");
                            }
                            
                            // Shadow filter radius
                            float filter_radius = light_comp->GetShadowFilterRadius();
                            if (ImGui::SliderFloat("Filter Radius##shadow", &filter_radius, 0.5f, 4.0f)) {
                                light_comp->SetShadowFilterRadius(filter_radius);
                                editor_state.editor_scene->MarkModified();
                            }
                            ImGui::SameLine();
                            ImGui::TextDisabled("(?)");
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Softness of shadow edges (PCF)");
                            }
                            
                            // Shadow planes
                            glm::vec2 shadow_planes = light_comp->GetShadowPlanes();
                            if (ImGui::SliderFloat("Near Plane##shadow", &shadow_planes.x, 0.01f, 10.0f)) {
                                light_comp->SetShadowPlanes(shadow_planes.x, shadow_planes.y);
                                editor_state.editor_scene->MarkModified();
                            }
                            if (ImGui::SliderFloat("Far Plane##shadow", &shadow_planes.y, 10.0f, 1000.0f)) {
                                light_comp->SetShadowPlanes(shadow_planes.x, shadow_planes.y);
                                editor_state.editor_scene->MarkModified();
                            }
                            
                            // Cascade count for directional lights
                            if (light_comp->GetType() == schizo::scene::LightType::Directional) {
                                uint32_t cascade_count = light_comp->GetCascadeCount();
                                int cascade_idx = static_cast<int>(cascade_count);
                                if (ImGui::SliderInt("Cascades##shadow", &cascade_idx, 1, 4)) {
                                    light_comp->SetCascadeCount(static_cast<uint32_t>(cascade_idx));
                                    editor_state.editor_scene->MarkModified();
                                }
                            }
                        }
                        
                        ImGui::TreePop();
                    }
                    
                    // Advanced features
                    ImGui::Separator();
                    if (ImGui::TreeNode("Advanced##light")) {
                        float volumetric = light_comp->GetVolumetricIntensity();
                        if (ImGui::SliderFloat("Volumetric Intensity##light", &volumetric, 0.0f, 1.0f)) {
                            light_comp->SetVolumetricIntensity(volumetric);
                            editor_state.editor_scene->MarkModified();
                        }
                        ImGui::SameLine();
                        ImGui::TextDisabled("(?)");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("God rays / Light shafts intensity");
                        }
                        
                        ImGui::TreePop();
                    }
                
                ImGui::TreePop();
            }
        }
        
        // Material Editor Section
        ImGui::Separator();
        if (ImGui::TreeNode("Mesh##inspector")) {
            ImGui::Text("Mesh Component");
            ImGui::Separator();
            
            // Mesh selector with drag-drop support
            ImGui::Text("Assign Mesh:");
            ImGui::InputText("Current Mesh##mesh_selector", const_cast<char*>("(drag asset here)"), 256, ImGuiInputTextFlags_ReadOnly);
            
            // Drag-drop target for mesh assignment
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MESH_ASSET")) {
                    size_t asset_idx = *reinterpret_cast<const size_t*>(payload->Data);
                    if (editor_state.asset_browser) {
                        const auto* dragged_asset = editor_state.asset_browser->GetAssetByIndex(asset_idx);
                        if (dragged_asset && dragged_asset->asset_type == "Mesh") {
                            if (mesh_comp) {
                                // Use the full path (supports absolute paths from outside project)
                                mesh_comp->SetMesh(dragged_asset->path);
                                spdlog::info("[Inspector] Assigned mesh: {}", dragged_asset->path);
                                editor_state.editor_scene->MarkModified();
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            
            // Show current mesh info
            if (mesh_comp && !mesh_comp->mesh_path.empty()) {
                ImGui::Separator();
                ImGui::Text("Current Mesh: %s", mesh_comp->mesh_path.c_str());
                ImGui::Text("Vertices: %u (if loaded)", mesh_comp->GetMeshAsset() ? 0 : 0);
                
                if (ImGui::Button("Clear Mesh##mesh", ImVec2(-1, 0))) {
                    mesh_comp->SetMesh("");
                    editor_state.editor_scene->MarkModified();
                    spdlog::info("[Inspector] Cleared mesh assignment");
                }
            } else {
                ImGui::TextDisabled("No mesh assigned");
            }
            
            ImGui::TreePop();
        }
        
        // Material Editor Section
        ImGui::Separator();
        if (ImGui::TreeNode("Material##inspector")) {
            if (editor_state.material_editor) {
                editor_state.material_editor->Render(selected_entity);
            } else {
                ImGui::Text("Material editor not initialized");
            }
            ImGui::TreePop();
        }
        
        // Physics Section
        ImGui::Separator();
        if (ImGui::TreeNode("Physics")) {
            static bool use_physics = false;
            if (ImGui::Checkbox("Enable Physics##phys", &use_physics)) {
                editor_state.editor_scene->MarkModified();
            }
            
            if (use_physics) {
                ImGui::Separator();
                ImGui::Text("Rigidbody:");
                static float mass = 1.0f;
                static bool gravity = true;
                
                if (ImGui::DragFloat("Mass##phys", &mass, 0.1f, 0.01f, 100.0f)) {
                    editor_state.editor_scene->MarkModified();
                }
                
                if (ImGui::Checkbox("Use Gravity##phys", &gravity)) {
                    editor_state.editor_scene->MarkModified();
                }
                
                ImGui::Separator();
                ImGui::Text("Collider:");
                static int collider_type = 0;
                const char* collider_types[] = { "None", "Box", "Sphere", "Capsule", "Mesh" };
                
                if (ImGui::Combo("Collider Type##phys", &collider_type, collider_types, IM_ARRAYSIZE(collider_types))) {
                    editor_state.editor_scene->MarkModified();
                    spdlog::info("Set collider type to: {}", collider_types[collider_type]);
                }
                
                if (collider_type > 0) {
                    static glm::vec3 collider_scale(1.0f, 1.0f, 1.0f);
                    static bool is_trigger = false;
                    
                    ImGui::DragFloat3("Collider Scale##phys", &collider_scale[0], 0.1f);
                    ImGui::Checkbox("Is Trigger##phys", &is_trigger);
                }
            }
            
            ImGui::TreePop();
        }
        
        // Gizmo Controls Section
        ImGui::Separator();
        if (ImGui::TreeNode("Gizmo")) {
            const char* gizmo_modes[] = { "None", "Translate", "Rotate", "Scale" };
            int current_mode = static_cast<int>(editor_state.transform_gizmo.GetMode());
            
            if (ImGui::Combo("Gizmo Mode##insp", &current_mode, gizmo_modes, IM_ARRAYSIZE(gizmo_modes))) {
                editor_state.transform_gizmo.SetMode(static_cast<schizo::editor::GizmoMode>(current_mode));
            }
            
            ImGui::Text("Hint: Use T/R/S keys in viewport to toggle modes");
            ImGui::Text("Drag to transform, right-click to cancel");
            ImGui::TreePop();
        }
        
        ImGui::Separator();
        
        if (ImGui::Button("Delete Entity", ImVec2(-1, 0))) {
            spdlog::info("Deleted entity: {}", selected_entity->GetName());
            scene->RemoveEntity(selected_entity);
            editor_state.selected_entity_id = 0;
            editor_state.editor_scene->MarkModified();
        }
        
        ImGui::End();
    }
}

void ShowAssetBrowser(EditorState& editor_state) {
    if (!editor_state.show_asset_browser) return;

    if (!editor_state.asset_browser) {
        ImGui::Begin("Asset Browser", &editor_state.show_asset_browser);
        ImGui::Text("Asset browser not initialized");
        ImGui::End();
        return;
    }

    // The panel opens its own ImGui window ("Asset Browser##panel"); don't
    // wrap it in another Begin/End or two windows render side by side.
    auto scene = editor_state.editor_scene->GetScene();
    editor_state.asset_browser->Render(scene, &editor_state.show_asset_browser);
}

void ShowViewport(EditorState& editor_state) {
    if (!editor_state.show_viewport) return;
    
    if (ImGui::Begin("Viewport", &editor_state.show_viewport, ImGuiWindowFlags_NoMove)) {
        ImVec2 content_area = ImGui::GetContentRegionAvail();
        
        // Play mode indicator and controls
        if (editor_state.is_playing) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));  // Green text
            ImGui::Text(">>> PLAY MODE (%.2f s) <<<", editor_state.play_time);
            ImGui::PopStyleColor();
        } else {
            ImGui::Text("Edit Mode");
        }
        ImGui::SameLine();
        
        if (ImGui::Button(editor_state.is_playing ? "Stop (F5)" : "Play (F5)")) {
            editor_state.is_playing = !editor_state.is_playing;
            editor_state.play_time = 0.0f;
        }
        ImGui::SameLine();
        
        if (ImGui::Button("Reset Camera")) {
            editor_state.viewport_camera.Reset();
        }
        ImGui::Separator();
        
        auto scene = editor_state.editor_scene->GetScene();
        
        // Display viewport info
        ImGui::Text("Viewport: %.0f x %.0f", content_area.x, content_area.y);
        auto cam_pos = editor_state.viewport_camera.GetPosition();
        ImGui::Text("Camera: (%.1f, %.1f, %.1f)", cam_pos.x, cam_pos.y, cam_pos.z);
        float normalized_yaw = fmod(editor_state.viewport_camera.GetYaw(), 360.0f);
        if (normalized_yaw < 0.0f) normalized_yaw += 360.0f;
        ImGui::Text("Rotation: Yaw %.1f° Pitch %.1f°", normalized_yaw, editor_state.viewport_camera.GetPitch());
        ImGui::Text("Entities: %zu", scene ? scene->GetEntityCount() : 0);
        
        ImGui::Separator();
        ImGui::TextWrapped("Middle Mouse: Rotate | Scroll: Zoom | Right Drag: Pan");
        
        // Gizmo controls
        ImGui::Separator();
        ImGui::Checkbox("Show Gizmo", &editor_state.show_gizmo);
        ImGui::SameLine();
        
        const char* gizmo_modes[] = { "None", "Translate (T)", "Rotate (R)", "Scale (S)" };
        int gizmo_mode = static_cast<int>(editor_state.transform_gizmo.GetMode());
        if (ImGui::Combo("Gizmo Mode", &gizmo_mode, gizmo_modes, IM_ARRAYSIZE(gizmo_modes))) {
            editor_state.transform_gizmo.SetMode(static_cast<schizo::editor::GizmoMode>(gizmo_mode));
        }
        
        ImGui::Separator();
        ImGui::Checkbox("Wireframe Mode", &editor_state.wireframe_mode);
        
        ImGui::Separator();
        
        // Prepare view and projection matrices
        ImVec2 viewport_size = ImGui::GetContentRegionAvail();
        float aspect = viewport_size.x > 0 ? viewport_size.x / viewport_size.y : 1.0f;
        if (viewport_size.x > 50.0f && viewport_size.y > 50.0f)
            editor_state.viewport_panel_size = {viewport_size.x, viewport_size.y};
        
        glm::mat4 view_matrix;
        glm::mat4 proj_matrix;
        
        // Use playback camera if scene is playing
        if (editor_state.scene_playback_manager && editor_state.scene_playback_manager->IsPlaying()) {
            auto playback_camera = editor_state.scene_playback_manager->GetPlaybackCamera();
            
            if (!playback_camera) {
                spdlog::warn("⚠️ IsPlaying=true but GetPlaybackCamera() returned nullptr!");
                view_matrix = editor_state.viewport_camera.GetViewMatrix();
                proj_matrix = editor_state.viewport_camera.GetProjectionMatrix(aspect);
            } else {
                auto camera_transform = playback_camera->GetTransform();
                if (!camera_transform) {
                    spdlog::warn("⚠️ Playback camera exists but has no Transform!");
                    view_matrix = editor_state.viewport_camera.GetViewMatrix();
                    proj_matrix = editor_state.viewport_camera.GetProjectionMatrix(aspect);
                } else {
                    // Use the camera's WORLD rotation (so a first-person camera
                    // parented to a rotating player faces the player's forward,
                    // not its own local -Z).
                    glm::vec3 cam_pos = camera_transform->GetWorldPosition();
                    glm::quat cam_rot = camera_transform->GetWorldRotation();
                    glm::vec3 cam_forward = cam_rot * glm::vec3(0.0f, 0.0f, -1.0f);
                    glm::vec3 cam_up      = cam_rot * glm::vec3(0.0f, 1.0f,  0.0f);

                    view_matrix = glm::lookAt(cam_pos, cam_pos + cam_forward, cam_up);
                    proj_matrix = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 1000.0f);
                }
            }
        } else {
            // Normal editor camera
            spdlog::debug("🎥 VIEWPORT using editor camera (not playing)");
            view_matrix = editor_state.viewport_camera.GetViewMatrix();
            proj_matrix = editor_state.viewport_camera.GetProjectionMatrix(aspect);
        }
        
        // Display deferred pipeline output as viewport background
        ImVec2 image_min(0, 0), image_max(0, 0);
        bool image_drawn = false;
        if (viewport_size.x > 50.0f && viewport_size.y > 50.0f) {
            if (editor_state.viewport_texture_id != VK_NULL_HANDLE) {
                ImGui::Image((ImTextureID)(void*)editor_state.viewport_texture_id, viewport_size);
                image_min = ImGui::GetItemRectMin();
                image_max = ImGui::GetItemRectMax();
                image_drawn = true;
            } else {
                ImGui::TextDisabled("Vulkan viewport initializing...");
                ImGui::Text("Scene: %zu entities", scene ? scene->GetEntityCount() : 0);
            }
        }

        // ----------------------------------------------------------------
        // Gizmo overlay — draw colored axis lines on top of the viewport
        // image for the selected entity using ImGui's draw list. Picking
        // (later in this function) uses the same world-space axes.
        // ----------------------------------------------------------------
        if (image_drawn && editor_state.show_gizmo &&
            scene && editor_state.selected_entity_id != 0)
        {
            auto sel = scene->GetEntityById(editor_state.selected_entity_id);
            if (sel) {
                glm::vec3 origin_w = sel->GetTransform()->GetWorldPosition();
                const float gizmo_size = 3.5f;

                auto to_screen = [&](glm::vec3 w) -> ImVec2 {
                    // Match the renderer: GL-style projection, Vulkan Y-flip
                    // applied here so the overlay aligns with the rendered image.
                    glm::mat4 p = proj_matrix;
                    p[1][1] *= -1.0f;
                    glm::vec4 c = p * view_matrix * glm::vec4(w, 1.0f);
                    if (c.w <= 0.0f) return ImVec2(-1e9f, -1e9f); // behind cam
                    glm::vec3 ndc = glm::vec3(c) / c.w;
                    return ImVec2(
                        image_min.x + (ndc.x * 0.5f + 0.5f) * viewport_size.x,
                        image_min.y + (ndc.y * 0.5f + 0.5f) * viewport_size.y);
                };

                auto mode = editor_state.transform_gizmo.GetMode();

                // Scale gizmo follows the entity's local axes; translate and
                // rotate stay world-aligned (matches the picking math).
                glm::vec3 ax_x(1, 0, 0), ax_y(0, 1, 0), ax_z(0, 0, 1);
                if (mode == schizo::editor::GizmoMode::Scale) {
                    glm::quat rot = sel->GetTransform()->GetWorldRotation();
                    ax_x = rot * ax_x;
                    ax_y = rot * ax_y;
                    ax_z = rot * ax_z;
                }

                ImVec2 o  = to_screen(origin_w);
                ImVec2 ex = to_screen(origin_w + ax_x * gizmo_size);
                ImVec2 ey = to_screen(origin_w + ax_y * gizmo_size);
                ImVec2 ez = to_screen(origin_w + ax_z * gizmo_size);

                auto* dl = ImGui::GetWindowDrawList();
                // Strictly clip the gizmo to the rendered viewport image so it
                // never bleeds onto adjacent panels (Inspector, Hierarchy, etc.)
                // or the viewport's own toolbar/text area above the image.
                dl->PushClipRect(image_min, image_max, true);

                ImU32 col_x = IM_COL32(230, 60, 60, 255);
                ImU32 col_y = IM_COL32(60, 230, 60, 255);
                ImU32 col_z = IM_COL32(60, 110, 230, 255);
                ImU32 col_hover = IM_COL32(255, 230, 80, 255);
                if (editor_state.gizmo_axis == 'x') col_x = col_hover;
                if (editor_state.gizmo_axis == 'y') col_y = col_hover;
                if (editor_state.gizmo_axis == 'z') col_z = col_hover;

                if (mode == schizo::editor::GizmoMode::Translate ||
                    mode == schizo::editor::GizmoMode::None)
                {
                    // Lines + arrowheads for translate.
                    dl->AddLine(o, ex, col_x, 3.0f);
                    dl->AddLine(o, ey, col_y, 3.0f);
                    dl->AddLine(o, ez, col_z, 3.0f);
                    auto tip = [&](ImVec2 p, ImU32 col) {
                        dl->AddCircleFilled(p, 5.0f, col);
                    };
                    tip(ex, col_x); tip(ey, col_y); tip(ez, col_z);
                } else if (mode == schizo::editor::GizmoMode::Scale) {
                    // Lines with cubes at the tips for scale.
                    dl->AddLine(o, ex, col_x, 3.0f);
                    dl->AddLine(o, ey, col_y, 3.0f);
                    dl->AddLine(o, ez, col_z, 3.0f);
                    auto box = [&](ImVec2 p, ImU32 col) {
                        dl->AddRectFilled(ImVec2(p.x - 5, p.y - 5),
                                          ImVec2(p.x + 5, p.y + 5), col);
                    };
                    box(ex, col_x); box(ey, col_y); box(ez, col_z);
                } else if (mode == schizo::editor::GizmoMode::Rotate) {
                    // Three orthogonal rings projected to screen — sample the
                    // ring on each axis-plane and draw a polyline.
                    auto ring = [&](glm::vec3 a, glm::vec3 b, ImU32 col) {
                        const int N = 48;
                        ImVec2 prev{};
                        bool prev_valid = false;
                        for (int i = 0; i <= N; ++i) {
                            float t = (float)i / N * 6.2831853f;
                            glm::vec3 p = origin_w + (a * std::cos(t) + b * std::sin(t))
                                          * gizmo_size;
                            ImVec2 s = to_screen(p);
                            if (prev_valid) dl->AddLine(prev, s, col, 2.0f);
                            prev = s; prev_valid = true;
                        }
                    };
                    ring(glm::vec3(0,1,0), glm::vec3(0,0,1), col_x); // YZ plane (rotate around X)
                    ring(glm::vec3(1,0,0), glm::vec3(0,0,1), col_y); // XZ plane (rotate around Y)
                    ring(glm::vec3(1,0,0), glm::vec3(0,1,0), col_z); // XY plane (rotate around Z)
                }
                dl->AddCircleFilled(o, 4.0f, IM_COL32(220, 220, 220, 255));

                dl->PopClipRect();
            }
        }

        // Camera controls - only process when viewport is hovered
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
            ImGuiIO& io = ImGui::GetIO();
            
            // Handle entity selection through picking and gizmo interaction
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && scene && image_drawn) {
                ImVec2 mouse_pos = io.MousePos;
                // Use the image's actual rect (captured immediately after
                // ImGui::Image), not GetCursorScreenPos, which points BELOW
                // the image and made all picking math off by image height.
                float viewport_x = mouse_pos.x - image_min.x;
                float viewport_y = mouse_pos.y - image_min.y;

                // Clamp to viewport bounds
                viewport_x = std::max(0.0f, std::min(viewport_x, viewport_size.x));
                viewport_y = std::max(0.0f, std::min(viewport_y, viewport_size.y));
                
                if (viewport_x >= 0 && viewport_y >= 0 && viewport_x < viewport_size.x && viewport_y < viewport_size.y) {
                    // Get picking ray
                    auto [ray_origin, ray_direction] = editor_state.viewport_camera.GetPickingRay(
                        viewport_x, viewport_y, viewport_size.x, viewport_size.y
                    );
                    
                    // First check if gizmo is visible and try to hit an axis
                    if (editor_state.show_gizmo && editor_state.selected_entity_id != 0) {
                        auto selected_entity = scene->GetEntityById(editor_state.selected_entity_id);
                        if (selected_entity) {
                            auto selected_transform = selected_entity->GetTransform();
                            auto selected_pos = selected_transform->GetWorldPosition();
                            const float gizmo_render_scale = 3.5f;  // Matches rendering scale
                            const float gizmo_axis_length = 1.0f;   // Base axis length in mesh
                            const float gizmo_size = gizmo_render_scale * gizmo_axis_length;  // Actual visible size
                            
                            // Check intersection with X axis (red)
                            glm::vec3 x_start = selected_pos;
                            glm::vec3 x_end = selected_pos + glm::vec3(gizmo_size, 0.0f, 0.0f);
                            float x_dist = schizo::editor::ViewportCamera::RayLineDistanceSq(
                                ray_origin, ray_direction, x_start, x_end
                            );
                            
                            // Check intersection with Y axis (green)
                            glm::vec3 y_start = selected_pos;
                            glm::vec3 y_end = selected_pos + glm::vec3(0.0f, gizmo_size, 0.0f);
                            float y_dist = schizo::editor::ViewportCamera::RayLineDistanceSq(
                                ray_origin, ray_direction, y_start, y_end
                            );
                            
                            // Check intersection with Z axis (blue)
                            glm::vec3 z_start = selected_pos;
                            glm::vec3 z_end = selected_pos + glm::vec3(0.0f, 0.0f, gizmo_size);
                            float z_dist = schizo::editor::ViewportCamera::RayLineDistanceSq(
                                ray_origin, ray_direction, z_start, z_end
                            );
                            
                            // Determine which axis was hit (closest)
                            // Select the closest axis within threshold
                            float hit_threshold = 2.0f;  // Generous hit zone
                            float min_dist = std::min({x_dist, y_dist, z_dist});
                            
                            if (min_dist < hit_threshold) {
                                editor_state.gizmo_dragging = true;
                                editor_state.gizmo_drag_start = glm::vec2(mouse_pos.x, mouse_pos.y);
                                editor_state.gizmo_drag_offset = glm::vec3(0.0f);
                                
                                // Select the closest axis
                                if (x_dist == min_dist) {
                                    editor_state.gizmo_axis = 'x';
                                    spdlog::info("Gizmo X-axis grabbed (dist: {:.3f})", x_dist);
                                } else if (y_dist == min_dist) {
                                    editor_state.gizmo_axis = 'y';
                                    spdlog::info("Gizmo Y-axis grabbed (dist: {:.3f})", y_dist);
                                } else {
                                    editor_state.gizmo_axis = 'z';
                                    spdlog::info("Gizmo Z-axis grabbed (dist: {:.3f})", z_dist);
                                }
                            } else {
                                // No gizmo hit, select entity normally
                                uint32_t closest_entity_id = 0;
                                float closest_distance = std::numeric_limits<float>::max();
                                
                                const auto& entities = scene->GetEntities();
                                for (const auto& entity : entities) {
                                    auto transform = entity->GetTransform();
                                    auto pos = transform->GetWorldPosition();
                                    auto scale = transform->GetLocalScale() * 0.5f;
                                    
                                    // AABB bounds
                                    glm::vec3 aabb_min = pos - scale;
                                    glm::vec3 aabb_max = pos + scale;
                                    
                                    // Test intersection
                                    float distance = schizo::editor::ViewportCamera::RayAABBIntersection(
                                        ray_origin, ray_direction, aabb_min, aabb_max
                                    );
                                    
                                    if (distance > 0 && distance < closest_distance) {
                                        closest_distance = distance;
                                        closest_entity_id = entity->GetId();
                                    }
                                }
                                
                                if (closest_entity_id != 0) {
                                    editor_state.selected_entity_id = closest_entity_id;
                                    spdlog::info("Entity selected via picking: ID {}", closest_entity_id);
                                }
                            }
                        }
                    } else {
                        // No gizmo visible or no selection, do normal entity picking
                        uint32_t closest_entity_id = 0;
                        float closest_distance = std::numeric_limits<float>::max();
                        
                        const auto& entities = scene->GetEntities();
                        for (const auto& entity : entities) {
                            auto transform = entity->GetTransform();
                            auto pos = transform->GetWorldPosition();
                            auto scale = transform->GetLocalScale() * 0.5f;
                            
                            // AABB bounds
                            glm::vec3 aabb_min = pos - scale;
                            glm::vec3 aabb_max = pos + scale;
                            
                            // Test intersection
                            float distance = schizo::editor::ViewportCamera::RayAABBIntersection(
                                ray_origin, ray_direction, aabb_min, aabb_max
                            );
                            
                            if (distance > 0 && distance < closest_distance) {
                                closest_distance = distance;
                                closest_entity_id = entity->GetId();
                            }
                        }
                        
                        if (closest_entity_id != 0) {
                            editor_state.selected_entity_id = closest_entity_id;
                            spdlog::info("Entity selected via picking: ID {}", closest_entity_id);
                        }
                    }
                }
            }
            
            // Handle gizmo dragging
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && editor_state.gizmo_dragging && scene) {
                ImVec2 current_mouse = io.MousePos;
                glm::vec2 current_mouse_glm(current_mouse.x, current_mouse.y);
                
                auto selected_entity = scene->GetEntityById(editor_state.selected_entity_id);
                if (selected_entity) {
                    auto selected_transform = selected_entity->GetTransform();
                    
                    // Determine axis from gizmo_axis character
                    schizo::editor::GizmoAxis axis = schizo::editor::GizmoAxis::None;
                    if (editor_state.gizmo_axis == 'x') {
                        axis = schizo::editor::GizmoAxis::X;
                    } else if (editor_state.gizmo_axis == 'y') {
                        axis = schizo::editor::GizmoAxis::Y;
                    } else if (editor_state.gizmo_axis == 'z') {
                        axis = schizo::editor::GizmoAxis::Z;
                    }
                    
                    // Begin drag on first frame
                    if (!editor_state.transform_gizmo.IsDragging()) {
                        editor_state.transform_gizmo.BeginDrag(axis, editor_state.gizmo_drag_start);
                    }
                    
                    // Get mode-specific updates
                    auto gmode = editor_state.transform_gizmo.GetMode();
                    if (gmode == schizo::editor::GizmoMode::Translate) {
                        glm::vec3 current_pos = selected_transform->GetLocalPosition();
                        glm::vec3 new_pos = editor_state.transform_gizmo.UpdateDrag(current_mouse_glm, current_pos);
                        selected_transform->SetLocalPosition(new_pos);
                    } else if (gmode == schizo::editor::GizmoMode::Rotate) {
                        // Mouse delta along its dominant axis drives a
                        // rotation around the selected gizmo axis. ~0.5°/pixel.
                        glm::vec2 delta = current_mouse_glm - editor_state.gizmo_drag_start;
                        editor_state.gizmo_drag_start = current_mouse_glm;
                        float scalar = (editor_state.gizmo_axis == 'y') ? -delta.y : delta.x;
                        float angle  = glm::radians(scalar * 0.5f);
                        glm::vec3 axis(0.0f);
                        if (editor_state.gizmo_axis == 'x') axis = glm::vec3(1, 0, 0);
                        if (editor_state.gizmo_axis == 'y') axis = glm::vec3(0, 1, 0);
                        if (editor_state.gizmo_axis == 'z') axis = glm::vec3(0, 0, 1);
                        if (axis != glm::vec3(0.0f) && std::abs(angle) > 1e-6f) {
                            glm::quat q = glm::angleAxis(angle, axis);
                            selected_transform->SetLocalRotation(q * selected_transform->GetLocalRotation());
                        }
                    } else if (gmode == schizo::editor::GizmoMode::Scale) {
                        glm::vec3 current_scale = selected_transform->GetLocalScale();
                        glm::vec3 new_scale = editor_state.transform_gizmo.UpdateDrag(current_mouse_glm, current_scale);
                        // Ensure scale doesn't go below minimum
                        new_scale = glm::max(new_scale, glm::vec3(0.01f));
                        selected_transform->SetLocalScale(new_scale);
                    }

                    editor_state.editor_scene->MarkModified();
                }
            } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                // Mouse released, stop gizmo dragging
                if (editor_state.gizmo_dragging) {
                    editor_state.transform_gizmo.EndDrag();
                }
                editor_state.gizmo_dragging = false;
                editor_state.gizmo_axis = 0;
            }
            
            // Middle mouse drag to rotate
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                ImVec2 current_mouse = io.MousePos;
                if (editor_state.viewport_camera_rotating) {
                    auto current_pos = glm::vec2(current_mouse.x, current_mouse.y);
                    float dx = current_pos.x - editor_state.last_mouse_pos.x;
                    float dy = current_pos.y - editor_state.last_mouse_pos.y;
                    editor_state.viewport_camera.Rotate(dx, -dy);
                }
                editor_state.viewport_camera_rotating = true;
                editor_state.last_mouse_pos = glm::vec2(current_mouse.x, current_mouse.y);
            } else {
                editor_state.viewport_camera_rotating = false;
            }
            
            // Scroll to zoom (only if not dragging gizmo)
            if (io.MouseWheel != 0.0f && !editor_state.gizmo_dragging) {
                editor_state.viewport_camera.Zoom(io.MouseWheel);
            }
            
            // Arrow key camera movement (camera speed = 1.0)
            // Only when viewport is focused (active window) and not dragging gizmo
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !editor_state.gizmo_dragging) {
                if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) {
                    spdlog::debug("[ARROW] Up key - pan forward");
                    editor_state.viewport_camera.Pan(0.0f, 1.0f);  // Forward
                }
                if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) {
                    spdlog::debug("[ARROW] Down key - pan backward");
                    editor_state.viewport_camera.Pan(0.0f, -1.0f);  // Backward
                }
                if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
                    spdlog::debug("[ARROW] Left key - pan left");
                    editor_state.viewport_camera.Pan(-1.0f, 0.0f);  // Left
                }
                if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
                    spdlog::debug("[ARROW] Right key - pan right");
                    editor_state.viewport_camera.Pan(1.0f, 0.0f);   // Right
                }
                
                // Vertical camera movement (Q/E keys for intuitive up/down)
                if (ImGui::IsKeyDown(ImGuiKey_Q)) {
                    spdlog::debug("[Q] Move camera up");
                    editor_state.viewport_camera.MoveLocal(0.0f, 0.0f, 1.0f);  // Up
                }
                if (ImGui::IsKeyDown(ImGuiKey_E)) {
                    spdlog::debug("[E] Move camera down");
                    editor_state.viewport_camera.MoveLocal(0.0f, 0.0f, -1.0f);  // Down
                }
            }
            
            // Right mouse drag to pan (only if not dragging gizmo)
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right) && !editor_state.gizmo_dragging) {
                ImVec2 current_mouse = io.MousePos;
                ImVec2 delta = ImVec2(
                    (io.MousePos.x - io.MousePosPrev.x) / viewport_size.x,
                    (io.MousePos.y - io.MousePosPrev.y) / viewport_size.y
                );
                editor_state.viewport_camera.Pan(-delta.x * 10.0f, delta.y * 10.0f);
            }
            
            // Gizmo drag handling
            
            // DROP TARGET - Viewport can receive mesh assets
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* mesh_payload = ImGui::AcceptDragDropPayload("MESH_ASSET")) {
                    const char* mesh_path = (const char*)mesh_payload->Data;
                    if (scene && editor_state.selected_entity_id != 0) {
                        auto entity = scene->GetEntityById(editor_state.selected_entity_id);
                        if (entity) {
                            entity->SetMesh(mesh_path);
                            spdlog::info("Assigned mesh '{}' to entity '{}' via viewport drop", mesh_path, entity->GetName());
                            editor_state.editor_scene->MarkModified();
                        }
                    } else {
                        spdlog::warn("Cannot assign mesh: no entity selected");
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
        ImGui::End();
    }
}

void ShowPreferences(EditorState& editor_state) {
    if (!editor_state.show_preferences) return;
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove;  // Fixed position
    if (editor_state.gizmo_dragging) {
        flags |= ImGuiWindowFlags_NoInputs;  // Disable input when dragging in viewport
    }
    
    if (ImGui::Begin("Preferences", &editor_state.show_preferences, flags)) {
        ImGui::Text("Editor Settings");
        ImGui::Separator();
        ImGui::Checkbox("Vsync", nullptr);
        ImGui::Checkbox("Show Grid", nullptr);
        ImGui::SliderFloat("Grid Size", nullptr, 0.1f, 10.0f);
        ImGui::Separator();
        
        if (ImGui::Button("Close")) {
            editor_state.show_preferences = false;
        }
        
        ImGui::End();
    }
}

// ============================================================================
// Scene Playback Controls
// ============================================================================

void ShowPlaybackControls(EditorState& editor_state) {
    if (!editor_state.show_playback_controls) return;
    
    // Playback controls in toolbar style
    ImGui::SetNextWindowPos(ImVec2(10, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 80), ImGuiCond_FirstUseEver);
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    if (ImGui::Begin("Scene Playback", &editor_state.show_playback_controls, flags)) {
        ImGui::TextUnformatted("Scene Playback Controls:");
        ImGui::Separator();
        
        auto scene = editor_state.editor_scene->GetScene();
        bool can_play = scene && !editor_state.scene_playback_manager->IsPlaying();
        
        // Play button
        ImGui::BeginDisabled(!can_play);
        if (ImGui::Button("Play (F5)##playback", ImVec2(80, 0))) {
            if (editor_state.scene_playback_manager->StartPlayback(scene)) {
                spdlog::info("Scene playback started");
            } else {
                spdlog::warn("Failed to start scene playback");
            }
        }
        ImGui::EndDisabled();
        
        ImGui::SameLine();
        
        // Pause button
        ImGui::BeginDisabled(!editor_state.scene_playback_manager->IsPlaying());
        if (ImGui::Button("Pause##playback", ImVec2(80, 0))) {
            bool is_paused = editor_state.scene_playback_manager->IsPaused();
            editor_state.scene_playback_manager->SetPaused(!is_paused);
            spdlog::info(is_paused ? "Resumed playback" : "Paused playback");
        }
        ImGui::EndDisabled();
        
        ImGui::SameLine();
        
        // Stop button
        ImGui::BeginDisabled(!editor_state.scene_playback_manager->IsPlaying());
        if (ImGui::Button("Stop##playback", ImVec2(80, 0))) {
            editor_state.scene_playback_manager->StopPlayback();
            spdlog::info("Scene playback stopped");
        }
        ImGui::EndDisabled();
        
        // Status display
        ImGui::Spacing();
        if (editor_state.scene_playback_manager->IsPlaying()) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Status: Playing");
        } else if (editor_state.scene_playback_manager->IsPaused()) {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Status: Paused");
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "Status: Stopped");
        }
        
        ImGui::End();
    }
}

// ============================================================================
// Debug Panels Window
// ============================================================================

void ShowDebugPanels(EditorState& editor_state) {
    if (!editor_state.show_debug_panels) return;
    
    ImGui::SetNextWindowPos(ImVec2(1500, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 800), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Debug Systems", &editor_state.show_debug_panels)) {
        ImGui::TextUnformatted("Phase 6 System Debug Tools:");
        ImGui::Separator();
        ImGui::TextDisabled("Debug panels temporarily disabled due to ImGui state issues.");
        ImGui::TextDisabled("These will be re-enabled after refactoring the panel hierarchy.");
        
        // TODO: Character Controller Panel - currently disabled due to Begin/End mismatch
        // if (editor_state.character_panel) {
        //     if (ImGui::CollapsingHeader("Character Controller##debug", ImGuiTreeNodeFlags_DefaultOpen)) {
        //         editor_state.character_panel->Render(editor_state.selected_character_controller);
        //     }
        // }
        
        // TODO: Ability System Panel - currently disabled due to Begin/End mismatch  
        // if (editor_state.ability_panel) {
        //     if (ImGui::CollapsingHeader("Ability System##debug", ImGuiTreeNodeFlags_DefaultOpen)) {
        //         editor_state.ability_panel->Render(editor_state.selected_ability_system);
        //     }
        // }
        
        // TODO: Network System Panel - currently disabled due to Begin/End mismatch
        // if (editor_state.network_panel) {
        //     if (ImGui::CollapsingHeader("Network System##debug", ImGuiTreeNodeFlags_DefaultOpen)) {
        //         editor_state.network_panel->Render(editor_state.network_manager);
        //     }
        // }
        
        ImGui::End();
    }
}

// ============================================================================
// Blit post-processing output to swapchain, leaving swapchain in
// TRANSFER_DST_OPTIMAL so the ImGui render pass can take it from there.
// ============================================================================

static void blit_to_swapchain(VkCommandBuffer cmd,
                               VkImage src_image,
                               VkExtent2D src_extent,
                               VkImage swapchain_image,
                               VkExtent2D dst_extent) {
    VkImageMemoryBarrier src_to_transfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    src_to_transfer.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    src_to_transfer.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    src_to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_to_transfer.image               = src_image;
    src_to_transfer.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    src_to_transfer.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    src_to_transfer.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkImageMemoryBarrier dst_to_transfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    dst_to_transfer.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    dst_to_transfer.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    dst_to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dst_to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dst_to_transfer.image               = swapchain_image;
    dst_to_transfer.srcAccessMask       = 0;
    dst_to_transfer.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    dst_to_transfer.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkImageMemoryBarrier pre_barriers[] = {src_to_transfer, dst_to_transfer};
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 2, pre_barriers);

    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0]  = {0, 0, 0};
    blit.srcOffsets[1]  = {(int32_t)src_extent.width, (int32_t)src_extent.height, 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[0]  = {0, 0, 0};
    blit.dstOffsets[1]  = {(int32_t)dst_extent.width, (int32_t)dst_extent.height, 1};
    vkCmdBlitImage(cmd,
                   src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapchain_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, VK_FILTER_LINEAR);

    // Restore post-processing output to SHADER_READ_ONLY for ImGui::Image display.
    // Swapchain stays in TRANSFER_DST_OPTIMAL — the ImGui render pass transitions it.
    VkImageMemoryBarrier src_restore{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    src_restore.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    src_restore.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    src_restore.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_restore.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_restore.image               = src_image;
    src_restore.srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    src_restore.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    src_restore.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &src_restore);
}

// ============================================================================
// main()
// ============================================================================

int main() {
    try {
        spdlog::set_level(spdlog::level::info);
        spdlog::info("=== Project Schizo Editor (Vulkan) ===");

        // ----------------------------------------------------------------
        // GLFW + window (Vulkan, no OpenGL context)
        // ----------------------------------------------------------------
        if (!glfwInit()) {
            spdlog::error("glfwInit failed");
            return 1;
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE,  GLFW_TRUE);

        constexpr uint32_t kW = 1920, kH = 1080;
        GLFWwindow* glfw_window = glfwCreateWindow(kW, kH,
                                                    "Project Schizo - Editor",
                                                    nullptr, nullptr);
        if (!glfw_window) {
            spdlog::error("glfwCreateWindow failed");
            glfwTerminate();
            return 1;
        }
        spdlog::info("GLFW window created ({}x{})", kW, kH);

        // ----------------------------------------------------------------
        // Vulkan device + surface + swapchain
        // ----------------------------------------------------------------
        VulkanDevice device;
        RenderConfig render_cfg{};
        render_cfg.window_width      = kW;
        render_cfg.window_height     = kH;
        render_cfg.enable_validation = true;
        render_cfg.app_name          = "ProjectSchizoEditor";
        device.initialize(render_cfg);
        spdlog::info("VulkanDevice initialized: {}", device.get_device_name());

        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (glfwCreateWindowSurface(device.get_vk_instance(), glfw_window,
                                    nullptr, &surface) != VK_SUCCESS) {
            spdlog::error("glfwCreateWindowSurface failed");
            device.shutdown();
            glfwDestroyWindow(glfw_window);
            glfwTerminate();
            return 1;
        }
        if (!device.attach_surface(surface) ||
            !device.create_window_swapchain(kW, kH)) {
            spdlog::error("Surface / swapchain setup failed");
            device.shutdown();
            glfwDestroyWindow(glfw_window);
            glfwTerminate();
            return 1;
        }
        auto* swapchain = device.get_swapchain();
        spdlog::info("Swapchain: {}x{} ({} images)",
                     swapchain->get_width(), swapchain->get_height(),
                     swapchain->get_image_count());

        // ----------------------------------------------------------------
        // Material descriptor infra (layout + pool shared by GBuffer pipeline
        // and all Material instances created in the editor session).
        // ----------------------------------------------------------------
        VkDescriptorSetLayout mat_layout =
            Material::create_descriptor_set_layout(device.get_device());
        VkDescriptorPool mat_pool =
            Material::create_descriptor_pool(device.get_device(), 64);

        // ----------------------------------------------------------------
        // Deferred pipeline
        // ----------------------------------------------------------------
        GBufferConfig g_cfg{};
        g_cfg.width              = kW;
        g_cfg.height             = kH;
        g_cfg.material_set_layout = mat_layout;
        auto g_buffer = VulkanGBuffer::create(&device, g_cfg);

        LightingConfig l_cfg{};
        l_cfg.ambient_color   = glm::vec3(0.3f);
        l_cfg.global_ambient  = 1.0f;
        auto lighting = VulkanLightingPass::create(&device, l_cfg, g_buffer.get());
        lighting->add_directional_light(glm::vec3(0.3f, -1.0f, 0.2f),
                                        glm::vec3(1.0f, 0.95f, 0.85f),
                                        1.5f, /*shadow=*/false);

        ShadowMapConfig s_cfg{};
        s_cfg.width = s_cfg.height = 512;
        s_cfg.cascade_count = 1;
        auto shadow_map = VulkanShadowMap::create(&device, s_cfg);

        PostProcessingConfig pp_cfg{};
        pp_cfg.width  = kW;
        pp_cfg.height = kH;
        pp_cfg.bloom.enabled        = true;
        pp_cfg.taa.enabled          = false;
        pp_cfg.tone_mapping.enabled = true;
        auto post_processing = VulkanPostProcessing::create(&device, pp_cfg);
        post_processing->set_input_image(lighting->get_output_view());

        if (!g_buffer || !lighting || !shadow_map || !post_processing) {
            spdlog::error("Deferred pipeline component construction failed");
            device.shutdown();
            glfwDestroyWindow(glfw_window);
            glfwTerminate();
            return 1;
        }

        RenderGraphConfig graph_cfg{};
        graph_cfg.device          = &device;
        graph_cfg.g_buffer        = g_buffer.get();
        graph_cfg.lighting        = lighting.get();
        graph_cfg.shadow_map      = shadow_map.get();
        graph_cfg.post_processing = post_processing.get();
        graph_cfg.width           = kW;
        graph_cfg.height          = kH;
        auto graph = VulkanRenderGraph::create(graph_cfg);
        if (!graph) {
            spdlog::error("VulkanRenderGraph::create failed");
            device.shutdown();
            glfwDestroyWindow(glfw_window);
            glfwTerminate();
            return 1;
        }
        spdlog::info("Deferred pipeline + render graph ready");

        // ----------------------------------------------------------------
        // Primitive mesh cache (one GPU mesh per MeshType; reused by every
        // entity that selects that primitive via MeshRendererComponent).
        // ----------------------------------------------------------------
        schizo::editor::PrimitiveMeshCache prim_cache;
        prim_cache.initialize(&device);
        spdlog::info("Primitive meshes initialized (cube/plane/sphere/cylinder/capsule/pyramid)");

        // Per-entity Material cache. Materials are rebuilt only when the
        // entity's color factors actually change.
        schizo::editor::EntityMaterialCache mat_cache;

        // glTF asset cache. Lazily loads .gltf/.glb files referenced via
        // entity MeshComponent::mesh_path (set by drag-drop in the inspector).
        schizo::editor::AssetMeshCache asset_cache;

        // ----------------------------------------------------------------
        // ImGui render pass: clears swapchain to dark gray, presents as PRESENT_SRC.
        // The 3D scene is displayed inside the viewport via ImGui::Image(), not as
        // a swapchain blit — so we start from UNDEFINED and clear each frame.
        // ----------------------------------------------------------------
        VkAttachmentDescription imgui_color_attachment{};
        imgui_color_attachment.format         = swapchain->get_format();
        imgui_color_attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        imgui_color_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        imgui_color_attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        imgui_color_attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        imgui_color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        imgui_color_attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        imgui_color_attachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference imgui_color_ref{};
        imgui_color_ref.attachment = 0;
        imgui_color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription imgui_subpass{};
        imgui_subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        imgui_subpass.colorAttachmentCount = 1;
        imgui_subpass.pColorAttachments    = &imgui_color_ref;

        VkSubpassDependency imgui_dep{};
        imgui_dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
        imgui_dep.dstSubpass    = 0;
        imgui_dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        imgui_dep.srcAccessMask = 0;
        imgui_dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        imgui_dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo imgui_rp_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        imgui_rp_info.attachmentCount = 1;
        imgui_rp_info.pAttachments    = &imgui_color_attachment;
        imgui_rp_info.subpassCount    = 1;
        imgui_rp_info.pSubpasses      = &imgui_subpass;
        imgui_rp_info.dependencyCount = 1;
        imgui_rp_info.pDependencies   = &imgui_dep;

        VkRenderPass imgui_render_pass = VK_NULL_HANDLE;
        if (vkCreateRenderPass(device.get_device(), &imgui_rp_info, nullptr,
                               &imgui_render_pass) != VK_SUCCESS) {
            spdlog::error("Failed to create ImGui render pass");
            device.shutdown();
            glfwDestroyWindow(glfw_window);
            glfwTerminate();
            return 1;
        }

        // One framebuffer per swapchain image
        std::vector<VkFramebuffer> imgui_framebuffers(swapchain->get_image_count());
        for (uint32_t i = 0; i < swapchain->get_image_count(); ++i) {
            VkImageView views[] = { swapchain->get_image_view(i) };
            VkFramebufferCreateInfo fb_info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fb_info.renderPass      = imgui_render_pass;
            fb_info.attachmentCount = 1;
            fb_info.pAttachments    = views;
            fb_info.width           = swapchain->get_width();
            fb_info.height          = swapchain->get_height();
            fb_info.layers          = 1;
            vkCreateFramebuffer(device.get_device(), &fb_info, nullptr,
                                &imgui_framebuffers[i]);
        }
        spdlog::info("ImGui render pass + framebuffers created");

        // ----------------------------------------------------------------
        // ImGuiVulkan (owns ImGui context, GLFW + Vulkan backends)
        // ----------------------------------------------------------------
        auto imgui = ImGuiVulkan::create(&device, graph.get(), glfw_window,
                                         kW, kH, imgui_render_pass);
        if (!imgui) {
            spdlog::error("ImGuiVulkan::create failed");
            device.shutdown();
            glfwDestroyWindow(glfw_window);
            glfwTerminate();
            return 1;
        }
        ImGui::GetIO().IniFilename = "editor.ini";
        spdlog::info("ImGuiVulkan initialized");

        // ----------------------------------------------------------------
        // Per-frame synchronisation (double-buffered)
        // ----------------------------------------------------------------
        constexpr uint32_t kMaxFrames = 2;
        VkCommandBuffer frame_cmds[kMaxFrames]    = {};
        VkSemaphore     acquire_sems[kMaxFrames]  = {};
        VkSemaphore     render_sems[kMaxFrames]   = {};
        VkFence         frame_fences[kMaxFrames]  = {};

        VkCommandBufferAllocateInfo cmd_alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cmd_alloc.commandPool        = device.get_command_pool();
        cmd_alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_alloc.commandBufferCount = kMaxFrames;
        vkAllocateCommandBuffers(device.get_device(), &cmd_alloc, frame_cmds);

        VkSemaphoreCreateInfo sem_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo     fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (uint32_t i = 0; i < kMaxFrames; ++i) {
            vkCreateSemaphore(device.get_device(), &sem_info, nullptr, &acquire_sems[i]);
            vkCreateSemaphore(device.get_device(), &sem_info, nullptr, &render_sems[i]);
            vkCreateFence(device.get_device(), &fence_info, nullptr, &frame_fences[i]);
        }
        uint32_t current_frame = 0;

        // ----------------------------------------------------------------
        // Viewport texture — register post-processing output with ImGui
        // CRITICAL FIX #6: Validate post-processing before using its output
        // ----------------------------------------------------------------
        VkDescriptorSet viewport_ds = VK_NULL_HANDLE;
        if (post_processing) {
            viewport_ds = ImGui_ImplVulkan_AddTexture(
                post_processing->get_output_image(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        } else {
            spdlog::warn("Post-processing not initialized - viewport texture will be null");
        }

        // ----------------------------------------------------------------
        // Editor scene + UI state
        // ----------------------------------------------------------------
        schizo::editor::EditorScene editor_scene;
        EditorState editor_state;
        editor_state.editor_scene       = &editor_scene;
        editor_state.viewport_texture_id = viewport_ds;

        g_editor_state = &editor_state;
        glfwSetDropCallback(glfw_window, DropCallback);

        editor_state.asset_browser  = std::make_unique<schizo::editor::AssetBrowserPanel>();
        editor_state.material_editor = std::make_unique<schizo::editor::MaterialEditorPanel>();
        editor_state.asset_import_dialog = std::make_unique<schizo::editor::AssetImportDialog>();
        editor_state.scene_playback_manager = std::make_unique<schizo::editor::ScenePlaybackManager>();
        editor_state.character_panel = std::make_unique<schizo::editor::CharacterControllerPanel>();
        editor_state.ability_panel   = std::make_unique<schizo::editor::AbilitySystemPanel>();
        editor_state.network_panel   = std::make_unique<schizo::editor::NetworkSystemPanel>();
        spdlog::info("Editor state and panels initialized");

        // ----------------------------------------------------------------
        // Populate default base scene — light + ground only. The user adds
        // additional entities via the Scene Hierarchy "+ Add Entity" menu.
        // ----------------------------------------------------------------
        {
            auto scene = editor_scene.GetScene();
            if (scene) {
                schizo::scene::EntityFactory::CreateDirectionalLight(scene, "Directional Light",
                    glm::vec3(-0.3f, -1.0f, 0.2f),
                    glm::vec3(1.0f, 0.95f, 0.85f), 1.5f);

                schizo::scene::EntityFactory::CreatePlane(scene, "Ground", 10.0f, 10.0f,
                    glm::vec4(0.45f, 0.45f, 0.45f, 1.0f));

                spdlog::info("Default scene: {} entities", scene->GetEntityCount());
            }
        }

        // ----------------------------------------------------------------
        // Main loop
        // ----------------------------------------------------------------
        spdlog::info("Entering editor loop...");
        int frame_count = 0;

        // Persistent state for the play-mode cursor pipeline. We mirror the
        // playback manager's IsCursorCaptured() flag onto GLFW (hide/lock the
        // cursor) and onto ImGui (NoMouse = ignore mouse on panels) so the
        // player can't accidentally hover or click editor UI while playing.
        bool prev_cursor_captured = false;
        double last_cursor_x = 0.0;
        double last_cursor_y = 0.0;
        bool cursor_delta_primed = false;

        while (!glfwWindowShouldClose(glfw_window)) {
            glfwPollEvents();

            // Key input
            auto key = [&](int k) { return glfwGetKey(glfw_window, k) == GLFW_PRESS; };

            // ----------------------------------------------------------------
            // Play-mode cursor pipeline. Gated tightly on IsPlaying() so it is
            // a no-op outside play. No ImGui ConfigFlags manipulation here —
            // we only toggle GLFW's cursor mode and feed raw deltas. Editor
            // panels remain hoverable, but the OS cursor is hidden and locked
            // so the player can't move it onto a panel by accident.
            // ----------------------------------------------------------------
            const bool playing = editor_state.scene_playback_manager &&
                                 editor_state.scene_playback_manager->IsPlaying();
            if (playing) {
                const bool want_capture =
                    editor_state.scene_playback_manager->IsCursorCaptured();
                if (want_capture != prev_cursor_captured) {
                    glfwSetInputMode(glfw_window, GLFW_CURSOR,
                                     want_capture ? GLFW_CURSOR_DISABLED
                                                  : GLFW_CURSOR_NORMAL);
                    cursor_delta_primed = false;
                    prev_cursor_captured = want_capture;
                }
                if (want_capture) {
                    double cx = 0.0, cy = 0.0;
                    glfwGetCursorPos(glfw_window, &cx, &cy);
                    if (cursor_delta_primed) {
                        editor_state.scene_playback_manager->OnMouseDelta(
                            static_cast<float>(cx - last_cursor_x),
                            static_cast<float>(cy - last_cursor_y));
                    }
                    last_cursor_x = cx;
                    last_cursor_y = cy;
                    cursor_delta_primed = true;
                }
            } else if (prev_cursor_captured) {
                // Exited play mode — restore the OS cursor we hid earlier.
                glfwSetInputMode(glfw_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                prev_cursor_captured = false;
                cursor_delta_primed = false;
            }

            // ESC progressively de-escalates while playing:
            //   captured cursor → release cursor
            //   released cursor → stop playback
            // Outside play, ESC still exits the editor (matches old behaviour).
            {
                static bool prev_esc = false;
                bool cur_esc = key(GLFW_KEY_ESCAPE);
                if (cur_esc && !prev_esc) {
                    if (playing) {
                        if (editor_state.scene_playback_manager->IsCursorCaptured()) {
                            editor_state.scene_playback_manager->SetCursorCaptured(false);
                        } else {
                            editor_state.scene_playback_manager->StopPlayback();
                        }
                    } else {
                        spdlog::info("ESC — exiting editor");
                        prev_esc = cur_esc;
                        break;
                    }
                }
                prev_esc = cur_esc;
            }
            if (key(GLFW_KEY_LEFT_CONTROL) && key(GLFW_KEY_Z)) {
                if (editor_state.undo_redo_manager.CanUndo())
                    editor_state.undo_redo_manager.Undo();
            }
            if (key(GLFW_KEY_LEFT_CONTROL) && key(GLFW_KEY_Y)) {
                if (editor_state.undo_redo_manager.CanRedo())
                    editor_state.undo_redo_manager.Redo();
            }
            // F5 toggles play/stop. Edge-detected so a held key fires once.
            {
                static bool prev_f5 = false;
                bool cur_f5 = key(GLFW_KEY_F5);
                if (cur_f5 && !prev_f5 && editor_state.scene_playback_manager) {
                    if (editor_state.scene_playback_manager->IsPlaying()) {
                        editor_state.scene_playback_manager->StopPlayback();
                    } else {
                        auto sc = editor_state.editor_scene->GetScene();
                        if (sc) editor_state.scene_playback_manager->StartPlayback(sc);
                    }
                }
                prev_f5 = cur_f5;
            }

            // V toggles first-/third-person view while playing. Edge-detected
            // so a held key only fires once.
            {
                static bool prev_v = false;
                bool cur_v = key(GLFW_KEY_V);
                if (cur_v && !prev_v &&
                    editor_state.scene_playback_manager &&
                    editor_state.scene_playback_manager->IsPlaying()) {
                    editor_state.scene_playback_manager->ToggleCameraView();
                }
                prev_v = cur_v;
            }

            // Delete key removes the selected entity. Edge-detected so a held
            // key fires once; suppressed while ImGui has a text field focused
            // so it doesn't conflict with the rename dialog.
            {
                static bool prev_delete = false;
                bool cur_delete = key(GLFW_KEY_DELETE);
                if (cur_delete && !prev_delete &&
                    !ImGui::GetIO().WantTextInput &&
                    editor_state.selected_entity_id != 0) {
                    auto sc = editor_state.editor_scene->GetScene();
                    auto ent = sc ? sc->GetEntityById(editor_state.selected_entity_id)
                                  : nullptr;
                    if (ent) {
                        spdlog::info("Deleted entity (Delete key): {}", ent->GetName());
                        sc->RemoveEntity(ent);
                        editor_state.selected_entity_id = 0;
                        editor_state.editor_scene->MarkModified();
                    }
                }
                prev_delete = cur_delete;
            }

            float cam_spd = 0.1f;
            if (key(GLFW_KEY_W))     editor_state.viewport_camera.MoveLocal( cam_spd, 0.f, 0.f);
            if (key(GLFW_KEY_S))     editor_state.viewport_camera.MoveLocal(-cam_spd, 0.f, 0.f);
            if (key(GLFW_KEY_A))     editor_state.viewport_camera.MoveLocal(0.f, -cam_spd, 0.f);
            if (key(GLFW_KEY_D))     editor_state.viewport_camera.MoveLocal(0.f,  cam_spd, 0.f);
            if (key(GLFW_KEY_SPACE)) editor_state.viewport_camera.MoveLocal(0.f, 0.f,  cam_spd);

            if (editor_state.is_playing)
                editor_state.play_time += 0.016f;
            if (editor_state.scene_playback_manager &&
                editor_state.scene_playback_manager->IsPlaying())
                editor_state.scene_playback_manager->Update(0.016f);

            // ------------------------------------------------------------
            // Build ImGui frame (no GPU commands yet)
            // ------------------------------------------------------------
            imgui->begin_frame();

            {   // Dark background window
                ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                ImGui::Begin("##background", nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoBringToFrontOnFocus);
                ImGui::End();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar(2);
            }

            // Consume OS-dropped files (filled by GLFW DropCallback). Copies
            // recognised asset extensions into assets/models and refreshes
            // the browser so they appear immediately.
            if (!editor_state.dropped_files.empty()) {
                namespace fs = std::filesystem;
                const fs::path target_dir = "assets/models";
                std::error_code ec;
                fs::create_directories(target_dir, ec);
                int imported = 0;
                for (const auto& src : editor_state.dropped_files) {
                    fs::path src_path(src);
                    std::string ext = src_path.extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                                   [](unsigned char c){ return std::tolower(c); });
                    if (ext != ".obj" && ext != ".gltf" && ext != ".glb" &&
                        ext != ".fbx" && ext != ".png" && ext != ".jpg" &&
                        ext != ".jpeg" && ext != ".tga") {
                        spdlog::warn("[Drop] Skipping unsupported file: {}", src);
                        continue;
                    }
                    fs::path dst = target_dir / src_path.filename();
                    std::error_code copy_ec;
                    fs::copy_file(src_path, dst,
                                  fs::copy_options::overwrite_existing, copy_ec);
                    if (copy_ec) {
                        spdlog::error("[Drop] Failed to import {} -> {}: {}",
                                      src, dst.string(), copy_ec.message());
                    } else {
                        spdlog::info("[Drop] Imported {} -> {}", src, dst.string());
                        ++imported;
                    }
                }
                editor_state.dropped_files.clear();
                if (imported > 0 && editor_state.asset_browser)
                    editor_state.asset_browser->RefreshAssets();
            }

            ShowMainMenuBar(editor_state);
            ShowSaveDialog(editor_state);
            ShowOpenDialog(editor_state);
            ShowRenameDialog(editor_state);
            if (editor_state.show_demo_window)
                ImGui::ShowDemoWindow(&editor_state.show_demo_window);
            ShowViewport(editor_state);
            ShowSceneHierarchy(editor_state);
            ShowInspector(editor_state);
            ShowAssetBrowser(editor_state);
            ShowPlaybackControls(editor_state);
            ShowDebugPanels(editor_state);
            if (editor_state.asset_import_dialog &&
                editor_state.asset_import_dialog->IsOpen())
                editor_state.asset_import_dialog->RenderDialog();
            ShowPreferences(editor_state);

            // ------------------------------------------------------------
            // GPU frame
            // ------------------------------------------------------------
            vkWaitForFences(device.get_device(), 1, &frame_fences[current_frame],
                            VK_TRUE, UINT64_MAX);
            uint32_t image_index = swapchain->acquire_next_image(
                acquire_sems[current_frame]);
            vkResetFences(device.get_device(), 1, &frame_fences[current_frame]);

            VkCommandBuffer cmd = frame_cmds[current_frame];
            vkResetCommandBuffer(cmd, 0);

            VkCommandBufferBeginInfo begin_info{
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &begin_info);

            // Camera from viewport — use the actual panel aspect ratio
            CameraData cam{};
            float vp_w = editor_state.viewport_panel_size.x;
            float vp_h = editor_state.viewport_panel_size.y;
            float aspect = (vp_h > 0.0f) ? vp_w / vp_h
                                          : static_cast<float>(kW) / static_cast<float>(kH);
            
            // Use playback camera if scene is playing, otherwise use editor viewport camera
            if (editor_state.scene_playback_manager && editor_state.scene_playback_manager->IsPlaying()) {
                auto playback_camera = editor_state.scene_playback_manager->GetPlaybackCamera();
                if (playback_camera) {
                    auto camera_transform = playback_camera->GetTransform();
                    if (camera_transform) {
                        // Use the camera's WORLD position and rotation
                        cam.position = camera_transform->GetWorldPosition();
                        glm::vec3 cam_forward = camera_transform->GetWorldRotation() * glm::vec3(0.0f, 0.0f, -1.0f);
                        glm::vec3 cam_up = camera_transform->GetWorldRotation() * glm::vec3(0.0f, 1.0f, 0.0f);
                        cam.view = glm::lookAt(cam.position, cam.position + cam_forward, cam_up);
                        cam.proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 1000.0f);
                        static bool logged_once = false;
                        if (!logged_once) {
                            spdlog::info("✓ CAMERA SWAP ACTIVE: Using playback camera '{}'", playback_camera->GetName());
                            spdlog::info("  Position: ({:.2f}, {:.2f}, {:.2f})", cam.position.x, cam.position.y, cam.position.z);
                            logged_once = true;
                        }
                    } else {
                        // Fallback to editor camera if camera has no transform
                        spdlog::warn("⚠️ Play mode active but playback camera has no transform, using editor camera");
                        cam.position = editor_state.viewport_camera.GetPosition();
                        cam.view = editor_state.viewport_camera.GetViewMatrix();
                        cam.proj = editor_state.viewport_camera.GetProjectionMatrix(aspect);
                    }
                } else {
                    // Fallback to editor camera if playback camera is null
                    spdlog::warn("⚠️ Play mode active but playback camera is null, using editor camera");
                    cam.position = editor_state.viewport_camera.GetPosition();
                    cam.view = editor_state.viewport_camera.GetViewMatrix();
                    cam.proj = editor_state.viewport_camera.GetProjectionMatrix(aspect);
                }
            } else {
                // Normal edit mode - use editor viewport camera
                cam.position = editor_state.viewport_camera.GetPosition();
                cam.view = editor_state.viewport_camera.GetViewMatrix();
                cam.proj = editor_state.viewport_camera.GetProjectionMatrix(aspect);
            }
            
            cam.proj[1][1] *= -1.0f;  // GL → Vulkan Y flip
            graph->set_camera(cam);

            // Build per-frame draw list from the editor scene's entities.
            // Entities with a MeshRendererComponent become DrawItems; others
            // (lights, empty parents) are skipped.
            mat_cache.prune(editor_scene.GetScene());
            
            // CRITICAL FIX #5: Validate render graph before setting draw items
            if (!graph) {
                spdlog::error("Render graph is null - skipping frame");
                device.wait_idle();
                continue;
            }
            
            auto scene_draw_items = schizo::editor::build_draw_items(
                editor_scene.GetScene(), prim_cache, mat_cache, asset_cache,
                &device, mat_layout, mat_pool);
            graph->set_draw_items(scene_draw_items);

            graph->begin_frame(cmd);
            graph->execute_stage(cmd, RenderGraphStage::Shadow,     {});
            graph->execute_stage(cmd, RenderGraphStage::Geometry,   {});
            graph->execute_stage(cmd, RenderGraphStage::Lighting,   {});
            graph->execute_stage(cmd, RenderGraphStage::PostProcess, {});
            graph->end_frame(cmd);

            // ImGui render pass — clears swapchain to dark gray, then draws UI on top.
            // The 3D scene is shown inside the Viewport panel via ImGui::Image().
            VkClearValue imgui_clear{};
            imgui_clear.color = {0.1f, 0.1f, 0.1f, 1.0f};
            VkRenderPassBeginInfo rp_begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            rp_begin.renderPass        = imgui_render_pass;
            rp_begin.framebuffer       = imgui_framebuffers[image_index];
            rp_begin.renderArea.offset = {0, 0};
            rp_begin.renderArea.extent = {swapchain->get_width(),
                                          swapchain->get_height()};
            rp_begin.clearValueCount   = 1;
            rp_begin.pClearValues      = &imgui_clear;
            vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
            imgui->end_frame(cmd);  // ImGui::Render + RenderDrawData
            vkCmdEndRenderPass(cmd);

            vkEndCommandBuffer(cmd);

            VkPipelineStageFlags wait_stage =
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit_info.waitSemaphoreCount   = 1;
            submit_info.pWaitSemaphores      = &acquire_sems[current_frame];
            submit_info.pWaitDstStageMask    = &wait_stage;
            submit_info.commandBufferCount   = 1;
            submit_info.pCommandBuffers      = &cmd;
            submit_info.signalSemaphoreCount = 1;
            submit_info.pSignalSemaphores    = &render_sems[current_frame];
            vkQueueSubmit(device.get_graphics_queue(), 1, &submit_info,
                          frame_fences[current_frame]);

            swapchain->present_image(image_index, render_sems[current_frame]);
            current_frame = (current_frame + 1) % kMaxFrames;
            ++frame_count;
        }

        spdlog::info("Editor closed after {} frames", frame_count);

        // ----------------------------------------------------------------
        // Cleanup
        // ----------------------------------------------------------------
        device.wait_idle();

        if (viewport_ds != VK_NULL_HANDLE)
            ImGui_ImplVulkan_RemoveTexture(viewport_ds);

        imgui.reset();  // ImGui::DestroyContext + backends shutdown

        for (auto fb : imgui_framebuffers)
            vkDestroyFramebuffer(device.get_device(), fb, nullptr);
        vkDestroyRenderPass(device.get_device(), imgui_render_pass, nullptr);

        for (uint32_t i = 0; i < kMaxFrames; ++i) {
            vkDestroyFence(device.get_device(), frame_fences[i], nullptr);
            vkDestroySemaphore(device.get_device(), render_sems[i], nullptr);
            vkDestroySemaphore(device.get_device(), acquire_sems[i], nullptr);
        }
        vkFreeCommandBuffers(device.get_device(), device.get_command_pool(),
                             kMaxFrames, frame_cmds);

        // Scene geometry — caches must release their meshes + materials
        // before the descriptor pool / layout are destroyed.
        mat_cache.clear();
        asset_cache.clear();
        prim_cache.cube.reset();
        prim_cache.plane.reset();
        prim_cache.sphere.reset();
        prim_cache.cylinder.reset();
        prim_cache.capsule.reset();
        prim_cache.pyramid.reset();
        vkDestroyDescriptorPool(device.get_device(), mat_pool, nullptr);
        vkDestroyDescriptorSetLayout(device.get_device(), mat_layout, nullptr);

        graph.reset();
        post_processing.reset();
        shadow_map.reset();
        lighting.reset();
        g_buffer.reset();

        device.shutdown();
        glfwDestroyWindow(glfw_window);
        glfwTerminate();

        return 0;
    }
    catch (const std::exception& e) {
        spdlog::error("Exception: {}", e.what());
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
}
