// Project Schizo Editor - Main entry point
// Phase 5: In-engine editor with ImGui

#define GLM_ENABLE_EXPERIMENTAL

// Glad header for OpenGL loading - must be included before GLFW
#include <glad/glad.h>

// Prevent GLFW from including the native GL header (we're using glad instead)
#define GLFW_INCLUDE_NONE

// GLFW header
#include <GLFW/glfw3.h>

#include "window.h"
#include "editor_scene.h"
#include "scene.h"
#include "entity_factory.h"
#include "transform_component.h"
#include "light_component.h"
#include "viewport_camera.h"
#include "mesh_renderer_component.h"
#include "simple_renderer.h"
#include "viewport_renderer_3d.h"
#include "asset_browser_panel.h"
#include "material_editor_panel.h"
#include "asset_import_dialog.h"
#include "transform_gizmo.h"
#include "undo_redo_manager.h"
#include "asset_manager.h"
#include "scene_playback_manager.h"
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

// ImGui headers
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

// GLM headers
#include <glm/gtx/euler_angles.hpp>

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
    float clear_color[4] = {0.1f, 0.1f, 0.1f, 1.0f};
    
    // Viewport data
    schizo::editor::ViewportCamera viewport_camera;
    bool viewport_camera_rotating = false;
    glm::vec2 last_mouse_pos = glm::vec2(0.0f);
    
    // Viewport display options
    bool show_grid = true;
    bool show_axes = true;
    bool wireframe_mode = false;
    bool show_entity_names = true;
    int viewport_width = 800;
    int viewport_height = 600;
    float grid_spacing = 1.0f;
    int grid_size = 20;
    
    // File dialog state
    bool show_save_dialog = false;
    bool show_open_dialog = false;
    char save_filename[256] = "untitled.scene";
    char open_filename[256] = "";
    
    // Rename dialog state
    bool show_rename_dialog = false;
    uint32_t rename_entity_id = 0;
    char rename_buffer[256] = "";
    
    // 3D Renderer
    std::unique_ptr<schizo::editor::SimpleRenderer> simple_renderer;
    std::unique_ptr<schizo::editor::ViewportRenderer3D> viewport_renderer_3d;
    
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
                    
                    // Also accept mesh assets
                    if (const ImGuiPayload* mesh_payload = ImGui::AcceptDragDropPayload("MESH_ASSET")) {
                        const char* mesh_path = (const char*)mesh_payload->Data;
                        entity->SetMesh(mesh_path);
                        spdlog::info("Assigned mesh '{}' to entity '{}'", mesh_path, entity->GetName());
                        editor_state.editor_scene->MarkModified();
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
            if (!children.empty() && ImGui::BeginChild("ChildrenList", ImVec2(0, 100), true)) {
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
            scene->RemoveEntity(selected_entity);
            editor_state.selected_entity_id = 0;
            spdlog::info("Deleted entity: {}", selected_entity->GetName());
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
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove;
    if (editor_state.gizmo_dragging) {
        flags |= ImGuiWindowFlags_NoInputs;
    }
    
    if (ImGui::Begin("Asset Browser", &editor_state.show_asset_browser, flags)) {
        auto scene = editor_state.editor_scene->GetScene();
        editor_state.asset_browser->Render(scene);
    }
    ImGui::End();
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
        
        auto view_matrix = editor_state.viewport_camera.GetViewMatrix();
        auto proj_matrix = editor_state.viewport_camera.GetProjectionMatrix(aspect);
        
        // Render to framebuffer
        if (viewport_size.x > 50.0f && viewport_size.y > 50.0f && editor_state.simple_renderer) {
            uint32_t fb_width = static_cast<uint32_t>(viewport_size.x);
            uint32_t fb_height = static_cast<uint32_t>(viewport_size.y);
            
            try {
                editor_state.simple_renderer->ResizeFramebuffer(fb_width, fb_height);
                editor_state.simple_renderer->BindFramebuffer();
                // Gray background
                editor_state.simple_renderer->ClearFramebuffer(glm::vec4(0.2f, 0.2f, 0.2f, 1.0f));
                
                // Render grid (identity transform)
                glm::mat4 identity = glm::mat4(1.0f);
                editor_state.simple_renderer->RenderMesh(
                    editor_state.simple_renderer->grid_mesh,
                    identity, view_matrix, proj_matrix
                );
                
                // Render axes (scale for visibility)
                glm::mat4 axes_transform = glm::scale(glm::mat4(1.0f), glm::vec3(5.0f));
                editor_state.simple_renderer->RenderMesh(
                    editor_state.simple_renderer->axes_mesh,
                    axes_transform, view_matrix, proj_matrix
                );
                
                // Render entities as cubes
                if (scene) {
                    const auto& entities = scene->GetEntities();
                    spdlog::info("=== VIEWPORT RENDER: {} entities ===", entities.size());
                    for (const auto& entity : entities) {
                        auto transform = entity->GetTransform();
                        auto pos = transform->GetWorldPosition();
                        auto local_scale = transform->GetLocalScale();
                        
                        // Create model matrix
                        glm::mat4 model = glm::translate(glm::mat4(1.0f), pos);
                        model = glm::scale(model, local_scale);
                        
                        // Skip rendering Camera entities as boxes - they'll be rendered as gizmos
                        if (entity->GetName() == "Camera") {
                            continue;
                        }
                        
                        // Check for mesh component with custom mesh
                        bool rendered = false;
                        auto mesh_component = entity->GetMeshComponent();
                        if (mesh_component && mesh_component->use_asset_mesh) {
                            auto mesh_asset = mesh_component->GetMeshAsset();
                            if (mesh_asset) {
                                spdlog::info("[VIEWPORT] Rendering custom mesh for entity '{}'", entity->GetName());
                                // Convert mesh asset to GPU mesh and render
                                auto asset_mesh = editor_state.simple_renderer->GetOrCreateMeshFromAsset(*mesh_asset);
                                editor_state.simple_renderer->RenderMesh(
                                    asset_mesh,
                                    model, view_matrix, proj_matrix
                                );
                                rendered = true;
                            }
                        }
                        
                        // Fallback to cube if no custom mesh or render as normal cube
                        if (!rendered) {
                            editor_state.simple_renderer->RenderMesh(
                                editor_state.simple_renderer->cube_mesh,
                                model, view_matrix, proj_matrix
                            );
                        }
                    }
                    
                    // Render camera gizmos and other entities
                    for (const auto& entity : entities) {
                        // Render camera entities as pyramid gizmos
                        if (entity->GetName() == "Camera") {
                            auto transform = entity->GetTransform();
                            auto entity_cam_pos = transform->GetWorldPosition();
                            
                            // Camera gizmo: small pyramid pointing forward
                            glm::mat4 cam_gizmo = glm::translate(glm::mat4(1.0f), entity_cam_pos);
                            cam_gizmo = glm::scale(cam_gizmo, glm::vec3(0.2f));
                            
                            // Create pyramid outline for camera visualization
                            // For now, use small axes to show camera orientation
                            glm::mat4 cam_axes = glm::translate(glm::mat4(1.0f), entity_cam_pos);
                            cam_axes = glm::scale(cam_axes, glm::vec3(0.3f));
                            editor_state.simple_renderer->RenderMesh(
                                editor_state.simple_renderer->axes_mesh,
                                cam_axes, view_matrix, proj_matrix
                            );
                        }
                    }
                    
                    // Render transform gizmo for selected entity
                    if (editor_state.show_gizmo && editor_state.selected_entity_id != 0) {
                        auto selected_entity = scene->GetEntityById(editor_state.selected_entity_id);
                        if (selected_entity) {
                            auto selected_transform = selected_entity->GetTransform();
                            auto selected_pos = selected_transform->GetWorldPosition();
                            auto selected_scale = selected_transform->GetLocalScale();
                            
                            // Render the gizmo
                            editor_state.transform_gizmo.Render(
                                editor_state.simple_renderer.get(),
                                selected_pos, 
                                glm::vec3(0.0f),  // rotation (not used yet)
                                selected_scale,
                                view_matrix, proj_matrix,
                                2.0f  // gizmo size
                            );
                        }
                    }
                    
                    // Render selection highlight box around selected entity
                    if (editor_state.selected_entity_id != 0) {
                        auto selected_entity = scene->GetEntityById(editor_state.selected_entity_id);
                        if (selected_entity) {
                            auto selected_transform = selected_entity->GetTransform();
                            auto selected_pos = selected_transform->GetWorldPosition();
                            auto selected_scale = selected_transform->GetLocalScale();
                            
                            // Create selection box (wireframe cube outline at entity position)
                            glm::mat4 selection_box = glm::translate(glm::mat4(1.0f), selected_pos);
                            selection_box = glm::scale(selection_box, selected_scale * 0.5f);
                            
                            // Render as wireframe to show selection
                            editor_state.simple_renderer->RenderMesh(
                                editor_state.simple_renderer->wireframe_box_mesh,
                                selection_box, view_matrix, proj_matrix
                            );
                        }
                    }
                }
                
                editor_state.simple_renderer->UnbindFramebuffer();
                
                // TEMPORARY TEST: Render frame to window instead of displaying framebuffer texture
                spdlog::info("[VIEWPORT] Unbind complete - would display framebuffer texture ID: {}", 
                    editor_state.simple_renderer->GetFramebufferTexture());
                
                // Display framebuffer texture in ImGui
                GLuint fb_texture = editor_state.simple_renderer->GetFramebufferTexture();
                if (fb_texture != 0) {
                    // Display with different UV coords
                    ImGui::Image(
                        reinterpret_cast<void*>(static_cast<uintptr_t>(fb_texture)),
                        viewport_size,
                        ImVec2(0, 1),
                        ImVec2(1, 0)
                    );
                } else {
                    ImGui::Text("Framebuffer texture not initialized (ID=0)");
                }
            } catch (const std::exception& e) {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "Viewport error: %s", e.what());
                spdlog::error("Viewport rendering error: {}", e.what());
            }
        }
        
        // Camera controls - only process when viewport is hovered
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
            ImGuiIO& io = ImGui::GetIO();
            
            // Handle entity selection through picking and gizmo interaction
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && scene) {
                ImVec2 mouse_pos = io.MousePos;
                ImVec2 viewport_canvas_pos = ImGui::GetCursorScreenPos();
                
                // Calculate relative position within viewport (from actual image position)
                float viewport_x = mouse_pos.x - viewport_canvas_pos.x;
                float viewport_y = mouse_pos.y - viewport_canvas_pos.y;
                
                // Clamp to viewport bounds
                viewport_x = std::max(0.0f, std::min(viewport_x, viewport_size.x));
                viewport_y = std::max(0.0f, std::min(viewport_y, viewport_size.y));
                
                spdlog::debug("[CLICK] Mouse: ({}, {}), Canvas: ({}, {}), Viewport: ({}, {})", 
                    mouse_pos.x, mouse_pos.y, viewport_canvas_pos.x, viewport_canvas_pos.y, viewport_x, viewport_y);
                
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
                    if (editor_state.transform_gizmo.GetMode() == schizo::editor::GizmoMode::Translate) {
                        glm::vec3 current_pos = selected_transform->GetLocalPosition();
                        glm::vec3 new_pos = editor_state.transform_gizmo.UpdateDrag(current_mouse_glm, current_pos);
                        selected_transform->SetLocalPosition(new_pos);
                    } else if (editor_state.transform_gizmo.GetMode() == schizo::editor::GizmoMode::Rotate) {
                        // Rotation support will be added once we refactor to handle quaternions properly
                        // For now, skip rotation updates
                    } else if (editor_state.transform_gizmo.GetMode() == schizo::editor::GizmoMode::Scale) {
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
    
    ImGuiWindow* parent = ImGui::GetCurrentWindow();
    
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
        
        // Character Controller Panel
        if (editor_state.character_panel) {
            if (ImGui::CollapsingHeader("Character Controller##debug", ImGuiTreeNodeFlags_DefaultOpen)) {
                editor_state.character_panel->Render(editor_state.selected_character_controller);
            }
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        
        // Ability System Panel
        if (editor_state.ability_panel) {
            if (ImGui::CollapsingHeader("Ability System##debug", ImGuiTreeNodeFlags_DefaultOpen)) {
                editor_state.ability_panel->Render(editor_state.selected_ability_system);
            }
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        
        // Network System Panel
        if (editor_state.network_panel) {
            if (ImGui::CollapsingHeader("Network System##debug", ImGuiTreeNodeFlags_DefaultOpen)) {
                editor_state.network_panel->Render(editor_state.network_manager);
            }
        }
        
        ImGui::End();
    }
}

int main() {
    try {
        spdlog::set_level(spdlog::level::info);
        spdlog::info("=== Project Schizo Editor ===");
        
        // Create window
        spdlog::info("Creating window...");
        schizo::window::WindowProperties props;
        props.width = 1920;
        props.height = 1080;
        props.title = "Project Schizo - Editor";
        props.vsync = true;
        
        auto window = schizo::window::Window::Create(props);
        if (!window) {
            spdlog::error("Failed to create window!");
            return 1;
        }
        spdlog::info("Window created successfully!");
        
        // Load OpenGL function pointers with glad
        if (!gladLoadGL((GLADloadproc)glfwGetProcAddress)) {
            spdlog::error("Failed to initialize glad!");
            return 1;
        }
        const char* gl_version = (const char*)glGetString(GL_VERSION);
        spdlog::info("Glad initialized - OpenGL {} loaded", gl_version);
        
        // Initialize ImGui
        spdlog::info("Initializing ImGui...");
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        
        // Enable keyboard navigation
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        
        // Set INI file for window state persistence
        io.IniFilename = "editor.ini";
        
        ImGui::StyleColorsDark();
        
        // Setup Platform/Renderer backends
        ImGui_ImplGlfw_InitForOpenGL(window->GetNativeHandle(), true);
        ImGui_ImplOpenGL3_Init("#version 450");
        
        spdlog::info("ImGui initialized!");
        
        // Create editor scene (manages current scene being edited)
        schizo::editor::EditorScene editor_scene;
        
        // Editor state
        EditorState editor_state;
        editor_state.editor_scene = &editor_scene;
        
        // Set up drag-and-drop callback
        g_editor_state = &editor_state;
        glfwSetDropCallback(window->GetNativeHandle(), DropCallback);
        spdlog::info("Drag-and-drop support enabled");
        
        // Initialize 3D renderer
        spdlog::info("Initializing SimpleRenderer...");
        editor_state.simple_renderer = std::make_unique<schizo::editor::SimpleRenderer>();
        editor_state.simple_renderer->Initialize();
        
        // Initialize 3D Viewport Renderer
        spdlog::info("Initializing 3D Viewport Renderer...");
        editor_state.viewport_renderer_3d = std::make_unique<schizo::editor::ViewportRenderer3D>();
        editor_state.viewport_renderer_3d->Initialize(nullptr, 1024, 768);
        spdlog::info("ViewportRenderer3D initialized");
        
        // Initialize Asset Browser Panel
        spdlog::info("Initializing Asset Browser Panel...");
        editor_state.asset_browser = std::make_unique<schizo::editor::AssetBrowserPanel>();
        spdlog::info("Asset Browser Panel initialized");
        
        // Initialize Material Editor Panel
        spdlog::info("Initializing Material Editor Panel...");
        editor_state.material_editor = std::make_unique<schizo::editor::MaterialEditorPanel>();
        spdlog::info("Material Editor Panel initialized");
        
        // Initialize Asset Import Dialog
        spdlog::info("Initializing Asset Import Dialog...");
        editor_state.asset_import_dialog = std::make_unique<schizo::editor::AssetImportDialog>();
        spdlog::info("Asset Import Dialog initialized");
        
        // Initialize Scene Playback Manager
        spdlog::info("Initializing Scene Playback Manager...");
        editor_state.scene_playback_manager = std::make_unique<schizo::editor::ScenePlaybackManager>();
        spdlog::info("Scene Playback Manager initialized");
        
        // Initialize Debug Panels
        spdlog::info("Initializing Debug Panels...");
        editor_state.character_panel = std::make_unique<schizo::editor::CharacterControllerPanel>();
        editor_state.ability_panel = std::make_unique<schizo::editor::AbilitySystemPanel>();
        editor_state.network_panel = std::make_unique<schizo::editor::NetworkSystemPanel>();
        spdlog::info("Debug Panels initialized (Character, Ability, Network)");
        
        // Main loop
        spdlog::info("Entering editor loop...");
        int frame_count = 0;
        
        while (window->Update()) {
            if (window->IsKeyPressed(schizo::window::KeyCode::ESCAPE)) {
                spdlog::info("ESC pressed - exiting editor");
                break;
            }
            
            // Ctrl+Z for undo
            if (window->IsKeyPressed(schizo::window::KeyCode::LEFT_CONTROL) && 
                window->IsKeyPressed(schizo::window::KeyCode::Z)) {
                if (editor_state.undo_redo_manager.CanUndo()) {
                    editor_state.undo_redo_manager.Undo();
                    spdlog::info("Undo: {}", editor_state.undo_redo_manager.GetRedoDescription());
                }
            }
            
            // Ctrl+Y for redo
            if (window->IsKeyPressed(schizo::window::KeyCode::LEFT_CONTROL) && 
                window->IsKeyPressed(schizo::window::KeyCode::Y)) {
                if (editor_state.undo_redo_manager.CanRedo()) {
                    editor_state.undo_redo_manager.Redo();
                    spdlog::info("Redo: {}", editor_state.undo_redo_manager.GetUndoDescription());
                }
            }
            
            // F5 to toggle play mode
            if (window->IsKeyPressed(schizo::window::KeyCode::F5)) {
                if (editor_state.scene_playback_manager->IsPlaying()) {
                    editor_state.scene_playback_manager->StopPlayback();
                    spdlog::info("Play mode stopped (F5)");
                } else {
                    auto scene = editor_state.editor_scene->GetScene();
                    if (scene && editor_state.scene_playback_manager->StartPlayback(scene)) {
                        spdlog::info("Play mode started (F5)");
                    } else {
                        spdlog::warn("Failed to start playback - no valid scene");
                    }
                }
            }
            
            // Flythrough camera controls (WASD keys)
            float camera_movement = 0.1f;
            if (window->IsKeyPressed(schizo::window::KeyCode::W)) {
                editor_state.viewport_camera.MoveLocal(camera_movement, 0.0f, 0.0f);
            }
            if (window->IsKeyPressed(schizo::window::KeyCode::S)) {
                editor_state.viewport_camera.MoveLocal(-camera_movement, 0.0f, 0.0f);
            }
            if (window->IsKeyPressed(schizo::window::KeyCode::A)) {
                editor_state.viewport_camera.MoveLocal(0.0f, -camera_movement, 0.0f);
            }
            if (window->IsKeyPressed(schizo::window::KeyCode::D)) {
                editor_state.viewport_camera.MoveLocal(0.0f, camera_movement, 0.0f);
            }
            if (window->IsKeyPressed(schizo::window::KeyCode::SPACE)) {
                editor_state.viewport_camera.MoveLocal(0.0f, 0.0f, camera_movement);
            }
            if (window->IsKeyPressed(schizo::window::KeyCode::LEFT_CONTROL)) {
                editor_state.viewport_camera.MoveLocal(0.0f, 0.0f, -camera_movement);
            }
            
            // Update play time if in play mode
            if (editor_state.is_playing) {
                editor_state.play_time += 0.016f;  // Approximate 60 FPS delta time
            }
            
            // Update scene playback if running
            if (editor_state.scene_playback_manager && editor_state.scene_playback_manager->IsPlaying()) {
                editor_state.scene_playback_manager->Update(0.016f);  // Approximate 60 FPS delta time
            }
            
            // Start ImGui frame
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            
            // Draw background to clear previous frames
            {
                ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                ImGui::Begin("##background", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);
                ImGui::End();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar(2);
            }
            
            // Render menus and panels
            ShowMainMenuBar(editor_state);
            
            // File dialogs
            ShowSaveDialog(editor_state);
            ShowOpenDialog(editor_state);
            ShowRenameDialog(editor_state);
            
            if (editor_state.show_demo_window) {
                ImGui::ShowDemoWindow(&editor_state.show_demo_window);
            }
            
            ShowViewport(editor_state);
            ShowSceneHierarchy(editor_state);
            ShowInspector(editor_state);
            ShowAssetBrowser(editor_state);
            
            // Phase 6 Systems - Playback and Debug Tools
            ShowPlaybackControls(editor_state);
            ShowDebugPanels(editor_state);
            
            // Render import dialog if open
            if (editor_state.asset_import_dialog && editor_state.asset_import_dialog->IsOpen()) {
                editor_state.asset_import_dialog->RenderDialog();
            }
            
            ShowPreferences(editor_state);
            
            // Render ImGui
            ImGui::Render();
            
            // ImGui OpenGL3 backend renders to the window
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            
            // Swap buffers
            window->SwapBuffers();
            
            frame_count++;
        }
        
        spdlog::info("Editor closed after {} frames", frame_count);
        
        // Cleanup ImGui
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        
        return 0;
    }
    catch (const std::exception& e) {
        spdlog::error("Exception: {}", e.what());
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
}
