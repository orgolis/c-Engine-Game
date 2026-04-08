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
#include "transform_component.h"
#include "viewport_camera.h"
#include "mesh_renderer_component.h"
#include "simple_renderer.h"
#include "transform_gizmo.h"
#include "undo_redo_manager.h"
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
    
    // Transform Gizmo
    schizo::editor::TransformGizmo transform_gizmo;
    bool show_gizmo = true;
    
    // Play Mode
    bool is_playing = false;
    float play_time = 0.0f;
    
    // Undo/Redo
    schizo::editor::UndoRedoManager undo_redo_manager;
};

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
    
    if (ImGui::Begin("Scene Hierarchy", &editor_state.show_scene_hierarchy)) {
        auto scene = editor_state.editor_scene->GetScene();
        if (!scene) {
            ImGui::Text("No scene loaded");
            ImGui::End();
            return;
        }
        
        ImGui::Text("Scene: %s", scene->GetName().c_str());
        ImGui::Separator();
        
        // Create entity button
        if (ImGui::Button("+ Create Entity")) {
            auto entity_name = "Entity_" + std::to_string(scene->GetEntityCount());
            
            // Create undo/redo command for entity creation
            auto create_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                [scene, entity_name]() {
                    auto ent = scene->CreateEntity(entity_name);
                    spdlog::info("Created entity: {}", entity_name);
                },
                [scene, entity_name]() {
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
    }
    ImGui::End();
}

void ShowInspector(EditorState& editor_state) {
    if (!editor_state.show_inspector) return;
    
    if (ImGui::Begin("Inspector", &editor_state.show_inspector)) {
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
                    // Display component type info (C++ RTTI)
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
                ImGui::EndPopup();
            }
            
            ImGui::TreePop();
        }
        
        ImGui::Separator();
        
        if (ImGui::Button("Delete Entity", ImVec2(-1, 0))) {
            scene->RemoveEntity(selected_entity);
            editor_state.selected_entity_id = 0;
            spdlog::info("Deleted entity: {}", selected_entity->GetName());
        }
    }
    ImGui::End();
}

void ShowAssetBrowser(EditorState& editor_state) {
    if (!editor_state.show_asset_browser) return;
    
    if (ImGui::Begin("Asset Browser", &editor_state.show_asset_browser)) {
        ImGui::Text("Assets folder: assets/");
        ImGui::Separator();
        ImGui::Text("[No assets loaded]");
    }
    ImGui::End();
}

