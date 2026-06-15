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
#include "vulkan/vulkan_transparent_pass.h"
#include "vulkan/vulkan_environment_map.h"
#include "vulkan/vulkan_ssao_pass.h"
#include "vulkan/vulkan_ssr_pass.h"
#include "vulkan/vulkan_rt_scene.h"
#include "vulkan/vulkan_vxao_pass.h"
#include "vulkan/vulkan_occlusion_culler.h"
#include "vulkan/vulkan_hzb_culler.h"
#include "vulkan/vulkan_render_graph.h"
#include "vulkan/vulkan_scene_mesh.h"
#include "vulkan/vulkan_scene_material.h"
#include "vulkan/imgui_vulkan.h"

// Engine foundation (Master Plan Stage 0/1): job system, frame allocator,
// reflected ECS components. (Component registration only needs reflection +
// glm; the EnTT World is not pulled into this TU yet — the OOP-scene → ECS
// migration is a separate pass.)
#include "jobs/job_system.h"
#include "memory/memory.h"
#include "profiler/profiler.h" // scoped CPU zones, per-thread (Stage 0.6)
#include "ecs/components.h"
#include "ecs_bridge.h"        // shadow ECS mirror of the scene (Stage 1.4 step 1)

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
#include "camera_component.h"
#include "viewport_camera.h"
#include "mesh_renderer_component.h"
#include "collider_component.h"
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

#ifdef _WIN32
  // Native file-open / file-save dialogs (Win32 commdlg). Pulled in just for
  // the Save/Open Scene menu items — keep WIN32_LEAN_AND_MEAN so we don't
  // drag the whole Windows kitchen sink into this TU.
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
  #include <commdlg.h>
  #define GLFW_EXPOSE_NATIVE_WIN32
  #include <GLFW/glfw3native.h>
#endif
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

// Native Win32 file pickers. Block the calling thread (the editor's main
// loop) for the duration of the dialog, which is the expected behaviour.
// Filter is *.scene; default extension is added when the user types just
// "foo" into the Save dialog.
#ifdef _WIN32
static std::string OpenSceneDialogNative(GLFWwindow* window) {
    char buf[MAX_PATH] = {0};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = window ? glfwGetWin32Window(window) : NULL;
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = sizeof(buf);
    ofn.lpstrFilter = "Scene Files (*.scene)\0*.scene\0All Files\0*.*\0";
    ofn.lpstrTitle  = "Open Scene";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn) ? std::string(buf) : std::string();
}

static std::string SaveSceneDialogNative(GLFWwindow* window) {
    char buf[MAX_PATH] = {0};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = window ? glfwGetWin32Window(window) : NULL;
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = sizeof(buf);
    ofn.lpstrFilter = "Scene Files (*.scene)\0*.scene\0All Files\0*.*\0";
    ofn.lpstrTitle  = "Save Scene As";
    ofn.lpstrDefExt = "scene";
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetSaveFileNameA(&ofn) ? std::string(buf) : std::string();
}
#else
static std::string OpenSceneDialogNative(GLFWwindow*) { return {}; }
static std::string SaveSceneDialogNative(GLFWwindow*) { return {}; }
#endif

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