void ShowViewport(EditorState& editor_state) {
    if (!editor_state.show_viewport) return;
    
    if (ImGui::Begin("Viewport", &editor_state.show_viewport)) {
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
                editor_state.simple_renderer->ClearFramebuffer(glm::vec4(0.15f, 0.15f, 0.15f, 1.0f));
                
                // Render grid
                editor_state.simple_renderer->RenderGrid(view_matrix, proj_matrix, 1.0f, 10);
                
                // Render axes
                editor_state.simple_renderer->RenderAxes(view_matrix, proj_matrix, 5.0f);
                
                // Render entities as cubes
                if (scene) {
                    const auto& entities = scene->GetEntities();
                    for (const auto& entity : entities) {
                        auto transform = entity->GetTransform();
                        auto pos = transform->GetWorldPosition();
                        auto local_scale = transform->GetLocalScale();
                        
                        // Create model matrix
                        glm::mat4 model = glm::translate(glm::mat4(1.0f), pos);
                        model = glm::scale(model, local_scale * 0.5f);  // Scale down cubes
                        
                        // Render cube for entity
                        if (editor_state.wireframe_mode) {
                            editor_state.simple_renderer->RenderMeshWireframe(
                                editor_state.simple_renderer->GetCubeMesh(),
                                model, view_matrix, proj_matrix,
                                glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)  // White wireframe
                            );
                        } else {
                            editor_state.simple_renderer->RenderMesh(
                                editor_state.simple_renderer->GetCubeMesh(),
                                model, view_matrix, proj_matrix
                            );
                        }
                    }
                }
                
                editor_state.simple_renderer->UnbindFramebuffer();
                
                // Display framebuffer texture in ImGui
                GLuint fb_texture = editor_state.simple_renderer->GetFramebufferTexture();
                if (fb_texture != 0) {
                    ImGui::Image(
                        reinterpret_cast<void*>(static_cast<uintptr_t>(fb_texture)),
                        viewport_size,
                        ImVec2(0, 1),  // Invert Y for OpenGL texture
                        ImVec2(1, 0)
                    );
                } else {
                    ImGui::Text("Framebuffer texture not initialized");
                }
            } catch (const std::exception& e) {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "Viewport error: %s", e.what());
                spdlog::error("Viewport rendering error: {}", e.what());
            }
        }
        
        // Camera controls
        if (ImGui::IsWindowHovered()) {
            ImGuiIO& io = ImGui::GetIO();
            
            // Handle entity selection through picking
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && scene) {
                ImVec2 mouse_pos = io.MousePos;
                ImVec2 window_pos = ImGui::GetWindowPos();
                ImVec2 viewport_canvas_pos = ImGui::GetCursorScreenPos();
                
                // Calculate relative position within viewport
                float viewport_x = mouse_pos.x - (window_pos.x + 10.0f);
                float viewport_y = mouse_pos.y - (window_pos.y + 100.0f);
                
                // Clamp to viewport bounds
                viewport_x = std::max(0.0f, std::min(viewport_x, viewport_size.x));
                viewport_y = std::max(0.0f, std::min(viewport_y, viewport_size.y));
                
                if (viewport_x >= 0 && viewport_y >= 0 && viewport_x < viewport_size.x && viewport_y < viewport_size.y) {
                    // Get picking ray
                    auto [ray_origin, ray_direction] = editor_state.viewport_camera.GetPickingRay(
                        viewport_x, viewport_y, viewport_size.x, viewport_size.y
                    );
                    
                    // Test ray intersection with entities
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
            
            // Scroll to zoom
            if (io.MouseWheel != 0.0f) {
                editor_state.viewport_camera.Zoom(io.MouseWheel);
            }
            
            // Right mouse drag to pan
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                ImVec2 current_mouse = io.MousePos;
                ImVec2 delta = ImVec2(
                    (io.MousePos.x - io.MousePosPrev.x) / viewport_size.x,
                    (io.MousePos.y - io.MousePosPrev.y) / viewport_size.y
                );
                editor_state.viewport_camera.Pan(-delta.x * 10.0f, delta.y * 10.0f);
            }
            
            // Gizmo drag handling
            if (editor_state.transform_gizmo.IsDragging()) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    ImVec2 mouse_pos = io.MousePos;
                    if (scene && editor_state.selected_entity_id != 0) {
                        auto entity = scene->GetEntityById(editor_state.selected_entity_id);
                        if (entity) {
                            auto transform = entity->GetTransform();
                            auto pos = transform->GetLocalPosition();
                            auto new_pos = editor_state.transform_gizmo.UpdateDrag(
                                glm::vec2(mouse_pos.x, mouse_pos.y), pos);
                            transform->SetLocalPosition(new_pos);
                            editor_state.editor_scene->MarkModified();
                        }
                    }
                } else {
                    editor_state.transform_gizmo.EndDrag();
                }
            }
        }
    }
    ImGui::End();
}

void ShowPreferences(EditorState& editor_state) {
    if (!editor_state.show_preferences) return;
    
    if (ImGui::Begin("Preferences", &editor_state.show_preferences, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Editor Settings");
        ImGui::Separator();
        ImGui::Checkbox("Vsync", nullptr);
        ImGui::Checkbox("Show Grid", nullptr);
        ImGui::SliderFloat("Grid Size", nullptr, 0.1f, 10.0f);
        ImGui::Separator();
        
        if (ImGui::Button("Close")) {
            editor_state.show_preferences = false;
        }
    }
    ImGui::End();
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
        
        // Initialize 3D renderer
        spdlog::info("Initializing SimpleRenderer...");
        editor_state.simple_renderer = std::make_unique<schizo::editor::SimpleRenderer>();
        editor_state.simple_renderer->Initialize();
        
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
                editor_state.is_playing = !editor_state.is_playing;
                editor_state.play_time = 0.0f;
                spdlog::info(editor_state.is_playing ? "Play mode started (F5)" : "Play mode stopped (F5)");
            }
            
            // Update play time if in play mode
            if (editor_state.is_playing) {
                editor_state.play_time += 0.016f;  // Approximate 60 FPS delta time
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