void ShowMainMenuBar(EditorState& editor_state, GLFWwindow* glfw_window) {
    if (ImGui::BeginMainMenuBar()) {
        // File menu
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                editor_state.editor_scene->NewScene("Untitled");
                spdlog::info("New scene created");
            }
            if (ImGui::MenuItem("Open Scene", "Ctrl+O")) {
                std::string path = OpenSceneDialogNative(glfw_window);
                if (!path.empty()) {
                    editor_state.editor_scene->LoadScene(path);
                    spdlog::info("Scene loaded from: {}", path);
                }
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                auto filepath = editor_state.editor_scene->GetSceneFilepath();
                if (filepath.empty()) {
                    // No file backing the scene yet — fall through to Save As.
                    filepath = SaveSceneDialogNative(glfw_window);
                }
                if (!filepath.empty()) {
                    editor_state.editor_scene->SaveScene(filepath);
                    spdlog::info("Scene saved to: {}", filepath);
                }
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
                std::string path = SaveSceneDialogNative(glfw_window);
                if (!path.empty()) {
                    editor_state.editor_scene->SaveScene(path);
                    spdlog::info("Scene saved to: {}", path);
                }
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
            
            const bool playing_now = editor_state.scene_playback_manager &&
                                     editor_state.scene_playback_manager->IsPlaying();
            const char* play_label = playing_now ? "Stop (F5)" : "Play (F5)";
            if (ImGui::MenuItem(play_label)) {
                if (editor_state.scene_playback_manager) {
                    if (playing_now) {
                        editor_state.scene_playback_manager->StopPlayback();
                    } else if (auto scene = editor_state.editor_scene->GetScene()) {
                        if (!editor_state.scene_playback_manager->StartPlayback(scene)) {
                            spdlog::warn("Failed to start scene playback (no entity named 'Player'?)");
                        }
                    }
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

                if (ImGui::BeginMenu("Collider##add_collider")) {
                    // One collider per entity — adding a second would just sit
                    // there unused by the (single-collider) Phase 2 lookup.
                    const bool has_collider =
                        selected_entity->GetComponent<schizo::scene::ColliderComponent>() != nullptr;
                    ImGui::BeginDisabled(has_collider);
                    auto add_collider = [&](schizo::scene::ColliderShape s, const char* label) {
                        if (ImGui::MenuItem(label)) {
                            selected_entity->AddComponent<schizo::scene::ColliderComponent>(s);
                            editor_state.editor_scene->MarkModified();
                            spdlog::info("Added {} collider to entity: {}",
                                         label, selected_entity->GetName());
                            ImGui::CloseCurrentPopup();
                        }
                    };
                    add_collider(schizo::scene::ColliderShape::Box,      "Box Collider");
                    add_collider(schizo::scene::ColliderShape::Sphere,   "Sphere Collider");
                    add_collider(schizo::scene::ColliderShape::Capsule,  "Capsule Collider");
                    add_collider(schizo::scene::ColliderShape::Cylinder, "Cylinder Collider");
                    add_collider(schizo::scene::ColliderShape::Plane,    "Plane Collider");
                    add_collider(schizo::scene::ColliderShape::Mesh,     "Mesh Collider (uses MeshComponent)");
                    ImGui::EndDisabled();
                    if (has_collider) {
                        ImGui::Separator();
                        ImGui::TextDisabled("(already has a Collider)");
                    }
                    ImGui::EndMenu();
                }

                // One camera per entity — pick the active one in the
                // viewport/runtime by entity selection, not by stacking.
                {
                    const bool has_camera =
                        selected_entity->GetComponent<schizo::scene::CameraComponent>() != nullptr;
                    ImGui::BeginDisabled(has_camera);
                    if (ImGui::MenuItem("Camera")) {
                        selected_entity->AddComponent<schizo::scene::CameraComponent>();
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Added CameraComponent to entity: {}", selected_entity->GetName());
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndDisabled();
                    if (has_camera) {
                        ImGui::Separator();
                        ImGui::TextDisabled("(already has a Camera)");
                    }
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

        // Collider Component Properties (Phase 1 data layer — physics not
        // simulated yet; editing this just records authoring data that the
        // Phase 2 PhysicsWorld lifecycle will consume on StartPlayback).
        ImGui::Separator();
        if (auto collider = selected_entity->GetComponent<schizo::scene::ColliderComponent>()) {
            if (ImGui::TreeNode("Collider")) {
                using schizo::scene::ColliderShape;
                const char* shape_names[] = { "Box", "Sphere", "Plane", "Capsule", "Cylinder", "Mesh" };
                int shape_idx = static_cast<int>(collider->GetShape());
                if (ImGui::Combo("Shape##collider", &shape_idx, shape_names,
                                 IM_ARRAYSIZE(shape_names))) {
                    collider->SetShape(static_cast<ColliderShape>(shape_idx));
                    editor_state.editor_scene->MarkModified();
                }

                // Per-shape fields. Hide ones that don't apply to the
                // selected shape so the inspector stays uncluttered.
                ColliderShape shape = collider->GetShape();
                if (shape == ColliderShape::Box) {
                    glm::vec3 he = collider->GetHalfExtents();
                    if (ImGui::DragFloat3("Half Extents##collider", &he.x, 0.05f, 0.01f, 1000.0f)) {
                        collider->SetHalfExtents(he);
                        editor_state.editor_scene->MarkModified();
                    }
                } else if (shape == ColliderShape::Sphere) {
                    float r = collider->GetRadius();
                    if (ImGui::DragFloat("Radius##collider", &r, 0.05f, 0.01f, 1000.0f)) {
                        collider->SetRadius(r);
                        editor_state.editor_scene->MarkModified();
                    }
                } else if (shape == ColliderShape::Capsule ||
                           shape == ColliderShape::Cylinder) {
                    float r = collider->GetRadius();
                    if (ImGui::DragFloat("Radius##collider", &r, 0.05f, 0.01f, 1000.0f)) {
                        collider->SetRadius(r);
                        editor_state.editor_scene->MarkModified();
                    }
                    float h = collider->GetHeight();
                    if (ImGui::DragFloat("Height##collider", &h, 0.05f, 0.0f, 1000.0f)) {
                        collider->SetHeight(h);
                        editor_state.editor_scene->MarkModified();
                    }
                } else if (shape == ColliderShape::Plane) {
                    glm::vec3 n = collider->GetPlaneNormal();
                    if (ImGui::DragFloat3("Normal##collider", &n.x, 0.05f, -1.0f, 1.0f)) {
                        collider->SetPlaneNormal(n);
                        editor_state.editor_scene->MarkModified();
                    }
                } else if (shape == ColliderShape::Mesh) {
                    // Geometry comes from the entity's MeshComponent rather
                    // than from collider fields — show the source path so
                    // the user knows whether play mode will find anything
                    // to test against.
                    auto* mc = selected_entity->GetMeshComponent();
                    if (mc && !mc->mesh_path.empty()) {
                        ImGui::TextWrapped("Source: %s", mc->mesh_path.c_str());
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                            "No MeshComponent path set — assign a glTF/OBJ via drag-drop\n"
                            "or this collider will be skipped on Play.");
                    }
                }

                glm::vec3 offset = collider->GetOffset();
                if (ImGui::DragFloat3("Offset##collider", &offset.x, 0.05f)) {
                    collider->SetOffset(offset);
                    editor_state.editor_scene->MarkModified();
                }

                ImGui::Separator();
                bool dynamic = collider->IsDynamic();
                if (ImGui::Checkbox("Dynamic (responds to gravity)##collider", &dynamic)) {
                    collider->SetDynamic(dynamic);
                    editor_state.editor_scene->MarkModified();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Off  = Static: never moves, acts as immovable level geometry.\n"
                                      "On   = Dynamic: falls under gravity, bounces, gets pushed.\n"
                                      "The Player is special-cased to Kinematic regardless.");
                }
                if (dynamic) {
                    float mass = collider->GetMass();
                    if (ImGui::DragFloat("Mass (kg)##collider", &mass, 0.1f, 0.001f, 10000.0f)) {
                        collider->SetMass(mass);
                        editor_state.editor_scene->MarkModified();
                    }
                }

                bool is_trigger = collider->IsTrigger();
                if (ImGui::Checkbox("Trigger (overlap, no resolve)##collider", &is_trigger)) {
                    collider->SetTrigger(is_trigger);
                    editor_state.editor_scene->MarkModified();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Triggers still register contacts (game code can read them via\n"
                                      "PhysicsWorld::GetBodyContacts) but bodies pass through them\n"
                                      "instead of being pushed apart. Use for damage volumes, pickups,\n"
                                      "checkpoints, etc.");
                }

                if (ImGui::TreeNode("Collision Filtering##collider")) {
                    int layer = static_cast<int>(collider->GetLayer());
                    if (ImGui::DragInt("Layer (0-31)##collider", &layer, 0.1f, 0, 31)) {
                        collider->SetLayer(static_cast<uint8_t>(layer));
                        editor_state.editor_scene->MarkModified();
                    }
                    uint32_t mask = collider->GetMask();
                    bool changed = false;
                    if (ImGui::SmallButton("All##mask"))   { mask = 0xFFFFFFFFu; changed = true; }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("None##mask"))  { mask = 0u;          changed = true; }
                    // 8 columns × 4 rows of layer-bit toggles.
                    for (int row = 0; row < 4; ++row) {
                        for (int col = 0; col < 8; ++col) {
                            int bit = row * 8 + col;
                            bool on = (mask >> bit) & 1u;
                            char label[16];
                            std::snprintf(label, sizeof(label), "L%d##m", bit);
                            if (ImGui::Checkbox(label, &on)) {
                                mask = on ? (mask | (1u << bit)) : (mask & ~(1u << bit));
                                changed = true;
                            }
                            if (col < 7) ImGui::SameLine();
                        }
                    }
                    if (changed) {
                        collider->SetMask(mask);
                        editor_state.editor_scene->MarkModified();
                    }
                    ImGui::TreePop();
                }

                ImGui::Separator();
                if (ImGui::Button("Remove Collider##collider")) {
                    selected_entity->RemoveComponent(collider);
                    editor_state.editor_scene->MarkModified();
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

            // Base colour + cutout transparency. These edit the
            // MeshRendererComponent (which is what the renderer actually
            // reads), not the decorative state the Material Editor above
            // uses. The "Material" section above is currently visual only
            // and doesn't write back to the entity.
            if (auto mr = selected_entity->GetComponent<schizo::scene::MeshRendererComponent>()) {
                ImGui::Separator();
                ImGui::Text("Mesh Renderer (live)");

                glm::vec4 col = mr->GetColor();
                if (ImGui::ColorEdit4("Color (RGBA)##mr", &col.r,
                                      ImGuiColorEditFlags_AlphaBar |
                                      ImGuiColorEditFlags_AlphaPreview)) {
                    mr->SetColor(col);
                    editor_state.editor_scene->MarkModified();
                }

                float metallic = mr->GetMetallic();
                if (ImGui::SliderFloat("Metallic##mr", &metallic, 0.0f, 1.0f)) {
                    mr->SetMetallic(metallic);
                    editor_state.editor_scene->MarkModified();
                }

                float roughness = mr->GetRoughness();
                if (ImGui::SliderFloat("Roughness##mr", &roughness, 0.04f, 1.0f)) {
                    mr->SetRoughness(roughness);
                    editor_state.editor_scene->MarkModified();
                }

                float occlusion = mr->GetOcclusion();
                if (ImGui::SliderFloat("Occlusion##mr", &occlusion, 0.0f, 1.0f)) {
                    mr->SetOcclusion(occlusion);
                    editor_state.editor_scene->MarkModified();
                }

                glm::vec3 emissive = mr->GetEmissive();
                if (ImGui::ColorEdit3("Emissive##mr", &emissive.r,
                                      ImGuiColorEditFlags_HDR |
                                      ImGuiColorEditFlags_Float)) {
                    mr->SetEmissive(emissive);
                    editor_state.editor_scene->MarkModified();
                }
                // Emissive intensity — HDR multiplier. Bloom threshold is
                // ~1.0, so push this above ~1.5 to make the surface glow.
                float emissive_intensity = mr->GetEmissiveIntensity();
                if (ImGui::SliderFloat("Emissive glow##mr", &emissive_intensity,
                                       0.0f, 10.0f)) {
                    mr->SetEmissiveIntensity(emissive_intensity);
                    editor_state.editor_scene->MarkModified();
                }

                // Alpha mode — radio buttons (rather than a Combo) so all
                // three choices are always visible, can't be clipped by a
                // collapsed dropdown, and the current value is obvious at a
                // glance.
                ImGui::Text("Alpha Mode:");
                int am = static_cast<int>(mr->GetAlphaMode());
                bool changed = false;
                ImGui::SameLine();
                changed |= ImGui::RadioButton("Opaque##mr_am", &am, 0);
                ImGui::SameLine();
                changed |= ImGui::RadioButton("Cutout##mr_am", &am, 1);
                ImGui::SameLine();
                changed |= ImGui::RadioButton("Blend##mr_am",  &am, 2);
                if (changed) {
                    mr->SetAlphaMode(static_cast<schizo::scene::AlphaMode>(am));
                    editor_state.editor_scene->MarkModified();
                }
                ImGui::TextDisabled(
                    "Opaque: no transparency.   Cutout: hard discard below cutoff.   "
                    "Blend: real translucency.");
                if (mr->GetAlphaMode() == schizo::scene::AlphaMode::Cutout) {
                    float cutoff = mr->GetAlphaCutoff();
                    if (ImGui::SliderFloat("Alpha Cutoff##mat", &cutoff, 0.0f, 1.0f)) {
                        mr->SetAlphaCutoff(cutoff);
                        editor_state.editor_scene->MarkModified();
                    }
                }
            }
            ImGui::TreePop();
        }

        // Camera Component (intrinsic camera attached to this entity)
        if (auto cam = selected_entity->GetComponent<schizo::scene::CameraComponent>()) {
            ImGui::Separator();
            if (ImGui::TreeNode("Camera")) {
                // Projection type radios — same pattern as the AlphaMode
                // control: all options always visible, no hidden popup.
                ImGui::Text("Projection:");
                int proj = static_cast<int>(cam->GetProjection());
                bool changed = false;
                ImGui::SameLine();
                changed |= ImGui::RadioButton("Perspective##cam_proj",  &proj, 0);
                ImGui::SameLine();
                changed |= ImGui::RadioButton("Orthographic##cam_proj", &proj, 1);
                if (changed) {
                    cam->SetProjection(static_cast<schizo::scene::CameraProjection>(proj));
                    editor_state.editor_scene->MarkModified();
                }

                if (cam->GetProjection() == schizo::scene::CameraProjection::Perspective) {
                    float fov = cam->GetFOV();
                    if (ImGui::SliderFloat("FOV (deg)##cam", &fov, 10.0f, 120.0f)) {
                        cam->SetFOV(fov);
                        editor_state.editor_scene->MarkModified();
                    }
                } else {
                    float ortho = cam->GetOrthographicSize();
                    if (ImGui::DragFloat("Ortho Size##cam", &ortho, 0.1f, 0.1f, 1000.0f)) {
                        cam->SetOrthographicSize(ortho);
                        editor_state.editor_scene->MarkModified();
                    }
                }

                float np = cam->GetNearPlane();
                if (ImGui::DragFloat("Near##cam", &np, 0.01f, 0.001f, 1000.0f)) {
                    cam->SetNearPlane(np);
                    editor_state.editor_scene->MarkModified();
                }
                float fp = cam->GetFarPlane();
                if (ImGui::DragFloat("Far##cam",  &fp, 1.0f, np + 0.01f, 100000.0f)) {
                    cam->SetFarPlane(fp);
                    editor_state.editor_scene->MarkModified();
                }

                glm::vec4 cc = cam->GetClearColor();
                if (ImGui::ColorEdit4("Clear Color##cam", &cc.r,
                                      ImGuiColorEditFlags_AlphaBar |
                                      ImGuiColorEditFlags_AlphaPreview)) {
                    cam->SetClearColor(cc);
                    editor_state.editor_scene->MarkModified();
                }
                ImGui::TreePop();
            }
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
        auto scene = editor_state.editor_scene->GetScene();

        // Play mode indicator and controls — all play UI must drive
        // ScenePlaybackManager, so the gizmo overlay (gated on
        // scene_playback_manager->IsPlaying()) and the player update loop
        // see a consistent "playing" state.
        const bool viewport_playing = editor_state.scene_playback_manager &&
                                      editor_state.scene_playback_manager->IsPlaying();
        if (viewport_playing) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));  // Green text
            ImGui::Text(">>> PLAY MODE (%.2f s) <<<",
                        editor_state.scene_playback_manager->GetPlaybackTime());
            ImGui::PopStyleColor();
        } else {
            ImGui::Text("Edit Mode");
        }
        ImGui::SameLine();

        if (ImGui::Button(viewport_playing ? "Stop (F5)" : "Play (F5)")) {
            if (editor_state.scene_playback_manager) {
                if (viewport_playing) {
                    editor_state.scene_playback_manager->StopPlayback();
                } else if (scene) {
                    if (!editor_state.scene_playback_manager->StartPlayback(scene)) {
                        spdlog::warn("Failed to start scene playback (no entity named 'Player'?)");
                    }
                }
            }
        }
        ImGui::SameLine();
        
        if (ImGui::Button("Reset Camera")) {
            editor_state.viewport_camera.Reset();
        }
        ImGui::Separator();

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
        bool gizmo_scene_playing = editor_state.scene_playback_manager && editor_state.scene_playback_manager->IsPlaying();
        if (image_drawn && editor_state.show_gizmo &&
            scene && editor_state.selected_entity_id != 0 && !gizmo_scene_playing)
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
                    
                    // First check if gizmo is visible and try to hit an axis (only in edit mode, not during play)
                    bool play_mode_check = editor_state.scene_playback_manager && editor_state.scene_playback_manager->IsPlaying();
                    if (editor_state.show_gizmo && editor_state.selected_entity_id != 0 && !play_mode_check) {
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
            
            // Middle mouse drag to rotate (disabled during scene playback)
            bool is_scene_playing = editor_state.scene_playback_manager && editor_state.scene_playback_manager->IsPlaying();
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) && !is_scene_playing) {
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
            
            // Scroll to zoom (only if not dragging gizmo, and not during scene playback)
            bool is_scene_playing_zoom = editor_state.scene_playback_manager && editor_state.scene_playback_manager->IsPlaying();
            if (io.MouseWheel != 0.0f && !editor_state.gizmo_dragging && !is_scene_playing_zoom) {
                editor_state.viewport_camera.Zoom(io.MouseWheel);
            }
            
            // Arrow key camera movement (camera speed = 1.0)
            // Only when viewport is focused (active window) and not dragging gizmo, and scene is not playing
            bool scene_playing = editor_state.scene_playback_manager && editor_state.scene_playback_manager->IsPlaying();
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !editor_state.gizmo_dragging && !scene_playing) {
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
            
            // Right mouse drag to pan (only if not dragging gizmo, and not during scene playback)
            bool is_scene_playing_pan = editor_state.scene_playback_manager && editor_state.scene_playback_manager->IsPlaying();
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right) && !editor_state.gizmo_dragging && !is_scene_playing_pan) {
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
        // Flush every info+ line immediately so logs survive a hard kill / crash
        // and show up live when stdout is redirected to a file (otherwise the C
        // runtime fully-buffers a redirected stream and the tail is lost).
        spdlog::flush_on(spdlog::level::info);
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
        // Scene LightComponents are synced into the lighting pass each
        // frame in the render loop below. If the scene contains no
        // Directional LightComponent, that sync injects a default sun so
        // an empty scene isn't pitch black.

        ShadowMapConfig s_cfg{};
        // 2048² is the sweet spot for a single cascade on a desktop GPU:
        // crisp without dominating frame time. Drop to 1024 on lower-end
        // hardware if needed.
        s_cfg.width = s_cfg.height = 2048;
        s_cfg.cascade_count = 1;
        // Bind the material descriptor set in the shadow caster pipeline so
        // Cutout/Blend objects can do alpha-tested shadow casting (sampling
        // the albedo + reading the per-material cutoff from emissive.a).
        s_cfg.material_set_layout = mat_layout;
        auto shadow_map = VulkanShadowMap::create(&device, s_cfg);
        // Bind the shadow map view + sampler to the deferred lighting pass.
        // Static binding — the shadow_map's image identity doesn't change
        // across frames; only the depth contents do (rewritten each frame
        // by the Shadow stage).
        if (shadow_map && lighting) {
            lighting->set_directional_shadow_map(shadow_map->get_shadow_view(),
                                                  shadow_map->get_shadow_sampler());
        }

        PostProcessingConfig pp_cfg{};
        pp_cfg.width  = kW;
        pp_cfg.height = kH;
        pp_cfg.bloom.enabled        = true;
        pp_cfg.taa.enabled          = false;
        pp_cfg.tone_mapping.enabled = true;
        auto post_processing = VulkanPostProcessing::create(&device, pp_cfg);
        // Give auto-exposure the G-Buffer depth so it can exclude sky
        // pixels from metering — must be set before set_input_image (which
        // lazily initialises auto-exposure).
        post_processing->set_scene_depth(g_buffer->get_depth_view());
        post_processing->set_input_image(lighting->get_output_view());

        // Forward transparent pass — runs after lighting, reuses the HDR colour
        // image as its render target and the G-Buffer depth as a read-only
        // depth attachment. AlphaMode::Blend entities are routed through this
        // pass via the editor's draw-list filter.
        auto transparent = VulkanTransparentPass::create(
            &device,
            lighting->get_output_view(),
            VK_FORMAT_R16G16B16A16_SFLOAT,
            g_buffer->get_depth_view(),
            VK_FORMAT_D32_SFLOAT,
            kW, kH,
            mat_layout);
        // Environment cubemap. Looks for an HDR equirectangular file under
        // assets/skies/*.hdr; falls back to a procedural gradient cubemap
        // when no asset is present. Same data path either way — the sky
        // pass and (future) IBL precompute sample this cubemap.
        std::unique_ptr<VulkanEnvironmentMap> env_map;
        {
            namespace fs = std::filesystem;
            const fs::path skies_dir = "assets/skies";
            fs::path hdr_path;
            std::error_code ec;
            if (fs::exists(skies_dir, ec) && fs::is_directory(skies_dir, ec)) {
                for (const auto& entry : fs::directory_iterator(skies_dir, ec)) {
                    const auto ext = entry.path().extension().string();
                    if (ext == ".hdr" || ext == ".HDR") {
                        hdr_path = entry.path();
                        break;
                    }
                }
            }
            if (!hdr_path.empty()) {
                env_map = VulkanEnvironmentMap::create_from_hdr(
                    &device, hdr_path.string(), 512);
            }
            if (!env_map) {
                env_map = VulkanEnvironmentMap::create_procedural(&device, 256);
            }
            if (env_map) {
                // One-shot bake of irradiance + prefiltered specular +
                // BRDF LUT. The textures live for the editor session and
                // are sampled by the deferred lighting shader (and, later,
                // the forward transparent shader).
                env_map->bake_ibl();
            }
        }

        // SSAO compute pass. Reads the G-Buffer (position, normal, depth)
        // and writes a per-pixel occlusion image consumed by the lighting
        // pass. Dispatched between Geometry and Lighting in the frame
        // loop below.
        auto ssao = VulkanSsaoPass::create(&device, g_buffer.get(), kW, kH,
                                           /*use_rt=*/device.has_ray_tracing());
        if (ssao) {
            lighting->set_ssao_texture(ssao->get_output_view(),
                                       ssao->get_output_sampler());
        }

        // Voxel-cone-traced AO — the world-space, non-RT member of the AO
        // suite. Built unconditionally so the technique selector can switch
        // to it; only runs when VXAO is the active technique.
        auto vxao = VulkanVxaoPass::create(&device, g_buffer.get(), kW, kH);
        bool vxao_bound = false; // tracks which AO texture lighting samples

        // SSR — runs after Lighting (needs lit HDR) and before Transparent
        // (so transparent fragments composite over reflections). The env
        // cubemap is sampled on a ray miss so SSR is the single source of
        // specular reflection (object on-screen, sky elsewhere).
        auto ssr = (env_map)
            ? VulkanSsrPass::create(
                  &device, g_buffer.get(),
                  lighting->get_output_view(),
                  VK_FORMAT_R16G16B16A16_SFLOAT,
                  env_map->get_view(), env_map->get_sampler(),
                  kW, kH,
                  /*use_rt=*/device.has_ray_tracing())
            : nullptr;

        // Hand the IBL textures + env cubemap to the deferred lighting
        // pass. Lighting now samples envCubemap directly at empty-depth
        // pixels (the sky branch is folded into the lighting shader), so
        // the separate sky pass is no longer used.
        if (env_map) {
            lighting->set_env_cubemap(env_map->get_view(), env_map->get_sampler());
            if (env_map->ibl_ready()) {
                lighting->set_ibl_textures(
                    env_map->get_irradiance_view(),  env_map->get_sampler(),
                    env_map->get_prefiltered_view(), env_map->get_sampler(),
                    env_map->get_brdf_lut_view(),    env_map->get_brdf_lut_sampler(),
                    env_map->get_prefilter_mips());
            }
        }

        // Phase 1 RT scaffold: enable the toggle on the lighting pass only
        // when the device actually supports the four KHR extensions and the
        // rayQuery + accelerationStructure features. The shader path that
        // reads this flag lands in Phase 3 (RT shadows). Until then the
        // toggle is purely informational.
        std::unique_ptr<VulkanRtScene> rt_scene;
        if (device.has_ray_tracing()) {
            lighting->set_rt_enabled(true);
            lighting->set_rt_ao_enabled(true); // Phase 4: RT AO replaces SSAO
            rt_scene = VulkanRtScene::create(&device);
            spdlog::info("Ray tracing toggle ON (shadows + AO); RT scene {}",
                         rt_scene ? "created" : "creation failed");
        } else {
            spdlog::info("Ray tracing not available on this GPU — raster path only");
        }

        // (Sky is rendered inline inside the deferred lighting pass — see
        // lighting_pass.frag's sky branch. No separate sky stage.)

        if (transparent) {
            // Wire the transparent pass to the same dynamic light SSBO and
            // shadow textures the deferred lighting pass uses, so blend-mode
            // surfaces receive the same lighting (PBR + multi-light + PCF
            // shadows) as opaque ones.
            transparent->set_ambient(l_cfg.ambient_color, l_cfg.global_ambient);
            transparent->set_lighting_resources(
                lighting->get_light_buffer(),
                lighting->get_max_lights(),
                lighting->get_effective_directional_shadow_view(),
                lighting->get_effective_directional_shadow_sampler(),
                lighting->get_effective_point_shadow_view(),
                lighting->get_effective_point_shadow_sampler());

            // Same IBL data the deferred lighting samples — keeps the
            // blend-mode and opaque ambient terms consistent.
            if (env_map && env_map->ibl_ready()) {
                transparent->set_ibl_textures(
                    env_map->get_irradiance_view(),  env_map->get_sampler(),
                    env_map->get_prefiltered_view(), env_map->get_sampler(),
                    env_map->get_brdf_lut_view(),    env_map->get_brdf_lut_sampler(),
                    env_map->get_prefilter_mips());
            }
        }

        if (!g_buffer || !lighting || !shadow_map || !post_processing || !transparent) {
            spdlog::error("Deferred pipeline component construction failed");
            device.shutdown();
            glfwDestroyWindow(glfw_window);
            glfwTerminate();
            return 1;
        }

        // Occlusion-query culler stays unwired (per-query design hangs the
        // editor — see feedback_culling_traps memory).
        std::unique_ptr<VulkanOcclusionCuller> occlusion_culler; // intentionally null

        // HZB occlusion culler. The original "HZB false culls" we chased
        // earlier turned out to be a cocktail of other bugs (cube LEFT-face
        // winding, degenerate-AABB plane culling, OBJ winding + back-face
        // cull mismatch) — all since fixed. Re-enabling HZB now that the
        // rendering pipeline is consistent.
        VulkanHzbCuller::Config hzb_cfg{};
        hzb_cfg.depth_format = VK_FORMAT_D32_SFLOAT;
        hzb_cfg.depth_width  = kW;
        hzb_cfg.depth_height = kH;
        hzb_cfg.max_draws    = 4096;
        hzb_cfg.readback_mip = 4;
        auto hzb_culler = VulkanHzbCuller::create(&device, hzb_cfg);
        if (hzb_culler) {
            hzb_culler->set_depth_view(g_buffer->get_depth_view());
        }

        RenderGraphConfig graph_cfg{};
        graph_cfg.device           = &device;
        graph_cfg.g_buffer         = g_buffer.get();
        graph_cfg.lighting         = lighting.get();
        graph_cfg.shadow_map       = shadow_map.get();
        graph_cfg.post_processing  = post_processing.get();
        graph_cfg.transparent      = transparent.get();
        graph_cfg.occlusion_culler = nullptr;
        graph_cfg.hzb_culler       = hzb_culler.get();
        graph_cfg.width            = kW;
        graph_cfg.height           = kH;
        auto graph = VulkanRenderGraph::create(graph_cfg);
        if (!graph) {
            spdlog::error("VulkanRenderGraph::create failed");
            device.shutdown();
            glfwDestroyWindow(glfw_window);
            glfwTerminate();
            return 1;
        }
        // CPU-side frustum culling: skip draws whose AABB is outside the view
        // frustum. Default-off on the graph for backwards-compat, but for a
        // real editor session we want it on — it dramatically improves perf
        // on scenes with many off-screen objects.
        graph->set_frustum_culling_enabled(true);
        spdlog::info("Deferred pipeline + render graph ready (frustum culling ON)");

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

                // Ground already gets a static box collider from CreatePlane.
                schizo::scene::EntityFactory::CreatePlane(scene, "Ground", 10.0f, 10.0f,
                    glm::vec4(0.45f, 0.45f, 0.45f, 1.0f));

                // Two cubes — CreateCube already gives them a Box collider; flag
                // it DYNAMIC so they fall + land on the ground when you press
                // Play (Stage-4 Jolt physics demo).
                auto make_dynamic = [](const std::shared_ptr<schizo::scene::Entity>& e) {
                    if (auto col = e->GetComponent<schizo::scene::ColliderComponent>()) {
                        col->SetDynamic(true);
                        col->SetMass(1.0f);
                    }
                };
                auto parent_cube = schizo::scene::EntityFactory::CreateCube(scene);
                parent_cube->SetName("Parent_Cube");
                parent_cube->GetTransform()->SetLocalPosition(glm::vec3(0.0f, 4.0f, 0.0f));
                make_dynamic(parent_cube);

                auto child_cube = schizo::scene::EntityFactory::CreateCube(scene);
                child_cube->SetName("Falling_Cube");
                child_cube->GetTransform()->SetLocalPosition(glm::vec3(0.6f, 7.0f, 0.3f));
                make_dynamic(child_cube);

                // A dynamic CUSTOM-MESH collider (uses the convex-hull path —
                // Jolt triangle meshes are static-only). Falls + lands like the
                // cubes when you press Play.
                auto mesh_cube = scene->CreateEntity("MeshCube");
                mesh_cube->GetTransform()->SetLocalPosition(glm::vec3(2.5f, 5.5f, -1.0f));
                mesh_cube->SetMesh("../../assets/test_cooked/cube3d.obj");  // visual + collider source
                {
                    auto col = mesh_cube->AddComponent<schizo::scene::ColliderComponent>(
                        schizo::scene::ColliderShape::Mesh);
                    col->SetDynamic(true);
                    col->SetMass(1.0f);
                }

                // Stage 4: a "Player" (capsule collider + cameras) so Play works
                // out of the box — it becomes a Jolt CharacterVirtual.
                schizo::scene::EntityFactory::CreatePlayer(scene, "Player",
                    glm::vec3(-2.5f, 1.0f, 0.0f));

                spdlog::info("Default scene: {} entities (player + 2 dynamic cubes + ground)",
                             scene->GetEntityCount());

                // One-time headless self-check of the MIGRATED play-mode physics:
                // run the play manager (which builds a Jolt world from the scene's
                // colliders + a CharacterVirtual player) and confirm a dynamic
                // cube actually falls. StopPlayback restores the scene so the user
                // presses Play on a clean state.
                if (editor_state.scene_playback_manager) {
                    const float cy0 = parent_cube->GetTransform()->GetWorldPosition().y;
                    const float my0 = mesh_cube->GetTransform()->GetWorldPosition().y;
                    if (editor_state.scene_playback_manager->StartPlayback(scene)) {
                        for (int i = 0; i < 90; ++i)
                            editor_state.scene_playback_manager->Update(1.0f / 60.0f);
                        const float cy1 = parent_cube->GetTransform()->GetWorldPosition().y;
                        const float my1 = mesh_cube->GetTransform()->GetWorldPosition().y;
                        spdlog::info("Stage 4 play-mode physics (Jolt) self-check: box y {:.2f}->{:.2f} ({}), "
                                     "MESH-collider y {:.2f}->{:.2f} ({})",
                                     cy0, cy1, (cy1 < cy0 - 1.0f) ? "fell OK" : "CHECK",
                                     my0, my1, (my1 < my0 - 1.0f) ? "fell OK (convex hull)" : "CHECK");
                        editor_state.scene_playback_manager->StopPlayback();
                    }
                }

                // ----------------------------------------------------------------
                // Stage 2 cook->runtime loop — live verification on real cooked
                // content. Find a cooked scene bundle next to the editor, load it
                // via mmap (NO source .obj/.gltf parse), log what came through,
                // and drop one entity into the scene so it renders in the
                // viewport (proves "cooked scene loads via mmap with no runtime
                // parse"; the precomputed LOD chain becomes the runtime LODs).
                // ----------------------------------------------------------------
                {
                    namespace fs = std::filesystem;
                    std::string pak_path;
                    uintmax_t   best_size = 0;
                    const char* dirs[] = { "cooked", "bin/cooked", "../cooked", "assets/cooked" };
                    for (const char* d : dirs) {
                        std::error_code ec;
                        if (!fs::is_directory(d, ec)) continue;
                        for (const auto& e : fs::directory_iterator(d, ec)) {
                            if (!e.is_regular_file()) continue;
                            const std::string p = e.path().string();
                            if (p.size() < 4 || p.substr(p.size() - 4) != ".pak") continue;
                            if (p.find(".tex.pak") != std::string::npos) continue;  // textures
                            const uintmax_t sz = fs::file_size(e.path(), ec);
                            if (pak_path.empty() || sz < best_size) { pak_path = p; best_size = sz; }
                        }
                        if (!pak_path.empty()) break;
                    }

                    if (pak_path.empty()) {
                        spdlog::info("Stage 2 cook->runtime: no cooked .pak found "
                                     "(run tools/assetcook) — skipping live load");
                    } else {
                        schizo::editor::CookedSceneStats st;
                        if (schizo::editor::inspect_cooked_pak(pak_path, st)) {
                            spdlog::info("Stage 2 cook->runtime OK: '{}' mmap-loaded with NO source "
                                         "parse -> {} mesh(es), {} material(s), {} node(s), {} verts; "
                                         "first mesh: LOD0 {} idx, {} LOD tiers, {} meshlets; "
                                         "albedo tex linked {}/{} resolved-by-GUID",
                                         pak_path, st.mesh_count, st.material_count, st.node_count,
                                         st.total_vertices, st.lod0_indices, st.lod_tiers, st.meshlets,
                                         st.resolved_textures, st.linked_textures);

                            // Spawn a visible entity referencing the bundle. The
                            // AssetMeshCache loads the .pak through the same path
                            // as any mesh, so this renders without special-casing.
                            auto ent = scene->CreateEntity("CookedMesh (Stage 2 .pak)");
                            ent->SetMesh(pak_path);
                            const glm::vec3 ext    = st.bounds_max - st.bounds_min;
                            const glm::vec3 center = 0.5f * (st.bounds_min + st.bounds_max);
                            const float maxext = std::max({ ext.x, ext.y, ext.z, 1e-3f });
                            const float s = (maxext > 6.0f) ? (4.0f / maxext) : 1.0f;
                            ent->GetTransform()->SetLocalScale(glm::vec3(s));
                            ent->GetTransform()->SetLocalPosition(
                                glm::vec3(-4.0f, 1.0f, 0.0f) - center * s);
                            spdlog::info("Stage 2 cook->runtime: spawned '{}' from cooked bundle "
                                         "(scale {:.3f}) — visible in viewport at x=-4", ent->GetName(), s);
                        } else {
                            spdlog::warn("Stage 2 cook->runtime: '{}' is not a loadable scene bundle",
                                         pak_path);
                        }
                    }
                }

                // ----------------------------------------------------------------
                // Stage 2.3 stream-ready check: if a cooked virtual texture (.vt)
                // exists, memory-map it and page ONE tile zero-copy — proving the
                // tiled, page-aligned format streams with no parse (the Stage-8
                // virtual-texture streamer builds on exactly this).
                // ----------------------------------------------------------------
                {
                    namespace fs = std::filesystem;
                    std::string vt_path;
                    const char* vdirs[] = { "cooked", "bin/cooked", "../cooked", "assets/cooked" };
                    for (const char* d : vdirs) {
                        std::error_code ec;
                        if (!fs::is_directory(d, ec)) continue;
                        for (const auto& e : fs::directory_iterator(d, ec)) {
                            if (!e.is_regular_file()) continue;
                            const std::string p = e.path().string();
                            if (p.size() >= 3 && p.substr(p.size() - 3) == ".vt") { vt_path = p; break; }
                        }
                        if (!vt_path.empty()) break;
                    }
                    if (!vt_path.empty()) {
                        schizo::assets::MappedFile vf;
                        schizo::assets::CookedVTView vv;
                        if (vf.open(vt_path) && vv.open(vf.data(), vf.size())) {
                            uint32_t tsz = 0;
                            const uint8_t* tile0 = vv.tile(0, 0, 0, tsz);
                            const bool aligned = tile0 &&
                                (vv.tiles[0].offset % vv.header->page_align == 0);
                            spdlog::info("Stage 2.3 virtual texture: mmap'd '{}' -> {}x{}, {} mips, "
                                         "{} {}-px tiles; paged tile(0,0,0) = {} B zero-copy, "
                                         "page-aligned {}",
                                         vt_path, vv.header->width, vv.header->height,
                                         vv.header->mip_count, vv.header->tile_count,
                                         vv.header->tile_texels, tsz, aligned ? "OK" : "FAIL");
                        }
                    }
                }
            }
        }

        // ----------------------------------------------------------------
        // Engine foundation online (Master Plan Stage 0/1)
        // ----------------------------------------------------------------
        // Work-stealing job system (idle workers park, so no CPU spin).
        gws::jobs::JobSystem::instance().init();
        const unsigned kJobWorkers = gws::jobs::JobSystem::instance().worker_count();
        // Register the core POD components with reflection (so they're
        // serialisable / inspectable as the ECS migration proceeds).
        schizo::ecs::register_core_components();
        // Per-frame transient allocator: double-buffered, one arena PER
        // worker (lock-free). reset() at the top of every frame.
        gws::memory::FrameAllocator frame_allocator(8u * 1024u * 1024u, kJobWorkers);
        spdlog::info("Engine foundation online: {} job workers, frame allocator {} MiB x2 per worker",
                     kJobWorkers, 8);
        {
            // One-time live self-check that the parallel path works end-to-end
            // in the real build (not just standalone tests).
            constexpr size_t kSelfCheckN = 100000;
            std::atomic<long long> sum{0};
            gws::jobs::JobSystem::instance().parallel_for(0, kSelfCheckN, [&](size_t i) {
                sum.fetch_add(static_cast<long long>(i), std::memory_order_relaxed);
            });
            const long long expected =
                static_cast<long long>(kSelfCheckN - 1) * kSelfCheckN / 2;
            spdlog::info("parallel_for self-check: sum={} expected={} -> {}",
                         sum.load(), expected, sum.load() == expected ? "OK" : "FAIL");
        }

        // First OOP->ECS migration step (Stage 1.4): a non-authoritative shadow
        // world, refreshed from the scene each frame, that runs the
        // transform -> LocalToWorld system on the job system. Rendering still
        // reads the OOP scene, so this can't change what's drawn — it proves
        // the ECS data path runs live on real scene data.
        schizo::editor::EcsSceneBridge ecs_bridge;
        bool ecs_shadow_logged = false;
        bool ecs_persist_logged = false;
        bool ecs_snapshot_logged = false;

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

        // Variable timestep clock. Previously the loop passed a constant
        // 0.016 to Update() and Step(), so anything above 60 fps real played
        // back proportionally faster (e.g. at 144 fps, sim runs 2.4× too
        // fast). Cap dt to keep one stutter frame from launching dynamic
        // bodies through static colliders.
        double last_frame_wall = glfwGetTime();
        constexpr float kMaxFrameDt = 1.0f / 20.0f;  // 50 ms ceiling

        while (!glfwWindowShouldClose(glfw_window)) {
            glfwPollEvents();

            // Recycle the per-frame transient allocator (double-buffered, so
            // last frame's results survive into this frame's first reads).
            frame_allocator.begin_frame();
            GWS_PROFILE_FRAME_BEGIN();

            double now_wall = glfwGetTime();
            float delta_time = static_cast<float>(now_wall - last_frame_wall);
            last_frame_wall = now_wall;
            if (delta_time > kMaxFrameDt) delta_time = kMaxFrameDt;
            if (delta_time < 0.0f) delta_time = 0.0f;

            // Feed dt to the post-processing chain so auto-exposure can
            // do frame-rate-independent smoothing.
            if (post_processing) {
                post_processing->set_delta_time(delta_time);
            }

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

            if (editor_state.scene_playback_manager &&
                editor_state.scene_playback_manager->IsPlaying())
                editor_state.scene_playback_manager->Update(delta_time);
            else {
                // In edit mode, still need to update the scene so transforms are recalculated
                // This ensures the hierarchy MarkDirty cascade works in the editor
                auto scene = editor_state.editor_scene->GetScene();
                if (scene) {
                    scene->Update(delta_time);
                }
            }

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

            ShowMainMenuBar(editor_state, glfw_window);
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

            // Post-processing controls — inline here because post_processing
            // lives in main()'s scope (the free Show* helpers only get
            // EditorState). Toggles + live sliders for every effect.
            if (post_processing) {
                if (ImGui::Begin("Post-Processing")) {
                    bool bloom = post_processing->is_effect_enabled(
                        gws::renderer::gpu::PostProcessEffect::Bloom);
                    if (ImGui::Checkbox("Bloom", &bloom))
                        post_processing->set_effect_enabled(
                            gws::renderer::gpu::PostProcessEffect::Bloom, bloom);

                    bool fxaa = post_processing->is_fxaa_enabled();
                    if (ImGui::Checkbox("FXAA (anti-aliasing)", &fxaa))
                        post_processing->set_fxaa_enabled(fxaa);

                    bool ae = post_processing->is_auto_exposure_enabled();
                    if (ImGui::Checkbox("Auto-exposure", &ae))
                        post_processing->set_auto_exposure_enabled(ae);

                    // Ambient-occlusion technique selector.
                    if (ssao) {
                        ImGui::Separator();
                        ImGui::TextUnformatted("Ambient Occlusion");
                        const char* ao_items[] = { "SSAO", "HBAO", "HDAO",
                                                   "GTAO", "VXAO", "RT" };
                        int cur = static_cast<int>(ssao->technique());
                        if (ImGui::Combo("Technique##ao", &cur, ao_items, 6)) {
                            auto newt = static_cast<gws::renderer::gpu::AoTechnique>(cur);
                            ssao->set_technique(newt); // waits for device idle
                            // Rebind which occlusion texture the lighting pass
                            // samples: VXAO has its own output image, all the
                            // others share the SSAO pass's blurred output.
                            if (newt == gws::renderer::gpu::AoTechnique::VXAO && vxao)
                                lighting->set_ssao_texture(vxao->get_output_view(),
                                                           vxao->get_output_sampler());
                            else
                                lighting->set_ssao_texture(ssao->get_output_view(),
                                                           ssao->get_output_sampler());
                        }
                        if (!ssao->rt_available())
                            ImGui::TextDisabled("(RT unavailable on this GPU)");
                    }

                    ImGui::Separator();
                    ImGui::TextUnformatted("Color FX");

                    bool chroma = post_processing->is_chromatic_enabled();
                    if (ImGui::Checkbox("Chromatic aberration", &chroma))
                        post_processing->set_chromatic(
                            chroma, post_processing->chromatic_intensity_ref());
                    if (chroma)
                        ImGui::SliderFloat("  Chroma amount",
                            &post_processing->chromatic_intensity_ref(), 0.0f, 0.05f, "%.4f");

                    bool vig = post_processing->is_vignette_enabled();
                    if (ImGui::Checkbox("Vignette", &vig))
                        post_processing->set_vignette(
                            vig, post_processing->vignette_intensity_ref(),
                            post_processing->vignette_radius_ref());
                    if (vig) {
                        ImGui::SliderFloat("  Vignette strength",
                            &post_processing->vignette_intensity_ref(), 0.0f, 1.0f);
                        ImGui::SliderFloat("  Vignette radius",
                            &post_processing->vignette_radius_ref(), 0.2f, 1.2f);
                    }

                    bool grain = post_processing->is_film_grain_enabled();
                    if (ImGui::Checkbox("Film grain", &grain))
                        post_processing->set_film_grain(
                            grain, post_processing->film_grain_intensity_ref());
                    if (grain)
                        ImGui::SliderFloat("  Grain amount",
                            &post_processing->film_grain_intensity_ref(), 0.0f, 0.3f);

                    bool sharpen = post_processing->is_sharpen_enabled();
                    if (ImGui::Checkbox("Sharpen", &sharpen))
                        post_processing->set_sharpen(sharpen);
                    if (sharpen)
                        ImGui::SliderFloat("  Sharpen amount",
                            &post_processing->sharpen_intensity_ref(), 0.0f, 2.0f);

                    bool lens = post_processing->is_lens_distortion_enabled();
                    if (ImGui::Checkbox("Lens distortion", &lens))
                        post_processing->set_lens_distortion(lens);
                    if (lens)
                        ImGui::SliderFloat("  Distort (barrel/pincushion)",
                            &post_processing->lens_distortion_ref(), -0.5f, 0.5f);

                    ImGui::Separator();
                    ImGui::TextUnformatted("Color Grade");
                    bool grade = post_processing->is_color_grade_enabled();
                    if (ImGui::Checkbox("Enable grading", &grade))
                        post_processing->set_color_grade(grade);
                    if (grade) {
                        ImGui::SliderFloat("  Temperature", &post_processing->cg_temperature_ref(), -1.0f, 1.0f);
                        ImGui::SliderFloat("  Tint",        &post_processing->cg_tint_ref(),        -1.0f, 1.0f);
                        ImGui::SliderFloat("  Saturation",  &post_processing->cg_saturation_ref(),   0.0f, 2.0f);
                        ImGui::SliderFloat("  Contrast",    &post_processing->cg_contrast_ref(),     0.5f, 2.0f);
                        ImGui::SliderFloat("  Brightness",  &post_processing->cg_brightness_ref(),   0.5f, 2.0f);
                    }

                    ImGui::Separator();
                    ImGui::TextUnformatted("Stylized");
                    bool poster = post_processing->is_posterize_enabled();
                    if (ImGui::Checkbox("Posterize", &poster))
                        post_processing->set_posterize(poster);
                    if (poster)
                        ImGui::SliderFloat("  Levels",
                            &post_processing->posterize_levels_ref(), 2.0f, 32.0f);

                    bool pix = post_processing->is_pixelate_enabled();
                    if (ImGui::Checkbox("Pixelate", &pix))
                        post_processing->set_pixelate(pix);
                    if (pix)
                        ImGui::SliderFloat("  Block size (px)",
                            &post_processing->pixelate_size_ref(), 1.0f, 32.0f);

                    bool scan = post_processing->is_scanlines_enabled();
                    if (ImGui::Checkbox("Scanlines (CRT)", &scan))
                        post_processing->set_scanlines(scan);
                    if (scan)
                        ImGui::SliderFloat("  Scanline strength",
                            &post_processing->scanline_intensity_ref(), 0.0f, 1.0f);
                }
                ImGui::End();
            }

            // ------------------------------------------------------------
            // GPU frame
            // ------------------------------------------------------------
            vkWaitForFences(device.get_device(), 1, &frame_fences[current_frame],
                            VK_TRUE, UINT64_MAX);
            // The previous frame's occlusion queries are guaranteed available
            // after its fence. Pull the results into the culler so this
            // frame can skip occluded draws.
            if (graph) graph->resolve_occlusion_queries();
            // Same story for the HZB readback — the GPU finished copying the
            // HZB mip to the host buffer before signalling this fence.
            if (hzb_culler) hzb_culler->pull_readback();
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

                        // If the playback camera entity (or any ancestor / direct
                        // child) carries a CameraComponent, its FOV / clip
                        // planes / projection drive the matrix. Otherwise fall
                        // back to a sensible 45° perspective.
                        auto find_cam_comp = [](schizo::scene::Entity* e)
                                -> std::shared_ptr<schizo::scene::CameraComponent> {
                            if (!e) return nullptr;
                            if (auto c = e->GetComponent<schizo::scene::CameraComponent>()) return c;
                            // Walk up parents (one level — costly to walk deep
                            // and rigs typically put the component on the camera
                            // entity itself or on a single parent).
                            if (auto p = e->GetParent()) {
                                if (auto c = p->GetComponent<schizo::scene::CameraComponent>()) return c;
                            }
                            return nullptr;
                        };
                        if (auto cc = find_cam_comp(playback_camera)) {
                            cam.proj = cc->GetProjectionMatrix(aspect);
                        } else {
                            cam.proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 1000.0f);
                        }
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
            // The lighting pass now needs view+proj so the in-shader sky
            // branch can reconstruct world-space view rays at empty pixels.
            lighting->set_view_projection(cam.view, cam.proj);
            lighting->set_camera_position(cam.position);

            // Sync scene LightComponents into the lighting pass. Clear the
            // existing list and rebuild it from whatever LightComponents
            // are currently attached to active entities. If no Directional
            // LightComponent is present, inject a default sun so empty
            // scenes aren't pitch black.
            //
            // For the (first) shadow-casting directional light we also
            // build a light-space view-projection matrix so:
            //   1) the Shadow stage renders casters from the sun's POV,
            //   2) the lighting shader can project worldPos to light space
            //      and PCF-sample the shadow map.
            // Scene-bounds approximation: an ortho box of ±kShadowExtent
            // around the world origin, depth [0, kShadowFar]. Good enough
            // for now; cascades / per-frame fitting come later.
            glm::mat4 shadow_view_proj{1.0f};
            glm::vec3 shadow_light_dir{0.3f, -1.0f, 0.2f};
            bool      shadow_active = false;
            // Primary directional light captured for the RT reflection
            // re-shade (sun direction + colour×intensity). Defaults match
            // the fallback sun injected when the scene has no directional.
            glm::vec3 sun_dir_for_rt{0.3f, -1.0f, 0.2f};
            glm::vec3 sun_color_for_rt{1.0f, 0.95f, 0.85f};
            sun_color_for_rt *= 1.5f;
            {
                // Camera-anchored ortho frustum. The shadow box follows
                // the camera around so shadows exist where the player is
                // looking, not just near the world origin. Anchor point
                // is the camera position; eye is offset by -sun_dir
                // along the depth axis.
                constexpr float kShadowExtent = 30.0f;
                constexpr float kShadowNear   = 0.1f;
                constexpr float kShadowFar    = 150.0f;
                const glm::vec3 anchor = cam.position;

                lighting->clear_lights();
                bool has_directional = false;
                const auto& scene_entities = editor_scene.GetScene()->GetEntities();
                for (const auto& entity : scene_entities) {
                    if (!entity || !entity->IsActiveInHierarchy()) continue;
                    auto lc = entity->GetComponent<schizo::scene::LightComponent>();
                    if (!lc || !lc->IsEnabled()) continue;
                    const glm::vec3 color = lc->GetColor();
                    const float     intensity = lc->GetIntensity();
                    const bool      shadow = lc->GetCastShadow();
                    switch (lc->GetType()) {
                        case schizo::scene::LightType::Directional: {
                            const glm::vec3 dir = lc->GetDirection();
                            // First directional drives the RT reflection sun.
                            if (!has_directional) {
                                sun_dir_for_rt   = dir;
                                sun_color_for_rt = color * intensity;
                            }
                            glm::mat4 light_vp{1.0f};
                            if (shadow && !shadow_active) {
                                const glm::vec3 nd = glm::normalize(
                                    glm::length(dir) > 0.0f ? dir : glm::vec3(0,-1,0));
                                const glm::vec3 light_pos =
                                    anchor - nd * (kShadowFar * 0.5f);
                                const glm::vec3 up =
                                    (std::abs(nd.y) > 0.99f)
                                        ? glm::vec3(0.0f, 0.0f, 1.0f)
                                        : glm::vec3(0.0f, 1.0f, 0.0f);
                                const glm::mat4 light_view =
                                    glm::lookAt(light_pos, anchor, up);
                                const glm::mat4 light_proj =
                                    glm::orthoZO(-kShadowExtent, kShadowExtent,
                                                  -kShadowExtent, kShadowExtent,
                                                  kShadowNear, kShadowFar);
                                light_vp = light_proj * light_view;
                                shadow_view_proj = light_vp;
                                shadow_light_dir = nd;
                                shadow_active = true;
                            }
                            lighting->add_directional_light(dir, color, intensity,
                                                            shadow, light_vp);
                            has_directional = true;
                            break;
                        }
                        case schizo::scene::LightType::Point: {
                            lighting->add_point_light(lc->GetPosition(), color, intensity,
                                                      lc->GetRange(), shadow);
                            break;
                        }
                        case schizo::scene::LightType::Spot: {
                            const glm::vec2 angles_deg = lc->GetSpotAngles(); // (inner, outer)
                            const float outer_cos = glm::cos(glm::radians(angles_deg.y));
                            lighting->add_spot_light(lc->GetPosition(), lc->GetDirection(),
                                                     color, intensity, lc->GetRange(),
                                                     outer_cos, shadow);
                            break;
                        }
                        default: break;
                    }
                }
                if (!has_directional) {
                    // Default sun — only injected when the scene has none.
                    // Place a Directional LightComponent on an entity (with
                    // GetCastShadow=true) to drive actual shadows.
                    lighting->add_directional_light(glm::vec3(0.3f, -1.0f, 0.2f),
                                                    glm::vec3(1.0f, 0.95f, 0.85f),
                                                    1.5f, /*shadow=*/false);
                }
            }

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
            
            std::vector<gws::renderer::gpu::DrawItem> opaque_draws, transparent_draws;
            // Run the ECS transform system FIRST so its world matrices are
            // ready, then feed them to the draw-list builder (authoritative:
            // build_draw_items reads ECS LocalToWorld, OOP matrix is fallback).
            { GWS_PROFILE_ZONE("ecs_sync");
              ecs_bridge.sync_and_run(editor_scene.GetScene()); }

            { GWS_PROFILE_ZONE("build_draw_items");
              schizo::editor::build_draw_items(
                  editor_scene.GetScene(), prim_cache, mat_cache, asset_cache,
                  &device, mat_layout, mat_pool,
                  opaque_draws, transparent_draws, &ecs_bridge); }

            if (!ecs_shadow_logged && ecs_bridge.entity_count() > 0) {
                spdlog::info("ECS shadow live: {} entities synced, {} LocalToWorld "
                             "computed (parallel TRS + hierarchy), {}/{} matrices "
                             "match OOP world -> {}",
                             ecs_bridge.entity_count(), ecs_bridge.matrices_written(),
                             ecs_bridge.verified(), ecs_bridge.checked(),
                             (ecs_bridge.verified() == ecs_bridge.checked())
                                 ? "OK" : "MISMATCH");
                ecs_shadow_logged = true;
            }
            if (!ecs_persist_logged && ecs_bridge.reused() > 0) {
                spdlog::info("ECS persistent handles: {} reused, {} created, {} "
                             "destroyed this frame -> stable identity across frames",
                             ecs_bridge.reused(), ecs_bridge.created(),
                             ecs_bridge.destroyed());
                ecs_persist_logged = true;
            }
            if (!ecs_snapshot_logged && ecs_bridge.entity_count() > 0) {
                size_t snap_bytes = 0, reloaded = 0;
                const bool snap_ok = ecs_bridge.snapshot_selfcheck(snap_bytes, reloaded);
                spdlog::info("ECS snapshot self-check: {} entities -> {} bytes -> "
                             "reloaded {} -> {}",
                             ecs_bridge.entity_count(), snap_bytes, reloaded,
                             snap_ok ? "OK" : "FAIL");

                // Stage 1.4 draw submission: real scene drawables from the ECS,
                // plus the 100k single-parallel-pass acceptance benchmark.
                double scene_ms = 0.0, bench_ms = 0.0;
                const size_t scene_draws = ecs_bridge.collect_scene_draws(scene_ms);
                const size_t bench_draws =
                    schizo::editor::EcsSceneBridge::draw_benchmark(100000, bench_ms);
#ifdef NDEBUG
                const char* bench_verdict =
                    (bench_draws == 100000 && bench_ms < 16.0) ? "OK" : "SLOW";
#else
                const char* bench_verdict =
                    (bench_draws == 100000) ? "OK (unoptimized build; ~3.7ms at -O2)"
                                            : "FAIL";
#endif
                spdlog::info("ECS draw submission: scene {} draws ({:.3f} ms); "
                             "benchmark {} entities -> {} draws in one parallel "
                             "pass ({:.3f} ms) -> {}",
                             scene_draws, scene_ms, 100000, bench_draws, bench_ms,
                             bench_verdict);
                ecs_snapshot_logged = true;
            }

            // (WBOIT is order-independent — no need to back-to-front sort the
            // transparent list. The depth-weighted compositing in the shader
            // handles inter-fragment ordering even for intersecting meshes.)

            graph->set_draw_items(opaque_draws);

            // HZB occlusion test for the opaque draw list — projects each
            // entity's world-space AABB into NDC and compares against the
            // CPU-side HZB from the previous frame. Draws marked occluded
            // are skipped by VulkanGBuffer::draw_items.
            if (hzb_culler) {
                GWS_PROFILE_ZONE("hzb_cull");
                // Per-draw world-space AABB build — embarrassingly parallel
                // (each task writes only its own index), so it runs on the
                // job system. The scratch arrays come from the per-frame
                // FrameAllocator (transient, reset next frame) — this is the
                // first real allocation served by it live, alongside the
                // first per-frame parallel workload.
                const size_t draw_n = opaque_draws.size();
                glm::vec3* aabb_mins = draw_n ? frame_allocator.alloc_array<glm::vec3>(draw_n) : nullptr;
                glm::vec3* aabb_maxs = draw_n ? frame_allocator.alloc_array<glm::vec3>(draw_n) : nullptr;
                if (draw_n && aabb_mins && aabb_maxs)
                gws::jobs::JobSystem::instance().parallel_for(0, draw_n, [&](size_t i) {
                    const auto& d = opaque_draws[i];
                    if (!d.mesh) {
                        aabb_mins[i] = glm::vec3(0.0f);
                        aabb_maxs[i] = glm::vec3(0.0f);
                        return;
                    }
                    const auto& local = d.mesh->bounding_box();
                    glm::vec3 wmin( std::numeric_limits<float>::infinity());
                    glm::vec3 wmax(-std::numeric_limits<float>::infinity());
                    for (int c = 0; c < 8; ++c) {
                        glm::vec4 corner(
                            (c & 1) ? local.max.x : local.min.x,
                            (c & 2) ? local.max.y : local.min.y,
                            (c & 4) ? local.max.z : local.min.z,
                            1.0f);
                        glm::vec3 w = glm::vec3(d.model * corner);
                        wmin = glm::min(wmin, w);
                        wmax = glm::max(wmax, w);
                    }
                    aabb_mins[i] = wmin;
                    aabb_maxs[i] = wmax;
                });
                if (draw_n && aabb_mins && aabb_maxs)
                    hzb_culler->test_visibility(aabb_mins, aabb_maxs, draw_n,
                                                cam.proj * cam.view);
            }

            // Sync the transparent pass's view of the dynamic light list each
            // frame so newly-added lights or removed lights take effect.
            if (transparent) {
                transparent->set_light_count(lighting->get_light_count());
            }

            // Combined caster list for the shadow stage: opaque + transparent.
            // Blend materials cast binarised shadows (cutoff=0.5 in their
            // emissive_factor.a). Cutout materials use their per-material
            // cutoff. Opaque materials' cutoff is 0 → no discard branch.
            std::vector<gws::renderer::gpu::DrawItem> shadow_draws;
            shadow_draws.reserve(opaque_draws.size() + transparent_draws.size());
            shadow_draws.insert(shadow_draws.end(), opaque_draws.begin(), opaque_draws.end());
            shadow_draws.insert(shadow_draws.end(), transparent_draws.begin(), transparent_draws.end());

            graph->begin_frame(cmd);

            // RT scene update — build BLAS for any newly-seen meshes and
            // rebuild the TLAS with this frame's draw list. Has to happen
            // before any pass that wants to ray-query the TLAS, which
            // (starting in Phase 3) will be the Lighting stage. Phase 2
            // runs this for its own sake — verifies the AS builds work and
            // produces a log line per frame.
            if (rt_scene) {
                // NOTE: meshes are owned by the persistent prim_cache /
                // asset_cache (NOT the scene), so switching scenes does
                // NOT invalidate the BLAS cache — the Mesh* keys stay
                // alive. `ensure_blas` additionally validates each cached
                // entry by source-VBO handle, so even a reused Mesh*
                // address rebuilds correctly. We therefore do NOT clear
                // the RT scene on scene switch (doing so previously left
                // the lighting descriptor pointing at a destroyed TLAS,
                // which hung the editor on scene load).
                rt_scene->update(cmd, opaque_draws.data(), opaque_draws.size());
                // Re-bind the TLAS handle. rebuild_tlas() always produces
                // a valid handle (empty TLAS for empty scenes), so this is
                // never null and the descriptor never dangles.
                lighting->set_tlas(rt_scene->get_tlas_handle());
                if (ssao && ssao->uses_rt())
                    ssao->set_tlas(rt_scene->get_tlas_handle());
                if (ssr && ssr->uses_rt()) {
                    ssr->set_tlas(rt_scene->get_tlas_handle());
                    ssr->set_instance_data_buffer(rt_scene->get_instance_data_buffer());
                    // Feed the RT reflection re-shade the same sun + ambient
                    // the deferred lighting uses, so reflected geometry is
                    // lit consistently with what's directly visible.
                    ssr->set_sun(sun_dir_for_rt, sun_color_for_rt);
                    ssr->set_ambient(l_cfg.ambient_color, l_cfg.global_ambient);
                }
                static int rt_log_throttle = 0;
                if ((rt_log_throttle++ % 120) == 0) {
                    spdlog::info("RT scene update: {} instances",
                                 rt_scene->get_instance_count());
                }
            }

            // Shadow stage: render casters from the directional light's
            // POV using the matrix we just built in the scene sync. If no
            // shadow-casting directional light exists we still run the
            // stage so the shadow map clears, but with the camera matrix
            // (acts as a no-op the lighting shader ignores because the
            // sun's `castsShadow` flag will be 0).
            // The shadow stage just needs view+proj that, when multiplied,
            // equal shadow_view_proj. Easiest: pass identity view and
            // shadow_view_proj as proj — the shadow_map's caster shader
            // only uses `proj * view * model`, so the split doesn't matter.
            const glm::mat4 sh_view = glm::mat4(1.0f);
            const glm::mat4 sh_proj = shadow_active
                ? shadow_view_proj
                : graph->get_camera().proj;
            graph->execute_stage(cmd, RenderGraphStage::Shadow,
                [&](VkCommandBuffer rec_cmd) {
                    uint32_t calls = 0, tris = 0;
                    shadow_map->draw_items(rec_cmd,
                                           sh_view,
                                           sh_proj,
                                           graph->get_camera().position,
                                           shadow_draws.data(),
                                           shadow_draws.size(),
                                           &calls, &tris);
                });
            graph->execute_stage(cmd, RenderGraphStage::Geometry,   {});
            // SSAO compute dispatch — runs after the G-Buffer is populated
            // and before lighting samples the occlusion texture. Outside
            // the render graph because it's a compute pass and the graph
            // currently only models render-pass stages.
            if (ssao) {
                if (ssao->technique() == gws::renderer::gpu::AoTechnique::VXAO && vxao) {
                    // VXAO: voxelize this frame's opaque draws, then cone-trace.
                    vxao->voxelize(cmd, opaque_draws.data(), opaque_draws.size(),
                                   cam.position);
                    vxao->compute_ao(cmd);
                } else {
                    ssao->execute(cmd, cam.view, cam.proj);
                }
            }
            graph->execute_stage(cmd, RenderGraphStage::Lighting,   {});
            // SSR — compute reflections + composite into HDR before
            // transparent fragments are drawn. Runs outside the render
            // graph because the graph models only render-pass stages and
            // SSR is a compute + render-pass pair owned by its own class.
            if (ssr) {
                ssr->execute(cmd, cam.view, cam.proj, cam.position);
            }
            // Transparent stage uses a custom recorder so we can feed it
            // the back-to-front-sorted transparent draw list directly,
            // independent of the graph's stored opaque draw_items_.
            graph->execute_stage(cmd, RenderGraphStage::Transparent,
                [&](VkCommandBuffer rec_cmd) {
                    transparent->execute(rec_cmd, transparent_draws, graph->get_camera());
                });
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

            // Collect this frame's CPU zones and report a breakdown
            // periodically (the full flame-graph UI is Stage 14).
            GWS_PROFILE_FRAME_END();
#if GWS_PROFILE_ENABLED
            if (frame_count % 240 == 0)
                spdlog::info("[profiler] {}",
                             gws::profile::Profiler::instance().format_report());
#endif
        }

        spdlog::info("Editor closed after {} frames", frame_count);

        // ----------------------------------------------------------------
        // Cleanup
        // ----------------------------------------------------------------
        // Stop the job workers before tearing down (joins all threads).
        gws::jobs::JobSystem::instance().shutdown();
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
