// Project Schizo Editor - Main entry point
// Phase 6: Vulkan renderer backend

#define GLM_ENABLE_EXPERIMENTAL
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

// gws Vulkan renderer
#include "vulkan/vulkan_device.h"
#include "vulkan/vulkan_shader_registry.h"   // --startup-probe cost attribution
#include "vulkan/vulkan_swapchain.h"
#include "vulkan/vulkan_g_buffer.h"
#include "vulkan/vulkan_lighting_pass.h"
#include "vulkan/culling.h"   // Frustum::is_sphere_visible — per-light culling
#include "vulkan/vulkan_texture.h"  // Texture::create_from_file — spot cookies
#include "vulkan/vulkan_shadow_map.h"
#include "vulkan/vulkan_post_processing.h"
#include "vulkan/vulkan_transparent_pass.h"
#include "vulkan/vulkan_environment_map.h"
#include "vulkan/vulkan_water_pass.h"
#include "vulkan/vulkan_froxel_fog_pass.h"
#include "vulkan/vulkan_ddgi_pass.h"
#include "vulkan/vulkan_ssao_pass.h"
#include "vulkan/vulkan_ssr_pass.h"
#include "vulkan/vulkan_volumetric_light_pass.h"
#include "vulkan/vulkan_cloud_pass.h"
#include "vulkan/vulkan_rt_scene.h"
#include "vulkan/vulkan_vxao_pass.h"
#include "vulkan/vulkan_occlusion_culler.h"
#include "vulkan/vulkan_hzb_culler.h"
#include "vulkan/vulkan_render_graph.h"
#include "vulkan/vulkan_scene_mesh.h"
#include "vulkan/vulkan_scene_material.h"
#include "vulkan/vulkan_texture_manager.h"  // runtime texture handling (Stage 2)
#include "imported_skinned_actor.h"          // rigged-glTF import → GPU skin (Path B)
#include "skinned_demo.h"                    // Stage 5 skinned-animation test rig
#include "skinned_actor_cache.h"             // per-entity skinned actors (3.8)
#include "nav_bake.h"                        // navmesh from real scene geometry (3.4)
#include "world_streaming.h"                 // streaming + floating origin on the live scene (3.3)
#include "particle_emitter_cache.h"          // particle sim on real entities (3.9)
#include "npc_agents.h"                      // perception -> BT -> navmesh movement (3.5)
#include "locomotion_clip.h"                 // idle/walk from actual motion (3.8)
#include "game_ui_demo.h"                     // runtime game-UI HUD demo (Game-UI pillar)
#include "vulkan/imgui_vulkan.h"

// Engine foundation (Master Plan Stage 0/1): job system, frame allocator,
// reflected ECS components. (Component registration only needs reflection +
// glm; the EnTT World is not pulled into this TU yet — the OOP-scene → ECS
// migration is a separate pass.)
#include "jobs/job_system.h"
#include "memory/memory.h"
#include "profiler/profiler.h" // scoped CPU zones, per-thread (Stage 0.6)
#include "profiler/frame_capture.h" // N5: one-frame draw-list snapshot
#include "memory/memory_snapshot.h" // N3: per-tag live bytes + allocator registry
#include "audio/audio_engine.h"     // Stage 6: miniaudio device + spatial mixer
#include "physics/jolt_physics.h"   // Stage 6 step 6: raycast for audio occlusion
#include "net_profiler.h"           // N4: network bandwidth / RTT / loss / rollback
#include "vulkan/gpu_profiler.h"    // N2: per-pass GPU timing
#include "ecs/components.h"
#include "ecs_bridge.h"        // shadow ECS mirror of the scene (Stage 1.4 step 1)
#include "particle_emitter_component.h"
#include "npc_agent_component.h"
#include "scene_component_reflect.h"
#include "snapping.h"                         // grid / angle / scale snapping (4.7)
#include "curve_editor.h"                     // curve + gradient widgets (4.5)
#include "command_palette.h"                 // Ctrl+P: one entry point for every action (4.2)
#include "component_inspector.h"  // generic reflection-driven ECS component authoring (F2)

// ImGui headers
#include <imgui.h>
#include <imgui_internal.h>   // DockBuilder API (programmatic default dock layout)
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

// Editor / scene headers
#include "editor_scene.h"
#include "project.h"            // modular project model (manifest + features)
#include "project_launcher.h"  // first-run project launcher UI
#include "project_paths.h"     // per-project content sandbox (working-dir scoping)
#include "scene.h"
#include "entity_factory.h"
#include "transform_component.h"
#include "light_component.h"
#include "camera_component.h"
#include "viewport_camera.h"
#include "mesh_renderer_component.h"
#include "collider_component.h"
#include "audio_components.h"
#include "asset_browser_panel.h"
#include "logic_graph_panel.h"   // visual node editor for the scene logic graph
#include "material_editor_panel.h"
#include "asset_import_dialog.h"
#include "transform_gizmo.h"
#include "undo_redo_manager.h"
#include "asset_manager.h"
#include "scene_playback_manager.h"
#include "play_mode_changes.h"
#include "jobs/task_runner.h"
#include "primitive_meshes.h"
#include "scene_render_bridge.h"
#include "character_controller_panel.h"
#include "ability_system_panel.h"
#include "network_system_panel.h"
#include "net_session.h"
#include "script_system.h"       // Stage 12: custom scripts (Python/C++/C#)
#include "script_api_editor.h"
#include "script_component.h"
#include "script_params.h"       // Stage 12: script public-field declarations
#include "diagnostics/crash_handler.h"   // crash reports + persistent/ring logging

#ifndef GWS_ENGINE_VERSION
#define GWS_ENGINE_VERSION "dev"
#endif
#include "water_component.h"     // terrain expansion: water surfaces
#include "terminal_panel.h"
#include "console_panel.h"
#include "editor_audio_driver.h"

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
#include <fstream>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cfloat>
#include <cmath>
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
    bool show_logic_graph = false;   // scene logic-graph node editor
    bool show_viewport = true;
    bool show_demo_window = false;
    bool show_preferences = false;
    bool show_post_processing = true;   // closable Post-Processing dock panel
    bool show_terminal = true;          // embedded OS shell terminal panel
    bool show_output = true;            // editor log output console panel

    // Transient status line shown over the viewport (mesh apply/import result etc).
    std::string status_message;
    double      status_message_time = 0.0;   // ImGui::GetTime() when last set
    void set_status(const std::string& m) { status_message = m; status_message_time = ImGui::GetTime(); }

    // The persistent ECS bridge (set in main). Lets the inspector author gameplay
    // components on an entity's authoritative ECS entity (F1/F2).
    schizo::editor::EcsSceneBridge* ecs_bridge = nullptr;

    // Terrain sculpting (Phase A). Brush state shared by the Inspector's
    // Terrain section and the viewport sculpt handler.
    bool  terrain_sculpt_active  = false;  // LMB-drag sculpts the selected terrain
    int   terrain_brush_mode     = 0;      // 0=Raise 1=Lower 2=Smooth 3=Flatten
    float terrain_brush_radius   = 6.0f;   // world units
    float terrain_brush_strength = 0.5f;   // per stroke-application
    float terrain_brush_falloff  = 0.6f;   // 0 hard edge .. 1 soft
    glm::vec3 terrain_brush_hit  = glm::vec3(0.0f);  // last sculpt hit (for the ring overlay)
    bool  terrain_brush_valid    = false;  // is terrain_brush_hit current this frame

    // Terrain texture splat painting (Phase C). When paint mode is on, LMB-drag
    // paints the active layer's weight into the splatmap instead of sculpting.
    bool  terrain_paint_active   = false;  // paint splat layers (mutually excl. w/ sculpt)
    int   terrain_paint_layer    = 0;      // active layer 0..3
    float terrain_paint_strength = 0.5f;   // 0..1 per stroke-application

    // When true, the Unity-style dock layout is rebuilt to its default
    // arrangement on the next frame (set by Window > Reset Layout). Also
    // triggers the first-run build when no saved dock layout exists.
    bool request_reset_layout = false;

    // Scene/Entity data
    uint32_t selected_entity_id = 0;  // 0 = no selection

    // Command palette (4.2). The registry is rebuilt once at startup; it
    // holds no scene state, so it does not need refreshing per frame.
    // Gizmo snapping (4.7). Off by default; Ctrl enables it for the
    // duration of a drag, which is the convention every DCC uses and
    // avoids a mode the user can forget they are in.
    schizo::editor::SnapSettings snap;

    schizo::editor::CommandRegistry commands;
    bool show_command_palette = false;

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

    // Embedded OS shell terminal (ConPTY). Lazily created on first render so a
    // shell isn't spawned until the panel is actually shown.
    std::unique_ptr<schizo::editor::TerminalPanel> terminal;

    // "Output" log console (mirrors the editor's own spdlog output). Lazy —
    // the capture sink runs from startup regardless, so no logs are missed.
    std::unique_ptr<schizo::editor::ConsolePanel> console;

    // Material Editor
    std::unique_ptr<schizo::editor::MaterialEditorPanel> material_editor;
    
    // Asset Import Dialog
    std::unique_ptr<schizo::editor::AssetImportDialog> asset_import_dialog;
    
    // Gizmo dragging state
    bool gizmo_dragging = false;
    // Transform captured when a gizmo drag begins, so the whole drag becomes ONE
    // undo entry rather than one per mouse-move frame (same rule as inspector
    // fields — see edit_coalescer.h).
    uint32_t  gizmo_undo_entity = 0;
    glm::vec3 gizmo_undo_position{0.0f};
    glm::quat gizmo_undo_rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 gizmo_undo_scale{1.0f};
    char gizmo_axis = 0;  // 0=none, 'x', 'y', 'z'
    glm::vec2 gizmo_drag_start = glm::vec2(0.0f);
    // Where the selection stood when the drag began. Relative snapping
    // measures from here, so a layout that was never on the grid keeps
    // its position and only its SPACING becomes exact.
    glm::vec3 gizmo_drag_origin = glm::vec3(0.0f);
    glm::vec3 gizmo_drag_offset = glm::vec3(0.0f);
    
    // Undo/Redo
    schizo::editor::UndoRedoManager undo_redo_manager;
    
    // Drag and drop
    std::vector<std::string> dropped_files;
    
    // Scene Playback System
    std::unique_ptr<schizo::editor::ScenePlaybackManager> scene_playback_manager;
    bool show_playback_controls = true;
    bool show_debug_panels = true;
    bool show_performance = true;   // Stage 14 unified profiler overlay
    
    // Debug Panels
    std::unique_ptr<schizo::editor::CharacterControllerPanel> character_panel;
    std::unique_ptr<schizo::editor::AbilitySystemPanel> ability_panel;
    std::unique_ptr<schizo::editor::NetworkSystemPanel> network_panel;
    
    // Placeholder pointers for demo (would come from scene entities)
    engine::character::CharacterController* selected_character_controller = nullptr;
    engine::ability::AbilitySystem* selected_ability_system = nullptr;
    engine::network::NetworkManager* network_manager = nullptr;

    // Multiplayer (Stage 7): live host/join session on the verified net stack.
    // Ticked every frame; replicates scene transforms host->clients.
    schizo::editor::NetSession net_session;
    bool show_network_window = false;
    char net_host_port[16] = "7777";
    char net_join_ip[64]   = "127.0.0.1";
    char net_join_port[16] = "7777";
    int  net_launch_clients = 1;   // PIE launcher: how many client processes to spawn
    bool net_autoplay_started = false;  // did the session auto-enter Play mode?
    bool net_autoplay_failed  = false;  // tried but no 'Player' entity (edit-view fallback)

    // Custom scripts (Stage 12). One persistent ScriptApi table — backends
    // capture its address, so it must live as long as the editor; the ctx
    // pointers + dt/time are refreshed every frame.
    schizo::editor::ScriptSystem    script_system;
    schizo::editor::EditorScriptCtx script_ctx;
    schizo::editor::ScriptApi       script_api;
    double script_play_time = 0.0;   // seconds since Play started

    // ---- Project system (modular features + launcher) ----
    // The launcher shows first; once a project is chosen it loads, `features`
    // is set from its manifest, and the editor proper takes over. Non-launcher
    // runtime modes (net-spawned game windows) default `features` to all-on.
    bool                        in_launcher = true;      // showing the project launcher?
    bool                        project_loaded = false;
    schizo::project::ProjectManifest project;
    schizo::project::FeatureSet  features = schizo::project::FeatureSet::all();
    bool                        show_project_settings = false;

    bool feature_on(schizo::project::Feature f) const { return features.has(f); }

    // Path B: a requested rigged-model import (path set by the File menu,
    // consumed in the loop where the device + material pool are in scope).
    std::string pending_skinned_import;

    // Long work that must not freeze the UI. Completion callbacks are delivered
    // on this thread by tasks.poll(), so a handler may touch the scene and the
    // GPU safely. See jobs/task_runner.h.
    gws::tasks::TaskRunner tasks;
    bool show_task_panel = true;

    // Terrain sculpt undo. A stroke spans many frames, so the heightmap is
    // snapshotted when the mouse goes DOWN and diffed when it comes up — one
    // undo entry per stroke, not one per frame. The full snapshot is transient
    // (one at a time, ~17 KB at the default 64-cell resolution); only the
    // rectangle that actually changed is kept in the undo entry.
    bool                 terrain_stroke_active = false;
    uint32_t             terrain_stroke_entity = 0;
    std::vector<float>   terrain_stroke_heights;   // heightmap at stroke start
    std::vector<uint8_t> terrain_stroke_holes;     // hole mask at stroke start

    // Scene-file hot reload. Watches the CURRENT scene file so a change made
    // outside the editor (a git pull, a teammate, an agent) is noticed. It is
    // deliberately NOT a blind reload -- see ShowSceneReloadPrompt.
    // Navmesh baked from the real scene (3.4). Before this the only navmesh
    // in the editor was a synthetic grid generated inside the animation demo.
    // World streaming + floating origin driven by the viewport camera (3.3).
    // Off by default: streaming deactivates entities, and silently hiding parts
    // of the scene you are editing would be a bug, not a feature.
    schizo::editor::EditorWorldStreaming world_streaming;

    // Particle simulation on emitter entities (3.9). The GPU draw of the
    // billboards it produces is not built yet — see particle_emitter_cache.h.
    schizo::editor::EditorParticleEmitters particles;

    // NPC agents: perception -> behaviour tree -> movement along the baked
    // navmesh (3.5). Only runs during play, so agents do not wander off while
    // the scene is being edited.
    schizo::editor::EditorNpcAgents npc_agents;

    // Per-entity locomotion state for motion-driven clip selection. Keyed by
    // entity so a character that stops animating idle does not affect others.
    std::unordered_map<uint32_t, schizo::editor::LocomotionState> locomotion;

    schizo::ai::NavMesh        scene_navmesh;
    schizo::editor::NavBakeStats nav_stats;

    gws::assets::AssetWatcher scene_watcher;
    std::string  watched_scene_path;      // what scene_watcher is currently on
    bool         watched_scene_dirty = false;   // last known unsaved-changes state
    bool         scene_changed_on_disk = false; // set by the watcher callback
    bool         show_scene_reload_prompt = false;

    // Coalesces inspector field edits so one drag is one undo entry rather than
    // one per frame. See edit_coalescer.h.
    schizo::editor::EditCoalescer field_edits;
    // Separate coalescer: the logic graph is scene-scoped, and sharing one with
    // the inspector would make editing a node mid-drag look like "switched
    // target" and commit the wrong thing.
    schizo::editor::EditCoalescer logic_edits;

    // Play-mode change tracking. Play still discards by default; this keeps the
    // diff so the developer can choose what survives instead of losing a tuning
    // pass on Stop. See play_mode_changes.h.
    schizo::editor::PlayModeChanges  play_changes;
    schizo::editor::PlayChangeReport pending_play_changes;
    bool show_play_changes_popup = false;
};

// ============================================================================
// Play mode entry/exit — one place, so the F5 menu item and the toolbar button
// cannot drift apart (they already had duplicated bodies).
// ============================================================================
static void BeginPlayMode(EditorState& st, const std::shared_ptr<schizo::scene::Scene>& scene) {
    if (!st.scene_playback_manager || !scene) return;
    st.play_changes.Capture(scene, st.ecs_bridge);        // baseline BEFORE play mutates anything
    if (!st.scene_playback_manager->StartPlayback(scene)) {
        spdlog::warn("Failed to start scene playback (no entity named 'Player'?)");
        st.play_changes.Clear();
    }
}

static void EndPlayMode(EditorState& st, const std::shared_ptr<schizo::scene::Scene>& scene) {
    if (!st.scene_playback_manager) return;
    // Diff BEFORE stopping: StopPlayback restores the authored transforms, so
    // after it runs the play-end values are gone.
    if (st.play_changes.has_baseline() && scene)
        st.pending_play_changes = st.play_changes.Diff(scene, st.ecs_bridge);
    else
        st.pending_play_changes = {};

    st.scene_playback_manager->StopPlayback();
    st.play_changes.Clear();
    st.show_play_changes_popup = !st.pending_play_changes.empty();
}

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
static std::string OpenAudioDialogNative() {
    char buf[MAX_PATH] = {0};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = NULL;   // inspector has no window handle; unowned is fine
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = sizeof(buf);
    ofn.lpstrFilter = "Audio (*.wav;*.mp3;*.flac;*.ogg)\0*.wav;*.mp3;*.flac;*.ogg\0All Files\0*.*\0";
    ofn.lpstrTitle  = "Select Audio Clip";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn) ? std::string(buf) : std::string();
}

static std::string OpenModelDialogNative(GLFWwindow* window) {
    char buf[MAX_PATH] = {0};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = window ? glfwGetWin32Window(window) : NULL;
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = sizeof(buf);
    ofn.lpstrFilter = "Rigged Model (*.gltf;*.glb)\0*.gltf;*.glb\0All Files\0*.*\0";
    ofn.lpstrTitle  = "Import Skinned Model";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn) ? std::string(buf) : std::string();
}
#else
static std::string OpenSceneDialogNative(GLFWwindow*) { return {}; }
static std::string SaveSceneDialogNative(GLFWwindow*) { return {}; }
static std::string OpenAudioDialogNative() { return {}; }
static std::string OpenModelDialogNative(GLFWwindow*) { return {}; }
#endif

// Stable content GUID from a path — mirrors schizo::assets::asset_id_from_path
// (FNV-1a 64, low bit forced set) so audio clip GUIDs match the asset system
// without pulling the asset-pipeline header into this TU.
static uint64_t AudioGuidFromPath(const std::string& p) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : p) { h ^= c; h *= 1099511628211ull; }
    return h | 1ull;
}

// ============================================================================
// "Keep changes from play?" — shown on Stop when play changed something.
//
// Nothing has been kept at the point this opens: StopPlayback already restored
// the authored scene. Every row is an opt-in re-application, so closing the
// window with Escape or Discard leaves exactly the pre-play state.
// ============================================================================
// ============================================================================
// "This scene changed on disk" — the half of hot reload that must not be automatic.
//
// Every other asset type reloads silently, because the worst case is a texture
// popping. A scene is different: reloading throws away everything unsaved in the
// editor, and the change on disk may be a git pull, a teammate, or an agent.
// Silently reloading would be the single most destructive thing hot reload
// could do, which is why 2.4 left scenes for last rather than treating them
// like meshes.
//
// So the rule is: reload automatically ONLY when there is nothing to lose.
// With unsaved changes, stop and ask — and make "keep mine" the safe default by
// requiring an explicit click to discard.
// ============================================================================
void ShowSceneReloadPrompt(EditorState& editor_state) {
    if (!editor_state.show_scene_reload_prompt) return;

    ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Always);
    if (!ImGui::Begin("Scene changed on disk", &editor_state.show_scene_reload_prompt,
                      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped("%s was modified outside the editor.",
                       editor_state.editor_scene->GetSceneFilepath().c_str());
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.92f, 0.72f, 0.35f, 1.0f),
                       "You have unsaved changes. Reloading discards them.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Keep my version", ImVec2(150, 0))) {
        // Deliberately does nothing to the file. The next save overwrites the
        // on-disk version, which is what "keep mine" has to mean.
        editor_state.show_scene_reload_prompt = false;
        editor_state.scene_changed_on_disk = false;
        editor_state.set_status("Kept the in-editor scene; disk version ignored");
    }
    ImGui::SameLine();
    if (ImGui::Button("Save mine over it", ImVec2(150, 0))) {
        const std::string path = editor_state.editor_scene->GetSceneFilepath();
        if (!path.empty() && editor_state.editor_scene->SaveScene(path))
            editor_state.set_status("Saved over the on-disk scene");
        editor_state.show_scene_reload_prompt = false;
        editor_state.scene_changed_on_disk = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard mine, reload", ImVec2(160, 0))) {
        const std::string path = editor_state.editor_scene->GetSceneFilepath();
        if (!path.empty() && editor_state.editor_scene->LoadScene(path)) {
            editor_state.selected_entity_id = 0;
            editor_state.set_status("Reloaded scene from disk");
        }
        editor_state.show_scene_reload_prompt = false;
        editor_state.scene_changed_on_disk = false;
    }

    ImGui::End();
}

// ============================================================================
// Background work — visible, and interruptible.
//
// Shows only while something is running, or briefly after a failure. A panel
// that is always on screen showing "0 tasks" is furniture; one that appears
// exactly when the editor is busy is information.
// ============================================================================
// ============================================================================
// Undo support for entity creation.
//
// Every "Create X" command used to undo itself with
// `scene->GetEntityByName("Cube")`. Names are NOT unique -- CreateEntity does
// not enforce it and GetEntityByName returns the FIRST match -- so creating two
// cubes and pressing undo removed the WRONG one, and renaming the new entity
// first made undo silently do nothing.
//
// Undo that deletes the wrong object is worse than no undo, because the user
// trusts it. So creation records the id it actually produced and undo removes
// exactly that entity. Redo re-runs creation and overwrites the id, which is
// why the cell is shared rather than captured by value.
// ============================================================================
static std::shared_ptr<schizo::scene::Entity> FindEntityById(
        const std::shared_ptr<schizo::scene::Scene>& scene, uint32_t id) {
    if (!scene || !id) return nullptr;
    for (const auto& e : scene->GetEntities())
        if (e && e->GetId() == id) return e;
    return nullptr;
}

// ============================================================================
// Undo for a logic-graph edit.
//
// The graph round-trips through text (logic_to_text / logic_from_text), so an
// entry stores the whole graph before and after. That is fine here and would
// NOT be fine for terrain: a graph is a handful of nodes, a heightmap is
// megabytes. Same problem, different right answer.
// ============================================================================
static void PushLogicGraphCommand(EditorState& editor_state,
                                  schizo::editor::EcsSceneBridge* bridge,
                                  const schizo::editor::CoalescedEdit& edit) {
    if (!bridge) return;
    const std::string before(edit.before.begin(), edit.before.end());
    const std::string after (edit.after.begin(),  edit.after.end());

    auto apply = [bridge](const std::string& text) {
        schizo::editor::apply_logic_graph_text(*bridge, text);
    };

    auto cmd = std::make_unique<schizo::editor::FunctionCommand>(
        [apply, after, &editor_state]()  { apply(after);  editor_state.editor_scene->MarkModified(); },
        [apply, before, &editor_state]() { apply(before); editor_state.editor_scene->MarkModified(); },
        "Edit logic graph");
    editor_state.undo_redo_manager.PushExecuted(std::move(cmd));   // the panel already applied it
}

// ============================================================================
// Undo for a terrain sculpt stroke.
//
// Two problems, and the second is the one that made this the last undo item.
//
// A stroke spans many frames while the mouse is held, so it must be coalesced
// like a gizmo drag — mouse-down to mouse-up is one entry.
//
// And a heightmap is big. Storing a full copy per stroke is the obvious
// implementation and the wrong one: at higher resolutions that is megabytes per
// entry, so a session of sculpting would quietly consume hundreds of MB. But a
// brush only touches a small disc. So the stroke keeps ONE transient full
// snapshot (cheap, and only while the mouse is down) and the entry that
// survives stores just the bounding rectangle of what actually changed.
//
// Returns false when nothing changed, so a click that sculpts nothing does not
// push a no-op onto the stack.
// ============================================================================
static bool PushTerrainStrokeCommand(EditorState& editor_state,
                                     const std::shared_ptr<schizo::scene::Scene>& scene,
                                     uint32_t entity_id,
                                     const std::vector<float>& before_heights,
                                     const std::vector<uint8_t>& before_holes) {
    if (!scene || !entity_id) return false;
    auto ent = FindEntityById(scene, entity_id);
    if (!ent) return false;
    auto tc = ent->GetComponent<schizo::scene::TerrainComponent>();
    if (!tc) return false;

    const std::vector<float>& now_h = tc->Heights();
    if (now_h.size() != before_heights.size()) return false;   // resized mid-stroke

    const int n = tc->VertsPerSide();
    int x0 = n, x1 = -1, z0 = n, z1 = -1;
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            const size_t i = static_cast<size_t>(z) * n + x;
            if (now_h[i] != before_heights[i]) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (z < z0) z0 = z;
                if (z > z1) z1 = z;
            }
        }

    // Holes are a separate, cell-resolution mask; a hole brush changes no
    // heights at all, so it needs its own comparison or digging would be
    // silently un-undoable.
    const int res = tc->GetResolution();
    std::vector<uint8_t> now_holes(static_cast<size_t>(res) * res, 0);
    for (int z = 0; z < res; ++z)
        for (int x = 0; x < res; ++x)
            now_holes[static_cast<size_t>(z) * res + x] = tc->HasHole(x, z) ? 1u : 0u;
    const bool holes_changed = (now_holes != before_holes) && before_holes.size() == now_holes.size();

    if (x1 < x0 && !holes_changed) return false;   // the stroke did nothing

    // Copy out only the changed rectangle.
    struct Rect { int x0, z0, w, h; };
    Rect r{0, 0, 0, 0};
    std::vector<float> before_rect, after_rect;
    if (x1 >= x0) {
        r = Rect{x0, z0, x1 - x0 + 1, z1 - z0 + 1};
        before_rect.reserve(static_cast<size_t>(r.w) * r.h);
        after_rect.reserve(static_cast<size_t>(r.w) * r.h);
        for (int z = r.z0; z < r.z0 + r.h; ++z)
            for (int x = r.x0; x < r.x0 + r.w; ++x) {
                const size_t i = static_cast<size_t>(z) * n + x;
                before_rect.push_back(before_heights[i]);
                after_rect.push_back(now_h[i]);
            }
    }

    auto holes_before = holes_changed ? before_holes : std::vector<uint8_t>{};
    auto holes_after  = holes_changed ? now_holes    : std::vector<uint8_t>{};

    auto apply = [scene, entity_id, r, n, res](const std::vector<float>& rect,
                                               const std::vector<uint8_t>& holes) {
        auto e = FindEntityById(scene, entity_id);
        if (!e) return;
        auto t = e->GetComponent<schizo::scene::TerrainComponent>();
        if (!t || t->VertsPerSide() != n) return;   // resized since; refuse rather than corrupt
        if (!rect.empty()) {
            auto& h = t->MutableHeights();
            size_t k = 0;
            for (int z = r.z0; z < r.z0 + r.h; ++z)
                for (int x = r.x0; x < r.x0 + r.w; ++x)
                    h[static_cast<size_t>(z) * n + x] = rect[k++];
        }
        if (!holes.empty() && t->GetResolution() == res) {
            for (int z = 0; z < res; ++z)
                for (int x = 0; x < res; ++x)
                    t->SetHole(x, z, holes[static_cast<size_t>(z) * res + x] != 0);
            t->MarkAllDirty();          // a hole mask is compared whole, so it is
        } else if (!rect.empty()) {
            // Dirty only the cells the rect touches, so undoing a small stroke
            // rebuilds a few chunks instead of the entire terrain. The rect is
            // in VERTEX coords; a vertex influences the cells on either side.
            t->MarkDirtyRect(r.x0 - 1, r.z0 - 1, r.x0 + r.w, r.z0 + r.h);
        } else {
            t->MarkAllDirty();
        }
    };

    auto cmd = std::make_unique<schizo::editor::FunctionCommand>(
        [apply, after_rect, holes_after, &editor_state]() {
            apply(after_rect, holes_after);
            editor_state.editor_scene->MarkModified();
        },
        [apply, before_rect, holes_before, &editor_state]() {
            apply(before_rect, holes_before);
            editor_state.editor_scene->MarkModified();
        },
        "Sculpt terrain");
    editor_state.undo_redo_manager.PushExecuted(std::move(cmd));   // the stroke already applied it
    return true;
}

// ============================================================================
// Undo for reparenting in the hierarchy.
//
// Dragging one entity onto another rewrites the scene graph, and it was the
// easiest edit in the editor to do by accident -- a drag that lands one row off
// silently restructures the scene. Recording the OLD parent makes that
// recoverable.
//
// Entities are resolved by id at apply time, not captured, so an entity deleted
// and restored by an undo of its own still resolves.
// ============================================================================
static void PushReparentCommand(EditorState& editor_state,
                                const std::shared_ptr<schizo::scene::Scene>& scene,
                                uint32_t child_id,
                                uint32_t old_parent_id,
                                uint32_t new_parent_id) {
    if (!scene || !child_id || old_parent_id == new_parent_id) return;

    auto set_parent = [scene](uint32_t cid, uint32_t pid) {
        auto c = FindEntityById(scene, cid);
        if (!c) return;
        c->SetParent(pid ? FindEntityById(scene, pid) : nullptr);
    };

    auto child = FindEntityById(scene, child_id);
    const std::string label = "Reparent " + (child ? child->GetName() : std::string("entity"));

    auto cmd = std::make_unique<schizo::editor::FunctionCommand>(
        [set_parent, child_id, new_parent_id, &editor_state]() {
            set_parent(child_id, new_parent_id);
            editor_state.editor_scene->MarkModified();
        },
        [set_parent, child_id, old_parent_id, &editor_state]() {
            set_parent(child_id, old_parent_id);
            editor_state.editor_scene->MarkModified();
        },
        label);
    editor_state.undo_redo_manager.PushExecuted(std::move(cmd));   // the drop already applied it
}

// ============================================================================
// Undo for a gizmo drag.
//
// Moving something is the most common edit in any editor, and it was not
// undoable. The gizmo already has explicit BeginDrag/EndDrag, so unlike
// inspector fields there is no coalescing to infer — the gesture boundaries are
// stated by the code. Capture on begin, push on release.
//
// Looks the entity up by id at apply time rather than capturing a pointer: it
// may have been deleted and restored by an undo of its own since this entry was
// recorded.
// ============================================================================
static void PushGizmoTransformCommand(EditorState& editor_state,
                                      const std::shared_ptr<schizo::scene::Scene>& scene,
                                      uint32_t entity_id,
                                      const glm::vec3& before_pos,
                                      const glm::quat& before_rot,
                                      const glm::vec3& before_scale) {
    if (!scene || !entity_id) return;
    auto ent = FindEntityById(scene, entity_id);
    if (!ent) return;
    auto* t = ent->GetTransform();
    if (!t) return;

    const glm::vec3 after_pos   = t->GetLocalPosition();
    const glm::quat after_rot   = t->GetLocalRotation();
    const glm::vec3 after_scale = t->GetLocalScale();

    // A click that selects without moving anything is not an edit. Recording it
    // would put no-ops on the undo stack, which reads as Ctrl+Z being broken.
    const float eps = 1e-5f;
    const bool moved =
        glm::length(after_pos   - before_pos)   > eps ||
        glm::length(after_scale - before_scale) > eps ||
        std::fabs(std::fabs(glm::dot(after_rot, before_rot)) - 1.0f) > eps;
    if (!moved) return;

    auto apply = [scene, entity_id](const glm::vec3& p, const glm::quat& r, const glm::vec3& sc) {
        auto e = FindEntityById(scene, entity_id);
        if (!e) return;
        if (auto* tr = e->GetTransform()) {
            tr->SetLocalPosition(p);
            tr->SetLocalRotation(r);
            tr->SetLocalScale(sc);
        }
    };

    auto cmd = std::make_unique<schizo::editor::FunctionCommand>(
        [apply, after_pos, after_rot, after_scale, &editor_state]() {
            apply(after_pos, after_rot, after_scale);
            editor_state.editor_scene->MarkModified();
        },
        [apply, before_pos, before_rot, before_scale, &editor_state]() {
            apply(before_pos, before_rot, before_scale);
            editor_state.editor_scene->MarkModified();
        },
        "Move " + ent->GetName());
    // Already applied by the drag itself.
    editor_state.undo_redo_manager.PushExecuted(std::move(cmd));
}

// ============================================================================
// Undo for an inspector field edit.
//
// The value arrives already coalesced — one entry per drag, not one per frame
// (edit_coalescer.h). Both directions are a deserialize of a stored blob, which
// works for ANY authorable component with no per-component code, the same
// property that made play-mode change keeping cheap.
//
// The entity is looked up by id at apply time rather than captured: it may have
// been deleted and restored by an undo of its own since this entry was pushed,
// and holding a stale pointer would write into a detached object.
// ============================================================================
static void PushFieldEditCommand(EditorState& editor_state,
                                 const schizo::editor::CoalescedEdit& edit) {
    if (!editor_state.ecs_bridge || !editor_state.editor_scene) return;

    const std::string comp   = edit.component;
    const uint32_t    ent_id = edit.entity_id;
    auto              before = edit.before;
    auto              after  = edit.after;
    auto*             bridge = editor_state.ecs_bridge;

    auto cmd = std::make_unique<schizo::editor::FunctionCommand>(
        [bridge, comp, ent_id, after, &editor_state]() {
            schizo::editor::apply_component_bytes(*bridge, ent_id, comp, after);
            editor_state.editor_scene->MarkModified();
        },
        [bridge, comp, ent_id, before, &editor_state]() {
            schizo::editor::apply_component_bytes(*bridge, ent_id, comp, before);
            editor_state.editor_scene->MarkModified();
        },
        "Edit " + comp);
    // Already applied by the widget as the user dragged it. Routing this
    // through ExecuteCommand would re-apply it -- harmless for an idempotent
    // write, wrong in principle, and wrong outright the moment a command is not
    // idempotent.
    editor_state.undo_redo_manager.PushExecuted(std::move(cmd));
}

// ============================================================================
// Undo support for entity deletion — the case people actually need.
//
// Deleting was NOT undoable at all: all three paths (hierarchy context menu,
// the inspector button, the Delete key) called scene->RemoveEntity directly.
// Undo covering "Create Cube" but not "delete the thing I spent an hour on" is
// the wrong way round.
//
// This is cheap because RemoveEntity only UNREGISTERS: entities are held by
// shared_ptr, so the object survives as long as something references it. The
// command holds that reference, which keeps the deleted entity alive on the
// undo stack, and AddEntity is its exact inverse. Redo re-removes the same
// object, so identity — and therefore the ECS mapping keyed on its Transform —
// is preserved across any number of undo/redo cycles.
//
// Deliberately symmetric with existing behaviour: RemoveEntity does not touch
// children, so neither does this. Restoring more than was removed would be its
// own kind of surprise.
// ============================================================================
static void PushDeleteEntityCommand(EditorState& editor_state,
                                    const std::shared_ptr<schizo::scene::Scene>& scene,
                                    const std::shared_ptr<schizo::scene::Entity>& entity) {
    if (!scene || !entity) return;
    const std::string name = entity->GetName();
    const uint32_t    id   = entity->GetId();

    auto cmd = std::make_unique<schizo::editor::FunctionCommand>(
        [scene, entity, id, name, &editor_state]() {
            scene->RemoveEntity(entity);
            if (editor_state.selected_entity_id == id)
                editor_state.selected_entity_id = 0;
            editor_state.editor_scene->MarkModified();
            spdlog::info("Deleted entity: {}", name);
        },
        [scene, entity, &editor_state]() {
            scene->AddEntity(entity);
            editor_state.editor_scene->MarkModified();
            spdlog::info("Restored entity: {}", entity->GetName());
        },
        "Delete " + name);
    editor_state.undo_redo_manager.ExecuteCommand(std::move(cmd));
}

void ShowTaskPanel(EditorState& editor_state) {
    if (!editor_state.show_task_panel) return;
    const auto tasks = editor_state.tasks.snapshot();
    if (tasks.empty()) return;

    bool anything_worth_showing = false;
    for (const auto& t : tasks)
        if (t.state == gws::tasks::TaskState::Pending ||
            t.state == gws::tasks::TaskState::Running ||
            t.state == gws::tasks::TaskState::Failed) { anything_worth_showing = true; break; }
    if (!anything_worth_showing) return;

    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Background tasks", &editor_state.show_task_panel,
                      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::End();
        return;
    }

    for (const auto& t : tasks) {
        using S = gws::tasks::TaskState;
        if (t.state == S::Succeeded || t.state == S::Cancelled) continue;

        ImGui::PushID(static_cast<int>(t.id));
        if (t.state == S::Failed) {
            ImGui::TextColored(ImVec4(0.92f, 0.45f, 0.42f, 1.0f), "%s — failed", t.label.c_str());
            if (!t.error.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t.error.c_str());
            }
        } else {
            ImGui::TextUnformatted(t.label.c_str());
            // A negative fraction means the task genuinely does not know how far
            // along it is. An indeterminate bar says that; a bar parked at 0
            // would look like nothing is happening.
            if (t.progress < 0.0f) ImGui::ProgressBar(-1.0f * ImGui::GetTime(), ImVec2(-70, 0), "working");
            else                   ImGui::ProgressBar(t.progress, ImVec2(-70, 0));
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancel")) editor_state.tasks.cancel(t.id);
            if (!t.status.empty()) ImGui::TextDisabled("%s", t.status.c_str());
        }
        ImGui::PopID();
    }

    ImGui::End();
}

void ShowPlayChangesDialog(EditorState& editor_state) {
    if (!editor_state.show_play_changes_popup) return;

    auto& report = editor_state.pending_play_changes;
    ImGui::SetNextWindowSize(ImVec2(620, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Changes from play mode", &editor_state.show_play_changes_popup)) {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped(
        "Play mode changed %d value(s). They have already been reverted — tick the ones "
        "to re-apply to the scene.", static_cast<int>(report.changes.size()));

    if (report.entities_spawned || report.entities_destroyed) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.85f, 0.72f, 0.35f, 1.0f),
            "Not tracked: %d entity(s) spawned and %d destroyed during play. "
            "Only value changes on entities that existed before play can be kept.",
            static_cast<int>(report.entities_spawned),
            static_cast<int>(report.entities_destroyed));
    }

    ImGui::Spacing();
    if (ImGui::SmallButton("Select all")) for (auto& c : report.changes) c.keep = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("Select none")) for (auto& c : report.changes) c.keep = false;
    ImGui::Separator();

    const float footer = ImGui::GetFrameHeightWithSpacing() + 8.0f;
    if (ImGui::BeginChild("##play_change_rows", ImVec2(0, -footer))) {
        if (ImGui::BeginTable("##changes", 3,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Keep", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("Entity", ImGuiTableColumnFlags_WidthFixed, 160.0f);
            ImGui::TableSetupColumn("What changed");
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < report.changes.size(); ++i) {
                auto& c = report.changes[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(i));
                ImGui::Checkbox("##keep", &c.keep);
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(c.entity_name.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s — %s", c.component.c_str(), c.summary.c_str());
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    int selected = 0;
    for (const auto& c : report.changes) if (c.keep) ++selected;

    ImGui::BeginDisabled(selected == 0);
    if (ImGui::Button(selected ? "Keep selected" : "Keep selected (none)")) {
        auto scene = editor_state.editor_scene ? editor_state.editor_scene->GetScene() : nullptr;
        // Capture what these rows are about to overwrite, BEFORE applying --
        // afterwards the authored values are gone. The inverse of an apply is
        // another apply, so undo cannot disagree with redo.
        auto undo_rows = schizo::editor::PlayModeChanges::SnapshotCurrent(
            report.changes, scene, editor_state.ecs_bridge);
        auto redo_rows = report.changes;

        const size_t n = schizo::editor::PlayModeChanges::Apply(
            report.changes, scene, editor_state.ecs_bridge);
        spdlog::info("Kept {} change(s) from play mode", n);

        if (n > 0 && scene) {
            auto* bridge = editor_state.ecs_bridge;
            auto cmd = std::make_unique<schizo::editor::FunctionCommand>(
                [scene, bridge, redo_rows, &editor_state]() {
                    schizo::editor::PlayModeChanges::Apply(redo_rows, scene, bridge);
                    editor_state.editor_scene->MarkModified();
                },
                [scene, bridge, undo_rows, &editor_state]() {
                    schizo::editor::PlayModeChanges::Apply(undo_rows, scene, bridge);
                    editor_state.editor_scene->MarkModified();
                },
                "Keep " + std::to_string(n) + " play-mode change(s)");
            editor_state.undo_redo_manager.PushExecuted(std::move(cmd));   // already applied
            editor_state.editor_scene->MarkModified();
        }

        report = {};
        editor_state.show_play_changes_popup = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Discard all")) {
        report = {};
        editor_state.show_play_changes_popup = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d of %d selected", selected, static_cast<int>(report.changes.size()));

    ImGui::End();
}

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
// Dock Layout (Unity-style fixed/tiled workspace)
// ============================================================================

// Bump when the docked-panel set changes so an existing editor.ini layout
// (which predates a new panel) is rebuilt once into the default arrangement.
static constexpr int kEditorDockLayoutVersion = 2;   // 2 = added Output + Terminal

// Build the default docked layout into `dockspace_id`: a Unity-classic
// arrangement — Hierarchy (left), Viewport (center), Inspector (right), and a
// bottom strip with Asset Browser / Performance / Debug / Post-Processing /
// Playback tabbed together. The window names MUST match each panel's exact
// ImGui::Begin() title (including any "##suffix").
static void BuildEditorDockLayout(ImGuiID dockspace_id, ImVec2 size) {
    ImGui::DockBuilderRemoveNode(dockspace_id);                                  // wipe any prior layout
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, size);

    // Split the central node into the four Unity regions. Each split shrinks
    // `center`, which ends up as the middle viewport node.
    ImGuiID center = dockspace_id;
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down,  0.26f, nullptr, &center);
    ImGuiID left   = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left,  0.18f, nullptr, &center);
    ImGuiID right  = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);

    ImGui::DockBuilderDockWindow("Viewport",             center);
    ImGui::DockBuilderDockWindow("Scene Hierarchy",      left);
    ImGui::DockBuilderDockWindow("Inspector",            right);
    ImGui::DockBuilderDockWindow("Asset Browser##panel", bottom);
    ImGui::DockBuilderDockWindow("Performance",          bottom);
    ImGui::DockBuilderDockWindow("Debug Systems",        bottom);
    ImGui::DockBuilderDockWindow("Post-Processing",      bottom);
    ImGui::DockBuilderDockWindow("Scene Playback",       bottom);
    ImGui::DockBuilderDockWindow("Output",               bottom);
    ImGui::DockBuilderDockWindow("Terminal",             bottom);

    // The central viewport reads cleaner without a tab bar (it holds only the
    // 3D scene), matching Unity's Scene view.
    if (ImGuiDockNode* c = ImGui::DockBuilderGetNode(center))
        c->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;

    ImGui::DockBuilderFinish(dockspace_id);
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
            // Import a rigged character (.gltf/.glb) → GPU-skinned actor.
            // Gated by the Animation feature (modular project system).
            if (editor_state.feature_on(schizo::project::Feature::Animation) &&
                ImGui::MenuItem("Import Skinned Model (.glb/.gltf)...")) {
                std::string path = OpenModelDialogNative(glfw_window);
                if (!path.empty()) editor_state.pending_skinned_import = path;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                spdlog::info("Exit requested");
            }
            ImGui::EndMenu();
        }

        // Project menu — the current project + its modular feature set.
        if (ImGui::BeginMenu("Project")) {
            if (editor_state.project_loaded) {
                ImGui::MenuItem(("Current: " + editor_state.project.name).c_str(),
                                nullptr, false, false);
                ImGui::Separator();
            }
            if (ImGui::MenuItem("Project Settings (Features)...", nullptr, false,
                                editor_state.project_loaded)) {
                editor_state.show_project_settings = true;
            }
            if (ImGui::MenuItem("Close Project (back to Launcher)", nullptr, false,
                                editor_state.project_loaded)) {
                editor_state.in_launcher = true;
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
            ImGui::MenuItem("Logic Graph", nullptr, &editor_state.show_logic_graph);
            ImGui::MenuItem("Viewport", nullptr, &editor_state.show_viewport);
            ImGui::Separator();
            ImGui::MenuItem("Playback Controls", nullptr, &editor_state.show_playback_controls);
            ImGui::MenuItem("Performance (Stage 14)", nullptr, &editor_state.show_performance);
            ImGui::MenuItem("Debug Panels (Phase 6)", nullptr, &editor_state.show_debug_panels);
            ImGui::MenuItem("Post-Processing", nullptr, &editor_state.show_post_processing);
            ImGui::MenuItem("Output (Log)", nullptr, &editor_state.show_output);
            ImGui::MenuItem("Terminal", nullptr, &editor_state.show_terminal);
            if (editor_state.feature_on(schizo::project::Feature::Networking))
                ImGui::MenuItem("Multiplayer (Network)", nullptr, &editor_state.show_network_window);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout")) {
                // Rebuild the default Unity-style dock arrangement next frame.
                editor_state.request_reset_layout = true;
                // Re-open every dockable panel so the rebuilt layout is fully
                // populated (a closed panel wouldn't claim its dock slot).
                editor_state.show_scene_hierarchy = true;
                editor_state.show_inspector       = true;
                editor_state.show_asset_browser   = true;
                editor_state.show_viewport        = true;
                editor_state.show_playback_controls = true;
                editor_state.show_performance     = true;
                editor_state.show_debug_panels    = true;
                editor_state.show_post_processing = true;
                editor_state.show_terminal        = true;
                editor_state.show_output          = true;
                spdlog::info("Reset editor dock layout to default");
            }
            ImGui::EndMenu();
        }
        
        // Tools menu
        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Build Scene")) {
                spdlog::info("Build Scene");
            }

            // Bake a navmesh from the geometry that is actually in the scene
            // (3.4). Until now the only navmesh in the editor was a synthetic
            // grid generated inside the animation demo, so the AI pathed around
            // an obstacle that existed nowhere and over ground that was not the
            // ground.
            // World streaming (3.3). A toggle rather than always-on: streaming
            // deactivates entities, and hiding parts of the scene someone is
            // editing would be a bug.
            {
                bool streaming = editor_state.world_streaming.enabled();
                if (ImGui::MenuItem("World Streaming", nullptr, &streaming)) {
                    editor_state.world_streaming.set_enabled(streaming);
                    if (!streaming) {
                        // Switching off must put everything back, or the scene
                        // keeps whatever was hidden at that moment hidden.
                        editor_state.world_streaming.restore_all(
                            editor_state.editor_scene->GetScene());
                        editor_state.set_status("World streaming off — all entities restored");
                    } else {
                        editor_state.set_status("World streaming on — cells follow the camera");
                    }
                }
                if (editor_state.world_streaming.enabled()) {
                    const auto& ws = editor_state.world_streaming;
                    ImGui::MenuItem(("  cells: " + std::to_string(ws.loaded_cells()) +
                                     " loaded, " + std::to_string(ws.streamed_out()) +
                                     " entities streamed out").c_str(), nullptr, false, false);
                    ImGui::MenuItem(("  rebases: " + std::to_string(ws.rebase_count())).c_str(),
                                    nullptr, false, false);
                }
            }

            // Snapping (4.7). In the Tools menu AND on Ctrl, because a step
            // size is a setting but "snap this one drag" is a reflex.
            ImGui::Separator();
            ImGui::MenuItem("Snapping", "hold Ctrl", &editor_state.snap.enabled);
            if (editor_state.snap.enabled) {
                ImGui::SetNextItemWidth(110);
                ImGui::DragFloat("  grid",    &editor_state.snap.translate,  0.05f, 0.01f, 100.0f);
                ImGui::SetNextItemWidth(110);
                ImGui::DragFloat("  degrees", &editor_state.snap.rotate_deg, 1.0f,  1.0f,  90.0f);
                ImGui::SetNextItemWidth(110);
                ImGui::DragFloat("  scale",   &editor_state.snap.scale,      0.01f, 0.01f, 10.0f);
                ImGui::MenuItem("  relative to drag start", nullptr, &editor_state.snap.relative);
                if (editor_state.snap.relative)
                    ImGui::MenuItem("  (keeps off-grid layouts put)", nullptr, false, false);
            }
            ImGui::Separator();

            if (editor_state.feature_on(schizo::project::Feature::AI) &&
                ImGui::MenuItem("Bake Navmesh")) {
                auto nav_scene = editor_state.editor_scene->GetScene();
                editor_state.nav_stats = schizo::editor::bake_navmesh_from_scene(
                    nav_scene, editor_state.scene_navmesh);
                const auto& ns = editor_state.nav_stats;
                if (ns.ok) {
                    editor_state.set_status(
                        "Navmesh: " + std::to_string(ns.triangles) + " tris from " +
                        std::to_string(ns.terrains) + " terrain(s), " +
                        std::to_string(ns.boxes) + " box collider(s)" +
                        (ns.skipped_shapes ? " — " + std::to_string(ns.skipped_shapes) +
                                             " unsupported shape(s) skipped" : ""));
                } else {
                    // Say WHY rather than just failing: an empty navmesh and a
                    // scene with no walkable geometry look identical to a user.
                    editor_state.set_status(
                        "Navmesh: nothing walkable found — add a terrain or a box collider");
                }
            }
            if (editor_state.npc_agents.agent_count() > 0) {
                ImGui::MenuItem(("  agents: " + std::to_string(editor_state.npc_agents.agent_count()) +
                                 ", " + std::to_string(editor_state.npc_agents.chasing()) +
                                 " chasing").c_str(), nullptr, false, false);
            }
            
            const bool playing_now = editor_state.scene_playback_manager &&
                                     editor_state.scene_playback_manager->IsPlaying();
            const char* play_label = playing_now ? "Stop (F5)" : "Play (F5)";
            if (ImGui::MenuItem(play_label)) {
                auto play_scene = editor_state.editor_scene->GetScene();
                if (playing_now) EndPlayMode(editor_state, play_scene);
                else             BeginPlayMode(editor_state, play_scene);
            }

            // F5: item-definition data files (.items) in assets/gameplay/.
            ImGui::Separator();
            if (editor_state.ecs_bridge && ImGui::MenuItem("Reload Item Defs (.items)")) {
                const int n = editor_state.ecs_bridge->load_gameplay_data("assets/gameplay");
                editor_state.set_status("Reloaded " + std::to_string(n) + " item def(s) from assets/gameplay/");
            }
            if (editor_state.ecs_bridge && ImGui::MenuItem("Create example .items")) {
                std::error_code ec; std::filesystem::create_directories("assets/gameplay", ec);
                if (editor_state.ecs_bridge->create_example_items("assets/gameplay/example.items")) {
                    const int n = editor_state.ecs_bridge->load_gameplay_data("assets/gameplay");
                    editor_state.set_status("Wrote assets/gameplay/example.items — loaded " + std::to_string(n) + " item def(s)");
                } else {
                    editor_state.set_status("Could not write assets/gameplay/example.items");
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
    
    ImGui::Begin("Scene Hierarchy", &editor_state.show_scene_hierarchy, flags);  // docked window = child; End() must always run
    {
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
                auto created_id = std::make_shared<uint32_t>(0);   // see FindEntityById
                auto create_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                    [scene, &editor_state, created_id]() {
                        auto ent = schizo::scene::EntityFactory::CreateCube(scene);
                        *created_id = ent ? ent->GetId() : 0;
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Created Cube entity");
                    },
                    [scene, &editor_state, created_id]() {
                        auto ent = FindEntityById(scene, *created_id);
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

            // Spawn from a saved prefab (F4): instantiate a cube, then apply the
            // prefab's gameplay components. Lists prefabs/*.prefab.
            if (editor_state.ecs_bridge && ImGui::BeginMenu("From Prefab")) {
                std::error_code ec;
                bool any = false;
                if (std::filesystem::exists("prefabs", ec)) {
                    for (const auto& de : std::filesystem::directory_iterator("prefabs", ec)) {
                        if (de.path().extension() != ".prefab") continue;
                        any = true;
                        const std::string stem = de.path().stem().string();
                        if (ImGui::MenuItem(stem.c_str())) {
                            if (auto ent = schizo::scene::EntityFactory::CreateCube(scene)) {
                                editor_state.ecs_bridge->sync_and_run(scene);  // create its ECS entity
                                editor_state.ecs_bridge->apply_prefab_file(
                                    ent->GetTransform(), de.path().string());
                                editor_state.editor_scene->MarkModified();
                                editor_state.set_status("Spawned prefab: " + stem);
                            }
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
                if (!any) ImGui::TextDisabled("(no prefabs — save one from the Inspector)");
                ImGui::EndMenu();
            }

            // Spawn a cube pre-loaded with every G0–G4 gameplay system (attributes,
            // progression, skill tree, state machine, combat, ability, inventory)
            // so they can be exercised live from the Inspector. Great for testing.
            if (editor_state.ecs_bridge && ImGui::MenuItem("Gameplay Test Dummy")) {
                if (auto ent = schizo::scene::EntityFactory::CreateCube(scene)) {
                    editor_state.ecs_bridge->sync_and_run(scene);   // create its ECS entity
                    editor_state.ecs_bridge->make_gameplay_dummy(ent->GetTransform());
                    editor_state.editor_scene->MarkModified();
                    editor_state.set_status("Spawned Gameplay Test Dummy — select it, see Inspector > Gameplay Components (ECS)");
                }
                ImGui::CloseCurrentPopup();
            }

            if (editor_state.feature_on(schizo::project::Feature::Terrain) &&
                ImGui::MenuItem("Water")) {
                auto created_id = std::make_shared<uint32_t>(0);   // see FindEntityById
                auto create_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                    [scene, &editor_state, created_id]() {
                        auto ent = scene->CreateEntity("Water");
                        *created_id = ent ? ent->GetId() : 0;
                        if (ent) {
                            ent->AddComponent<schizo::scene::WaterComponent>();
                            ent->GetTransform()->SetLocalPosition(glm::vec3(0.0f, 0.5f, 0.0f));
                        }
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Created Water entity");
                    },
                    [scene, &editor_state, created_id]() {
                        auto ent = FindEntityById(scene, *created_id);
                        if (ent) scene->RemoveEntity(ent);
                    },
                    "Create Water"
                );
                editor_state.undo_redo_manager.ExecuteCommand(std::move(create_cmd));
                ImGui::CloseCurrentPopup();
            }

            if (editor_state.feature_on(schizo::project::Feature::Terrain) &&
                ImGui::MenuItem("Terrain")) {
                auto created_id = std::make_shared<uint32_t>(0);   // see FindEntityById
                auto create_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                    [scene, &editor_state, created_id]() {
                        auto ent = scene->CreateEntity("Terrain");
                        *created_id = ent ? ent->GetId() : 0;
                        if (ent) ent->AddComponent<schizo::scene::TerrainComponent>();
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Created Terrain entity");
                    },
                    [scene, &editor_state, created_id]() {
                        auto ent = FindEntityById(scene, *created_id);
                        if (ent) { scene->RemoveEntity(ent); spdlog::info("Removed Terrain entity"); }
                    },
                    "Create Terrain"
                );
                editor_state.undo_redo_manager.ExecuteCommand(std::move(create_cmd));
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("Sphere")) {
                auto created_id = std::make_shared<uint32_t>(0);   // see FindEntityById
                auto create_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                    [scene, &editor_state, created_id]() {
                        auto ent = schizo::scene::EntityFactory::CreateSphere(scene);
                        *created_id = ent ? ent->GetId() : 0;
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Created Sphere entity");
                    },
                    [scene, &editor_state, created_id]() {
                        auto ent = FindEntityById(scene, *created_id);
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
                auto created_id = std::make_shared<uint32_t>(0);   // see FindEntityById
                auto create_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                    [scene, &editor_state, created_id]() {
                        auto ent = schizo::scene::EntityFactory::CreateCapsule(scene);
                        *created_id = ent ? ent->GetId() : 0;
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Created Capsule entity");
                    },
                    [scene, &editor_state, created_id]() {
                        auto ent = FindEntityById(scene, *created_id);
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
                auto created_id = std::make_shared<uint32_t>(0);   // see FindEntityById
                auto create_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                    [scene, &editor_state, created_id]() {
                        auto ent = schizo::scene::EntityFactory::CreateCylinder(scene);
                        *created_id = ent ? ent->GetId() : 0;
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Created Cylinder entity");
                    },
                    [scene, &editor_state, created_id]() {
                        auto ent = FindEntityById(scene, *created_id);
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
                auto created_id = std::make_shared<uint32_t>(0);   // see FindEntityById
                auto create_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                    [scene, &editor_state, created_id]() {
                        auto ent = schizo::scene::EntityFactory::CreatePlane(scene);
                        *created_id = ent ? ent->GetId() : 0;
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Created Plane entity");
                    },
                    [scene, &editor_state, created_id]() {
                        auto ent = FindEntityById(scene, *created_id);
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
                auto created_id = std::make_shared<uint32_t>(0);   // see FindEntityById
                auto create_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                    [scene, &editor_state, created_id]() {
                        auto ent = schizo::scene::EntityFactory::CreatePlayer(scene);
                        *created_id = ent ? ent->GetId() : 0;
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Created Player entity");
                    },
                    [scene, &editor_state, created_id]() {
                        auto ent = FindEntityById(scene, *created_id);
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
                auto created_id = std::make_shared<uint32_t>(0);   // see FindEntityById
                auto create_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                    [scene, &editor_state, created_id]() {
                        auto ent = schizo::scene::EntityFactory::CreateCamera(scene);
                        *created_id = ent ? ent->GetId() : 0;
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Created Camera entity");
                    },
                    [scene, &editor_state, created_id]() {
                        auto ent = FindEntityById(scene, *created_id);
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
                auto created_id = std::make_shared<uint32_t>(0);   // see FindEntityById
                auto create_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                    [scene, &editor_state, created_id]() {
                        auto ent = schizo::scene::EntityFactory::CreateDirectionalLight(scene);
                        *created_id = ent ? ent->GetId() : 0;
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Created DirectionalLight entity");
                    },
                    [scene, &editor_state, created_id]() {
                        auto ent = FindEntityById(scene, *created_id);
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
                auto created_id = std::make_shared<uint32_t>(0);   // see FindEntityById
                auto create_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                    [scene, &editor_state, created_id]() {
                        auto ent = schizo::scene::EntityFactory::CreateGlobalLight(scene);
                        *created_id = ent ? ent->GetId() : 0;
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Created GlobalLight entity");
                    },
                    [scene, &editor_state, created_id]() {
                        auto ent = FindEntityById(scene, *created_id);
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
                auto created_id = std::make_shared<uint32_t>(0);   // see FindEntityById
                auto create_cmd = std::make_unique<schizo::editor::FunctionCommand>(
                    [scene, entity_name, &editor_state, created_id]() {
                        auto ent = scene->CreateEntity(entity_name);
                        *created_id = ent ? ent->GetId() : 0;
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Created entity: {}", entity_name);
                    },
                    [scene, entity_name, &editor_state, created_id]() {
                        // By id, not by name: entity names are not unique, and
                        // a rename would otherwise make undo silently no-op.
                        auto ent = FindEntityById(scene, *created_id);
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
                            auto old_parent = dragged->GetParent();
                            const uint32_t old_pid = old_parent ? old_parent->GetId() : 0;
                            dragged->SetParent(entity);
                            spdlog::info("Reparented {} to {}", dragged->GetName(), entity->GetName());
                            PushReparentCommand(editor_state, scene, dragged_entity_id,
                                                old_pid, entity->GetId());
                            editor_state.editor_scene->MarkModified();
                        }
                    }
                    
                    // Also accept mesh assets — import into the project + assign.
                    if (const ImGuiPayload* mesh_payload = ImGui::AcceptDragDropPayload("MESH_ASSET")) {
                        const char* dropped = static_cast<const char*>(mesh_payload->Data);
                        if (dropped && dropped[0]) {
                            const std::string proj = schizo::editor::import_asset_into_project(dropped, "models");
                            if (!proj.empty()) {
                                entity->SetMesh(proj);
                                editor_state.editor_scene->MarkModified();
                                editor_state.set_status("Applied mesh: " + proj + "  (on '" + entity->GetName() + "')");
                            } else {
                                editor_state.set_status(std::string("Mesh not found on disk: ") + dropped);
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
                        PushDeleteEntityCommand(editor_state, scene, entity);
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
    
    ImGui::Begin("Inspector", &editor_state.show_inspector, flags);  // docked window = child; End() must always run
    {
        auto scene = editor_state.editor_scene->GetScene();
        if (!scene || editor_state.selected_entity_id == 0) {
            ImGui::TextDisabled("No entity selected.");
            if (scene) {
                // ---- Scene Environment (scene-bound sky) ----
                ImGui::Separator();
                ImGui::TextUnformatted("Scene Environment");
                ImGui::TextDisabled("Each scene stores its own sky HDR.");

                char sky[260]; std::snprintf(sky, sizeof(sky), "%s", scene->GetSkyHdr().c_str());
                ImGui::SetNextItemWidth(-90);
                if (ImGui::InputText("Sky HDR", sky, sizeof(sky))) {
                    scene->SetSkyHdr(sky); editor_state.editor_scene->MarkModified();
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("TEXTURE_ASSET")) {
                        scene->SetSkyHdr(std::string(static_cast<const char*>(pl->Data)));
                        editor_state.editor_scene->MarkModified();
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear")) { scene->SetSkyHdr(""); editor_state.editor_scene->MarkModified(); }
                if (scene->GetSkyHdr().empty()) ImGui::TextDisabled("(procedural gradient sky)");
                ImGui::TextDisabled("Drag an .hdr here, or type a project-relative path.");

                float intensity = scene->GetSkyIntensity();
                ImGui::SetNextItemWidth(-90);
                if (ImGui::DragFloat("Sky Intensity", &intensity, 0.02f, 0.0f, 10.0f)) {
                    scene->SetSkyIntensity(intensity); editor_state.editor_scene->MarkModified();
                }
                ImGui::Dummy(ImVec2(0, 4));
                ImGui::TextWrapped("The sky updates live as you change this. Save the scene to "
                                   "keep it. Sky Intensity is authored here but not yet applied "
                                   "to rendering.");
            }
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
        // Particle emitter (3.9) and NPC agent (3.5). Neither had ANY inspector
        // UI: both components existed, both were read every frame by their
        // runtime systems, and neither could be configured from the editor at
        // all -- only from a script. Drawn from reflection, so a field added to
        // either shows up here AND in the save file with no edit in either
        // place. A component you can configure but not save is worse than one
        // you cannot configure, and keeping both off one source is what stops
        // them drifting apart.
        //
        // Unlike the skinned-mesh section these are shown whether or not they
        // are enabled, because the enable toggle IS the first field -- hiding
        // the section until enabled would leave no way to enable it.
        if (auto* pec = selected_entity->GetParticleEmitterComponent()) {
            if (ImGui::CollapsingHeader("Particle Emitter")) {
                // The colour ramp (4.5), the first consumer of the gradient
                // widget. The component still stores a start and an end colour,
                // which is exactly the limitation the item describes -- but
                // showing them as two disconnected swatches hid what the effect
                // actually does. Over a checkerboard, a fade to transparent is
                // finally distinguishable from a fade to black, which the raw
                // DragFloat4s below cannot show at all.
                ImGui::TextDisabled("Colour over life");
                gws::anim::Gradient ramp =
                    gws::anim::Gradient::two_stop(pec->color_start, pec->color_end);
                static int emitter_stop = 0;
                if (schizo::editor::draw_gradient_editor("##emitramp", ramp, 22.0f,
                                                         &emitter_stop)) {
                    // Only the ends are storable today; a stop dragged into the
                    // middle is shown but cannot be saved, so it is not silently
                    // written back as if it were.
                    pec->color_start = ramp.stops().front().color;
                    pec->color_end   = ramp.stops().back().color;
                    editor_state.editor_scene->MarkModified();
                }
                if (ramp.size() > 2)
                    ImGui::TextDisabled("(middle stops are preview only — the "
                                        "component stores two colours)");
                ImGui::Separator();

                if (schizo::editor::draw_reflected_fields(
                        pec, *gws::reflect::reflect<schizo::scene::ParticleEmitterComponent>()))
                    editor_state.editor_scene->MarkModified();
                if (ImGui::SmallButton("Reset##emitter")) {
                    *pec = schizo::scene::ParticleEmitterComponent{};
                    editor_state.editor_scene->MarkModified();
                }
            }
            ImGui::Separator();
        }
        if (auto* nac = selected_entity->GetNpcAgentComponent()) {
            if (ImGui::CollapsingHeader("NPC Agent")) {
                // target_name is a std::string, which offset reflection cannot
                // carry, so it needs its own widget -- and it is the field that
                // decides who the agent hunts, so leaving it out would make the
                // rest of the section useless.
                char buf[64];
                std::snprintf(buf, sizeof buf, "%s", nac->target_name.c_str());
                if (ImGui::InputText("target_name", buf, sizeof buf)) {
                    nac->target_name = buf;
                    editor_state.editor_scene->MarkModified();
                }
                if (schizo::editor::draw_reflected_fields(
                        nac, *gws::reflect::reflect<schizo::scene::NpcAgentComponent>()))
                    editor_state.editor_scene->MarkModified();
                if (ImGui::SmallButton("Reset##npcagent")) {
                    *nac = schizo::scene::NpcAgentComponent{};
                    editor_state.editor_scene->MarkModified();
                }
            }
            ImGui::Separator();
        }

        // Rigged character (SkinnedMeshComponent, 3.8). Shown only when the
        // entity is one — an empty section on every rock and crate is noise.
        if (auto* smc = selected_entity->GetSkinnedMeshComponent(); smc && smc->active()) {
            if (ImGui::CollapsingHeader("Skinned Character", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextDisabled("%s", smc->gltf_path.c_str());
                if (ImGui::Checkbox("Playing##skinned", &smc->playing))
                    editor_state.editor_scene->MarkModified();
                if (ImGui::DragFloat("Speed##skinned", &smc->speed, 0.05f, -4.0f, 4.0f))
                    editor_state.editor_scene->MarkModified();
                if (ImGui::DragInt("Clip##skinned", &smc->clip_index, 0.2f, 0, 64))
                    editor_state.editor_scene->MarkModified();
                if (ImGui::SmallButton("Clear rig")) {
                    *smc = schizo::scene::SkinnedMeshComponent{};
                    editor_state.editor_scene->MarkModified();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(drop a rigged .gltf/.glb on the viewport to assign)");
            }
            ImGui::Separator();
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
                    if (ImGui::MenuItem("Area Light")) {
                        selected_entity->AddComponent<schizo::scene::LightComponent>(
                            schizo::scene::LightType::Area,
                            "Area Light"
                        );
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Added Area LightComponent to entity: {}", selected_entity->GetName());
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

                // Audio — one source / one listener per entity (the mixer keys
                // voices by entity; the bridge uses the first active listener).
                {
                    const bool has_src =
                        selected_entity->GetComponent<schizo::scene::AudioSourceComponent>() != nullptr;
                    ImGui::BeginDisabled(has_src);
                    if (ImGui::MenuItem("Audio Source")) {
                        selected_entity->AddComponent<schizo::scene::AudioSourceComponent>();
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Added AudioSourceComponent to entity: {}", selected_entity->GetName());
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndDisabled();

                    const bool has_listener =
                        selected_entity->GetComponent<schizo::scene::AudioListenerComponent>() != nullptr;
                    ImGui::BeginDisabled(has_listener);
                    if (ImGui::MenuItem("Audio Listener")) {
                        selected_entity->AddComponent<schizo::scene::AudioListenerComponent>();
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Added AudioListenerComponent to entity: {}", selected_entity->GetName());
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndDisabled();
                }

                // Terrain — one heightmap landscape per entity.
                {
                    const bool has_terrain =
                        selected_entity->GetComponent<schizo::scene::TerrainComponent>() != nullptr;
                    ImGui::BeginDisabled(has_terrain);
                    if (ImGui::MenuItem("Terrain")) {
                        selected_entity->AddComponent<schizo::scene::TerrainComponent>();
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Added TerrainComponent to entity: {}", selected_entity->GetName());
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndDisabled();
                    if (has_terrain) {
                        ImGui::Separator();
                        ImGui::TextDisabled("(already has Terrain)");
                    }
                }

                // Script — a custom Python/C++/C# behavior (Stage 12).
                {
                    const bool has_script =
                        selected_entity->GetComponent<schizo::scene::ScriptComponent>() != nullptr;
                    ImGui::BeginDisabled(has_script);
                    if (ImGui::MenuItem("Script")) {
                        selected_entity->AddComponent<schizo::scene::ScriptComponent>();
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Added ScriptComponent to entity: {}", selected_entity->GetName());
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndDisabled();
                }

                // Water — an animated water surface (entity Y = water level).
                {
                    const bool has_water =
                        selected_entity->GetComponent<schizo::scene::WaterComponent>() != nullptr;
                    ImGui::BeginDisabled(has_water);
                    if (ImGui::MenuItem("Water")) {
                        selected_entity->AddComponent<schizo::scene::WaterComponent>();
                        editor_state.editor_scene->MarkModified();
                        spdlog::info("Added WaterComponent to entity: {}", selected_entity->GetName());
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndDisabled();
                }

                ImGui::EndPopup();
            }
            
            ImGui::TreePop();
        }

        // ── Audio Source Component ──────────────────────────────────────
        ImGui::Separator();
        auto audio_src = selected_entity->GetComponent<schizo::scene::AudioSourceComponent>();
        if (audio_src) {
            if (ImGui::TreeNode("Audio Source")) {
                // Clip: shows the path; "..." opens a file picker; the field also
                // accepts an AUDIO_ASSET drag-drop from the Asset Browser.
                char clip_buf[512];
                std::snprintf(clip_buf, sizeof(clip_buf), "%s", audio_src->GetClipPath().c_str());
                ImGui::SetNextItemWidth(-70.0f);
                if (ImGui::InputText("Clip##audiosrc", clip_buf, sizeof(clip_buf))) {
                    audio_src->SetClipPath(clip_buf);
                    audio_src->SetClipGuid(clip_buf[0] ? AudioGuidFromPath(clip_buf) : 0);
                    editor_state.editor_scene->MarkModified();
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("AUDIO_ASSET")) {
                        const char* apath = static_cast<const char*>(pl->Data);
                        if (apath && apath[0]) {
                            audio_src->SetClipPath(apath);
                            audio_src->SetClipGuid(AudioGuidFromPath(apath));
                            std::snprintf(clip_buf, sizeof(clip_buf), "%s", apath);
                            editor_state.editor_scene->MarkModified();
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                if (ImGui::Button("...##audiosrc")) {
                    std::string p = OpenAudioDialogNative();
                    if (!p.empty()) {
                        audio_src->SetClipPath(p);
                        audio_src->SetClipGuid(AudioGuidFromPath(p));
                        editor_state.editor_scene->MarkModified();
                    }
                }

                float vol = audio_src->GetVolume();
                if (ImGui::SliderFloat("Volume##audiosrc", &vol, 0.0f, 2.0f)) {
                    audio_src->SetVolume(vol); editor_state.editor_scene->MarkModified();
                }
                float pitch = audio_src->GetPitch();
                if (ImGui::SliderFloat("Pitch##audiosrc", &pitch, 0.1f, 4.0f)) {
                    audio_src->SetPitch(pitch); editor_state.editor_scene->MarkModified();
                }
                float radius = audio_src->GetRadius();
                if (ImGui::SliderFloat("Radius##audiosrc", &radius, 1.0f, 100.0f)) {
                    audio_src->SetRadius(radius); editor_state.editor_scene->MarkModified();
                }
                bool loop = audio_src->IsLooping();
                if (ImGui::Checkbox("Loop##audiosrc", &loop)) {
                    audio_src->SetLooping(loop); editor_state.editor_scene->MarkModified();
                }
                ImGui::SameLine();
                bool spatial = audio_src->IsSpatial();
                if (ImGui::Checkbox("Spatial##audiosrc", &spatial)) {
                    audio_src->SetSpatial(spatial); editor_state.editor_scene->MarkModified();
                }
                ImGui::SameLine();
                bool play_start = audio_src->PlayOnStart();
                if (ImGui::Checkbox("Play on Start##audiosrc", &play_start)) {
                    audio_src->SetPlayOnStart(play_start); editor_state.editor_scene->MarkModified();
                }

                ImGui::Separator();
                const bool playing = audio_src->IsPlaying();
                if (ImGui::Button(playing ? "Stop Preview##audiosrc"
                                          : "Preview##audiosrc", ImVec2(130, 0)))
                    audio_src->SetPlaying(!playing);   // edit-mode audition
                if (audio_src->GetClipPath().empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(no clip assigned)");
                } else if (!audio_src->PlayOnStart() && !playing) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f),
                                       "silent in Play: 'Play on Start' is off");
                }
                ImGui::TreePop();
            }
        }

        // ── Audio Listener Component ────────────────────────────────────
        ImGui::Separator();
        auto audio_listener = selected_entity->GetComponent<schizo::scene::AudioListenerComponent>();
        if (audio_listener) {
            if (ImGui::TreeNode("Audio Listener")) {
                bool active = audio_listener->IsActive();
                if (ImGui::Checkbox("Active##audiolisten", &active)) {
                    audio_listener->SetActive(active); editor_state.editor_scene->MarkModified();
                }
                float gain = audio_listener->GetMasterGain();
                if (ImGui::SliderFloat("Master Volume##audiolisten", &gain, 0.0f, 1.0f)) {
                    audio_listener->SetMasterGain(gain); editor_state.editor_scene->MarkModified();
                }
                ImGui::TextDisabled("The active listener (usually the camera or\n"
                                    "player) sets global pan & attenuation.");
                ImGui::TreePop();
            }
        }

        // ── Terrain Component ───────────────────────────────────────────
        ImGui::Separator();
        auto terrain_comp = selected_entity->GetComponent<schizo::scene::TerrainComponent>();
        if (terrain_comp) {
            if (ImGui::TreeNodeEx("Terrain", ImGuiTreeNodeFlags_DefaultOpen)) {
                int   res  = terrain_comp->GetResolution();
                float size = terrain_comp->GetSize();
                ImGui::Text("Grid: %d x %d cells over %.0f m", res, res, size);
                if (ImGui::SliderInt("Resolution##terrain", &res, 8, 1024)) {
                    terrain_comp->Resize(res, terrain_comp->GetSize());
                    editor_state.editor_scene->MarkModified();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Changing resolution or size resets the heightmap to flat.\n"
                                      "Large terrains are meshed in 64-cell chunks — sculpting\n"
                                      "only rebuilds the chunks the brush touches.");
                if (ImGui::SliderFloat("Size (m)##terrain", &size, 4.0f, 8000.0f,
                                       "%.0f", ImGuiSliderFlags_Logarithmic)) {
                    terrain_comp->Resize(terrain_comp->GetResolution(), size);
                    editor_state.editor_scene->MarkModified();
                }
                float hs = terrain_comp->GetHeightScale();
                if (ImGui::SliderFloat("Height Scale##terrain", &hs, 0.1f, 10.0f)) {
                    terrain_comp->SetHeightScale(hs);
                    editor_state.editor_scene->MarkModified();
                }
                if (ImGui::Button("Flatten##terrain")) {
                    terrain_comp->Flatten(0.0f);
                    editor_state.editor_scene->MarkModified();
                }

                ImGui::Separator();
                if (ImGui::Checkbox("Sculpt Mode##terrain", &editor_state.terrain_sculpt_active)) {
                    if (editor_state.terrain_sculpt_active) editor_state.terrain_paint_active = false;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(Left-drag over terrain)");
                const char* brushes[] = { "Raise", "Lower", "Smooth", "Flatten",
                                          "Dig Hole", "Fill Hole" };
                ImGui::Combo("Brush##terrain", &editor_state.terrain_brush_mode, brushes, 6);
                if (editor_state.terrain_brush_mode >= 4)
                    ImGui::TextDisabled("Holes carve the surface + collision —\n"
                                        "build caves (or underwater entrances) through them.");
                ImGui::SliderFloat("Radius##terrain",   &editor_state.terrain_brush_radius,   0.5f, 50.0f);
                ImGui::SliderFloat("Strength##terrain", &editor_state.terrain_brush_strength, 0.01f, 5.0f);
                ImGui::SliderFloat("Falloff##terrain",  &editor_state.terrain_brush_falloff,  0.0f, 1.0f);

                // ── Texture splat painting (Phase C) ──
                ImGui::Separator();
                if (ImGui::Checkbox("Paint Mode##terrain", &editor_state.terrain_paint_active)) {
                    if (editor_state.terrain_paint_active) editor_state.terrain_sculpt_active = false;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(Left-drag paints active layer)");

                ImGui::Text("Active layer:");
                ImGui::SameLine();
                for (int i = 0; i < schizo::scene::kTerrainLayers; ++i) {
                    char rb[16]; std::snprintf(rb, sizeof rb, "%d##tpl", i);
                    if (i) ImGui::SameLine();
                    ImGui::RadioButton(rb, &editor_state.terrain_paint_layer, i);
                }
                ImGui::SliderFloat("Paint Strength##terrain", &editor_state.terrain_paint_strength, 0.01f, 1.0f);
                ImGui::SliderFloat("Paint Radius##terrain",   &editor_state.terrain_brush_radius,   0.5f, 50.0f);

                // Per-layer texture path + tiling. Char buffers re-synced from
                // the component when the inspected entity changes; the path is
                // committed only when the field loses focus (avoids reloading a
                // texture on every keystroke).
                static uint32_t terr_synced_id = 0xFFFFFFFFu;
                static char     terr_layer_buf[schizo::scene::kTerrainLayers][260];
                const uint32_t  terr_eid = selected_entity->GetId();
                if (terr_synced_id != terr_eid) {
                    for (int i = 0; i < schizo::scene::kTerrainLayers; ++i)
                        std::snprintf(terr_layer_buf[i], sizeof terr_layer_buf[i],
                                      "%s", terrain_comp->GetLayerPath(i).c_str());
                    terr_synced_id = terr_eid;
                }
                for (int i = 0; i < schizo::scene::kTerrainLayers; ++i) {
                    ImGui::PushID(i);
                    char lbl[32]; std::snprintf(lbl, sizeof lbl, "Layer %d##path", i);
                    ImGui::InputText(lbl, terr_layer_buf[i], sizeof terr_layer_buf[i]);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        terrain_comp->SetLayerPath(i, terr_layer_buf[i]);
                        editor_state.editor_scene->MarkModified();
                    }
                    // Drag a texture from the Asset Browser onto the layer.
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("TEXTURE_ASSET")) {
                            const char* tp = static_cast<const char*>(pl->Data);
                            if (tp && tp[0]) {
                                terrain_comp->SetLayerPath(i, tp);
                                std::snprintf(terr_layer_buf[i], sizeof terr_layer_buf[i], "%s", tp);
                                editor_state.editor_scene->MarkModified();
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    float t = terrain_comp->GetTiling(i);
                    if (ImGui::SliderFloat("tiling", &t, 1.0f, 128.0f)) {
                        terrain_comp->SetTiling(i, t);
                        editor_state.editor_scene->MarkModified();
                    }
                    ImGui::PopID();
                }
                if (ImGui::Button("Clear Painting##terrain")) {
                    terrain_comp->ResizeSplat(terrain_comp->SplatResolution());
                    editor_state.editor_scene->MarkModified();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(resets to Layer 0)");

                // ── Integrated water (Unreal-style: part of the terrain) ──
                ImGui::Separator();
                bool twater = terrain_comp->IsWaterEnabled();
                if (ImGui::Checkbox("Water##terrain", &twater)) {
                    terrain_comp->SetWaterEnabled(twater);
                    editor_state.editor_scene->MarkModified();
                }
                if (twater) {
                    ImGui::SameLine();
                    bool tphys = terrain_comp->IsWaterPhysical();
                    if (ImGui::Checkbox("Physical##terrainwater", &tphys)) {
                        terrain_comp->SetWaterPhysical(tphys);
                        editor_state.editor_scene->MarkModified();
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Physical: buoyancy + swimming in Play mode.\n"
                                          "Unchecked: visual only.");
                    float tlvl = terrain_comp->GetWaterLevel();
                    if (ImGui::SliderFloat("Water Level##terrain", &tlvl, -50.0f, 100.0f)) {
                        terrain_comp->SetWaterLevel(tlvl);
                        editor_state.editor_scene->MarkModified();
                    }
                    glm::vec3 tdc = terrain_comp->GetWaterDeepColor();
                    if (ImGui::ColorEdit3("Deep##terrainwater", &tdc.x)) {
                        terrain_comp->SetWaterDeepColor(tdc);
                        editor_state.editor_scene->MarkModified();
                    }
                    glm::vec3 tsc = terrain_comp->GetWaterShallowColor();
                    if (ImGui::ColorEdit3("Shallow##terrainwater", &tsc.x)) {
                        terrain_comp->SetWaterShallowColor(tsc);
                        editor_state.editor_scene->MarkModified();
                    }
                    float twh = terrain_comp->GetWaterWaveHeight();
                    if (ImGui::SliderFloat("Wave Height##terrainwater", &twh, 0.0f, 2.0f)) {
                        terrain_comp->SetWaterWaveHeight(twh);
                        editor_state.editor_scene->MarkModified();
                    }
                    float tws = terrain_comp->GetWaterWaveSpeed();
                    if (ImGui::SliderFloat("Wave Speed##terrainwater", &tws, 0.0f, 5.0f)) {
                        terrain_comp->SetWaterWaveSpeed(tws);
                        editor_state.editor_scene->MarkModified();
                    }
                    float twsc = terrain_comp->GetWaterWaveScale();
                    if (ImGui::SliderFloat("Wave Length##terrainwater", &twsc, 0.5f, 100.0f)) {
                        terrain_comp->SetWaterWaveScale(twsc);
                        editor_state.editor_scene->MarkModified();
                    }
                    float tcl = terrain_comp->GetWaterClarity();
                    if (ImGui::SliderFloat("Clarity (m)##terrainwater", &tcl, 0.05f, 20.0f)) {
                        terrain_comp->SetWaterClarity(tcl);
                        editor_state.editor_scene->MarkModified();
                    }
                    float trf = terrain_comp->GetWaterReflectivity();
                    if (ImGui::SliderFloat("Reflectivity##terrainwater", &trf, 0.0f, 1.0f)) {
                        terrain_comp->SetWaterReflectivity(trf);
                        editor_state.editor_scene->MarkModified();
                    }
                }
                ImGui::TreePop();
            }
        }

        // ── Script Component (Stage 12: custom scripts) ────────────────
        ImGui::Separator();
        auto script_comp = selected_entity->GetComponent<schizo::scene::ScriptComponent>();
        if (script_comp) {
            if (ImGui::TreeNodeEx("Script", ImGuiTreeNodeFlags_DefaultOpen)) {
                // Path field — synced per selected entity, committed on defocus.
                static uint32_t scr_synced_id = 0xFFFFFFFFu;
                static char     scr_path_buf[260];
                if (scr_synced_id != selected_entity->GetId()) {
                    std::snprintf(scr_path_buf, sizeof scr_path_buf, "%s",
                                  script_comp->GetScriptPath().c_str());
                    scr_synced_id = selected_entity->GetId();
                }
                ImGui::InputText("Script File##script", scr_path_buf, sizeof scr_path_buf);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    script_comp->SetScriptPath(scr_path_buf);
                    editor_state.script_system.force_reload(selected_entity->GetId());
                    editor_state.editor_scene->MarkModified();
                }
                // Drag a script file from the Asset Browser onto the field.
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("SCRIPT_ASSET")) {
                        const char* sp = static_cast<const char*>(pl->Data);
                        if (sp && sp[0]) {
                            script_comp->SetScriptPath(sp);
                            std::snprintf(scr_path_buf, sizeof scr_path_buf, "%s", sp);
                            editor_state.script_system.force_reload(selected_entity->GetId());
                            editor_state.editor_scene->MarkModified();
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::TextDisabled(".py = Python, .cpp = C++ (g++), .cs = C# (.NET)\n"
                                    "hooks: on_start(e), on_update(e, dt)");

                bool s_enabled = script_comp->IsEnabled();
                if (ImGui::Checkbox("Enabled##script", &s_enabled)) {
                    script_comp->SetEnabled(s_enabled);
                    editor_state.editor_scene->MarkModified();
                }
                ImGui::SameLine();
                if (ImGui::Button("Reload##script")) {
                    editor_state.script_system.force_reload(selected_entity->GetId());
                    script_comp->SetStatus("");
                }

                // Runtime status from the ScriptSystem (errors in red).
                const std::string& st = script_comp->GetStatus();
                if (!st.empty()) {
                    const bool ok = (st == "running");
                    ImGui::TextColored(ok ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f)
                                          : ImVec4(0.95f, 0.4f, 0.35f, 1.0f),
                                       "%s", st.c_str());
                } else {
                    ImGui::TextDisabled("(runs in Play mode; edits hot-reload live)");
                }

                // ── Public fields ──────────────────────────────────────────
                // Auto-discovered from module-level variables (e.g. `speed = 5.0`
                // in a .py) AND `# @param name type default` comments. Edited
                // here; the values are pushed into the running Python script's
                // globals on start (and readable in any language via
                // engine.get_param_*). Always shown so there's feedback even when
                // there are no fields. Widgets commit on change (re-seeded from
                // the stored override each frame — same pattern as the Sky HDR).
                ImGui::SeparatorText("Fields");
                {
                    namespace fs = std::filesystem;
                    const std::string spath = script_comp->GetScriptPath();
                    std::error_code pec;
                    const bool file_ok = !spath.empty() && fs::exists(spath, pec);
                    std::vector<schizo::editor::ScriptParam> params;
                    if (file_ok) params = schizo::editor::scan_script_params(spath);

                    if (spath.empty()) {
                        ImGui::TextDisabled("Assign a script file to expose its fields.");
                    } else if (!file_ok) {
                        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.0f),
                                           "Script not found (relative to project):\n  %s", spath.c_str());
                    } else if (params.empty()) {
                        ImGui::TextDisabled("No public fields. Add a top-level variable\n"
                                            "(e.g.  speed = 5.0 ), or  # @param name type default.");
                    } else {
                        for (const auto& pr : params) {
                            const std::string* ov = script_comp->FindParam(pr.name);
                            const std::string cur = ov ? *ov : pr.def;
                            const std::string label =
                                (pr.label.empty() ? pr.name : pr.label) + "##fld_" + pr.name;
                            if (pr.type == "float") {
                                float v = std::strtof(cur.c_str(), nullptr);
                                if (ImGui::DragFloat(label.c_str(), &v, 0.1f)) {
                                    char b[64]; std::snprintf(b, sizeof b, "%g", v);
                                    script_comp->SetParam(pr.name, b);
                                    editor_state.editor_scene->MarkModified();
                                }
                            } else if (pr.type == "int") {
                                int v = std::atoi(cur.c_str());
                                if (ImGui::DragInt(label.c_str(), &v)) {
                                    script_comp->SetParam(pr.name, std::to_string(v));
                                    editor_state.editor_scene->MarkModified();
                                }
                            } else if (pr.type == "bool") {
                                bool v = (cur == "1" || cur == "true" || cur == "True");
                                if (ImGui::Checkbox(label.c_str(), &v)) {
                                    script_comp->SetParam(pr.name, v ? "1" : "0");
                                    editor_state.editor_scene->MarkModified();
                                }
                            } else {  // string
                                char b[256]; std::snprintf(b, sizeof b, "%s", cur.c_str());
                                if (ImGui::InputText(label.c_str(), b, sizeof b)) {
                                    script_comp->SetParam(pr.name, b);
                                    editor_state.editor_scene->MarkModified();
                                }
                            }
                        }
                        if (ImGui::SmallButton("Reset to defaults##scriptfields")) {
                            script_comp->ClearParams();
                            editor_state.editor_scene->MarkModified();
                        }
                    }
                }
                ImGui::TreePop();
            }
        }

        // ── Water Component ─────────────────────────────────────────────
        ImGui::Separator();
        auto water_comp = selected_entity->GetComponent<schizo::scene::WaterComponent>();
        if (water_comp) {
            if (ImGui::TreeNodeEx("Water", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextDisabled("Entity Y position = water level");
                bool wphys = water_comp->IsPhysical();
                if (ImGui::Checkbox("Physical##water", &wphys)) {
                    water_comp->SetPhysical(wphys);
                    editor_state.editor_scene->MarkModified();
                }
                ImGui::SameLine();
                ImGui::TextDisabled(wphys ? "(buoyancy + swimming in Play)"
                                          : "(visual only)");
                glm::vec2 wsz = water_comp->GetSize();
                if (ImGui::DragFloat2("Size (m)##water", &wsz.x, 1.0f, 1.0f, 4000.0f)) {
                    water_comp->SetSize(wsz); editor_state.editor_scene->MarkModified();
                }
                glm::vec3 dc = water_comp->GetDeepColor();
                if (ImGui::ColorEdit3("Deep Color##water", &dc.x)) {
                    water_comp->SetDeepColor(dc); editor_state.editor_scene->MarkModified();
                }
                glm::vec3 shc = water_comp->GetShallowColor();
                if (ImGui::ColorEdit3("Shallow Color##water", &shc.x)) {
                    water_comp->SetShallowColor(shc); editor_state.editor_scene->MarkModified();
                }
                float wh = water_comp->GetWaveHeight();
                if (ImGui::SliderFloat("Wave Height##water", &wh, 0.0f, 2.0f)) {
                    water_comp->SetWaveHeight(wh); editor_state.editor_scene->MarkModified();
                }
                float ws = water_comp->GetWaveSpeed();
                if (ImGui::SliderFloat("Wave Speed##water", &ws, 0.0f, 5.0f)) {
                    water_comp->SetWaveSpeed(ws); editor_state.editor_scene->MarkModified();
                }
                float wsc = water_comp->GetWaveScale();
                if (ImGui::SliderFloat("Wave Length##water", &wsc, 0.5f, 100.0f)) {
                    water_comp->SetWaveScale(wsc); editor_state.editor_scene->MarkModified();
                }
                float cl = water_comp->GetClarity();
                if (ImGui::SliderFloat("Clarity (m)##water", &cl, 0.05f, 20.0f)) {
                    water_comp->SetClarity(cl); editor_state.editor_scene->MarkModified();
                }
                float rf = water_comp->GetReflectivity();
                if (ImGui::SliderFloat("Reflectivity##water", &rf, 0.0f, 1.0f)) {
                    water_comp->SetReflectivity(rf); editor_state.editor_scene->MarkModified();
                }
                ImGui::TreePop();
            }
        }

        // Light Component Properties
        ImGui::Separator();
        auto light_comp = selected_entity->GetComponent<schizo::scene::LightComponent>();
        if (light_comp) {
            if (ImGui::TreeNode("Light")) {
                // Light type display
                const char* light_type_str;
                switch (light_comp->GetType()) {
                    case schizo::scene::LightType::Directional: light_type_str = "Directional (Sun)"; break;
                    case schizo::scene::LightType::Point:       light_type_str = "Point Light";       break;
                    case schizo::scene::LightType::Spot:        light_type_str = "Spot Light";        break;
                    case schizo::scene::LightType::Area:        light_type_str = "Area Light (rect)"; break;
                    default:                                    light_type_str = "Light";             break;
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

                        // Cookie / gobo — a texture projected through the cone.
                        char ck_buf[512];
                        std::snprintf(ck_buf, sizeof(ck_buf), "%s",
                                      light_comp->GetCookiePath().c_str());
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::InputTextWithHint("Cookie##spot", "texture path (drag from Asset Browser)",
                                                     ck_buf, sizeof(ck_buf))) {
                            light_comp->SetCookiePath(ck_buf);
                            editor_state.editor_scene->MarkModified();
                        }
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("TEXTURE_ASSET")) {
                                const char* tpath = static_cast<const char*>(pl->Data);
                                if (tpath && tpath[0]) {
                                    light_comp->SetCookiePath(tpath);
                                    editor_state.editor_scene->MarkModified();
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }
                        if (!light_comp->GetCookiePath().empty() &&
                            ImGui::SmallButton("Clear Cookie##spot")) {
                            light_comp->SetCookiePath("");
                            editor_state.editor_scene->MarkModified();
                        }
                    }

                    if (light_comp->GetType() == schizo::scene::LightType::Area) {
                        ImGui::Separator();
                        ImGui::Text("Area Light (rectangle)");
                        glm::vec2 sz = light_comp->GetAreaSize();
                        if (ImGui::SliderFloat2("Size W,H##area", &sz.x, 0.1f, 20.0f)) {
                            light_comp->SetAreaSize(sz);
                            editor_state.editor_scene->MarkModified();
                        }
                        bool two = light_comp->IsTwoSided();
                        if (ImGui::Checkbox("Two-sided##area", &two)) {
                            light_comp->SetTwoSided(two);
                            editor_state.editor_scene->MarkModified();
                        }
                        ImGui::TextDisabled("Emits along the entity's forward (-Z);\norient it with the Transform rotation.");
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
            
            // Drag-drop target for mesh assignment — import into project + assign.
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MESH_ASSET")) {
                    const char* dropped = static_cast<const char*>(payload->Data);
                    if (dropped && dropped[0] && mesh_comp) {
                        const std::string proj = schizo::editor::import_asset_into_project(dropped, "models");
                        if (!proj.empty()) {
                            mesh_comp->SetMesh(proj);
                            editor_state.editor_scene->MarkModified();
                            editor_state.set_status("Applied mesh: " + proj);
                        } else {
                            editor_state.set_status(std::string("Mesh not found on disk: ") + dropped);
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

        // ---- Gameplay Components (authoritative ECS, reflection-driven) ----
        // Every authorable ECS component (Health, Ability State, …) renders here
        // generically — no per-component UI code. This is the ECS-authoritative
        // path for gameplay state (F1/F2).
        ImGui::Separator();
        if (ImGui::TreeNodeEx("Gameplay Components (ECS)", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (editor_state.ecs_bridge && selected_entity) {
                auto* inspect_tf = selected_entity->GetTransform();
                if (schizo::editor::draw_ecs_component_inspector(
                        *editor_state.ecs_bridge, inspect_tf,
                        &editor_state.field_edits,
                        [&editor_state](const schizo::editor::CoalescedEdit& edit) {
                            PushFieldEditCommand(editor_state, edit);
                        }))
                    editor_state.editor_scene->MarkModified();

                // Save this entity's gameplay components as a reusable prefab (F4).
                ImGui::Dummy(ImVec2(0, 4));
                if (ImGui::Button("Save as Prefab")) {
                    std::error_code ec; std::filesystem::create_directories("prefabs", ec);
                    const std::string path = "prefabs/" + selected_entity->GetName() + ".prefab";
                    if (editor_state.ecs_bridge->save_entity_prefab(selected_entity->GetTransform(), path))
                        editor_state.set_status("Saved prefab: " + path);
                    else
                        editor_state.set_status("Nothing to save — add a gameplay component first.");
                }
            } else {
                ImGui::TextDisabled("ECS bridge unavailable.");
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

                // Albedo (base-colour) texture. Type a path or drag a texture
                // from the Asset Browser. Loaded through the TextureManager
                // (mipped, sRGB, deduplicated, hot-reloadable); `Color` above
                // multiplies the sampled texel. Empty = flat colour only.
                {
                    static char mr_albedo_buf[260] = {0};
                    static uint32_t mr_albedo_synced_id = 0xFFFFFFFFu;
                    if (mr_albedo_synced_id != selected_entity->GetId()) {
                        std::snprintf(mr_albedo_buf, sizeof mr_albedo_buf, "%s",
                                      mr->GetAlbedoTexturePath().c_str());
                        mr_albedo_synced_id = selected_entity->GetId();
                    }
                    ImGui::Text("Albedo Texture:");
                    ImGui::InputText("##mr_albedo", mr_albedo_buf, sizeof mr_albedo_buf);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        mr->SetAlbedoTexturePath(mr_albedo_buf);
                        editor_state.editor_scene->MarkModified();
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("TEXTURE_ASSET")) {
                            const char* tp = static_cast<const char*>(pl->Data);
                            if (tp && tp[0]) {
                                mr->SetAlbedoTexturePath(tp);
                                std::snprintf(mr_albedo_buf, sizeof mr_albedo_buf, "%s", tp);
                                editor_state.editor_scene->MarkModified();
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    if (mr->HasAlbedoTexture()) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Clear##mr_albedo")) {
                            mr->SetAlbedoTexturePath("");
                            mr_albedo_buf[0] = '\0';
                            editor_state.editor_scene->MarkModified();
                        }
                    }
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
            PushDeleteEntityCommand(editor_state, scene, selected_entity);
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
    
    ImGui::Begin("Viewport", &editor_state.show_viewport, ImGuiWindowFlags_NoMove);  // docked window = child; End() must always run
    {
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
            if (viewport_playing) EndPlayMode(editor_state, scene);
            else                  BeginPlayMode(editor_state, scene);
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

            // ---- Terrain sculpting (Phase A) ----
            // Sculpt mode + selected terrain: ray-march the mouse ray onto the
            // heightmap, draw a brush ring, and (while LMB held) apply the brush.
            editor_state.terrain_brush_valid = false;
            // End of a sculpt stroke: mouse released (or sculpt mode turned
            // off mid-stroke, which must still commit -- the edit happened).
            if (editor_state.terrain_stroke_active &&
                (!ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                 !editor_state.terrain_sculpt_active)) {
                PushTerrainStrokeCommand(editor_state, scene,
                                         editor_state.terrain_stroke_entity,
                                         editor_state.terrain_stroke_heights,
                                         editor_state.terrain_stroke_holes);
                editor_state.terrain_stroke_active = false;
                editor_state.terrain_stroke_entity = 0;
                // Release the snapshot: it is the largest thing this state holds.
                editor_state.terrain_stroke_heights.clear();
                editor_state.terrain_stroke_heights.shrink_to_fit();
                editor_state.terrain_stroke_holes.clear();
                editor_state.terrain_stroke_holes.shrink_to_fit();
            }

            if (editor_state.terrain_sculpt_active && scene && image_drawn &&
                editor_state.selected_entity_id != 0) {
                auto sel = scene->GetEntityById(editor_state.selected_entity_id);
                auto tc  = sel ? sel->GetComponent<schizo::scene::TerrainComponent>() : nullptr;
                if (tc) {
                    const float vx = io.MousePos.x - image_min.x;
                    const float vy = io.MousePos.y - image_min.y;
                    if (vx >= 0 && vy >= 0 && vx < viewport_size.x && vy < viewport_size.y) {
                        auto [ro, rd] = editor_state.viewport_camera.GetPickingRay(
                            vx, vy, viewport_size.x, viewport_size.y);
                        const glm::vec3 base = sel->GetTransform()->GetWorldPosition();
                        const float half = tc->GetSize() * 0.5f;
                        const float cell = tc->CellSize();
                        const float step = std::max(0.2f, cell * 0.5f);
                        const float maxT = tc->GetSize() * 4.0f + 200.0f;
                        bool hit = false; glm::vec3 hitp(0.0f);
                        for (float t = 0.0f; t < maxT; t += step) {
                            glm::vec3 lp = (ro + rd * t) - base;
                            if (lp.x < -half || lp.x > half || lp.z < -half || lp.z > half) continue;
                            if (lp.y <= tc->SampleHeightLocal(lp.x, lp.z)) {
                                hit = true; hitp = ro + rd * t; break;
                            }
                        }
                        if (hit) {
                            editor_state.terrain_brush_hit   = hitp;
                            editor_state.terrain_brush_valid = true;

                            // Brush ring overlay (flat circle at the hit).
                            auto to_scr = [&](glm::vec3 w) -> ImVec2 {
                                glm::mat4 p = proj_matrix; p[1][1] *= -1.0f;
                                glm::vec4 c = p * view_matrix * glm::vec4(w, 1.0f);
                                if (c.w <= 0.0f) return ImVec2(-1e9f, -1e9f);
                                glm::vec3 ndc = glm::vec3(c) / c.w;
                                return ImVec2(image_min.x + (ndc.x * 0.5f + 0.5f) * viewport_size.x,
                                              image_min.y + (ndc.y * 0.5f + 0.5f) * viewport_size.y);
                            };
                            ImDrawList* dl = ImGui::GetWindowDrawList();
                            ImVec2 prev; bool first = true;
                            for (int s = 0; s <= 32; ++s) {
                                float a = (float)s / 32.0f * 6.2831853f;
                                ImVec2 sp = to_scr(hitp + glm::vec3(std::cos(a), 0.0f, std::sin(a)) *
                                                              editor_state.terrain_brush_radius);
                                if (!first && sp.x > -1e8f && prev.x > -1e8f)
                                    dl->AddLine(prev, sp, IM_COL32(255, 220, 80, 220), 1.5f);
                                prev = sp; first = false;
                            }

                            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                                // First frame of the stroke: remember the whole
                                // heightmap. Transient and small; only the
                                // changed rect is kept when the stroke ends.
                                if (!editor_state.terrain_stroke_active) {
                                    editor_state.terrain_stroke_active = true;
                                    editor_state.terrain_stroke_entity = sel->GetId();
                                    editor_state.terrain_stroke_heights = tc->Heights();
                                    const int hres = tc->GetResolution();
                                    editor_state.terrain_stroke_holes.assign(
                                        static_cast<size_t>(hres) * hres, 0);
                                    for (int hz = 0; hz < hres; ++hz)
                                        for (int hx = 0; hx < hres; ++hx)
                                            editor_state.terrain_stroke_holes[
                                                static_cast<size_t>(hz) * hres + hx] =
                                                    tc->HasHole(hx, hz) ? 1u : 0u;
                                }
                                const float lx = hitp.x - base.x;
                                const float lz = hitp.z - base.z;
                                const float R  = editor_state.terrain_brush_radius;
                                const int   res = tc->GetResolution();
                                const int   n   = tc->VertsPerSide();
                                const float dt  = io.DeltaTime > 0.0f ? io.DeltaTime : 0.016f;
                                const float scale = tc->GetHeightScale() > 1e-3f ? tc->GetHeightScale() : 1.0f;
                                const float flat_target = tc->SampleHeightLocal(lx, lz) / scale;
                                const int cx = (int)std::floor((lx + half) / cell);
                                const int cz = (int)std::floor((lz + half) / cell);
                                const int rc = (int)std::ceil(R / cell) + 1;
                                bool changed = false;
                                if (editor_state.terrain_brush_mode >= 4) {
                                    // Hole brushes operate on CELLS: 4 = dig
                                    // (carve mesh + collision), 5 = fill.
                                    const bool dig = editor_state.terrain_brush_mode == 4;
                                    for (int iz = std::max(0, cz - rc); iz < std::min(res, cz + rc + 1); ++iz)
                                    for (int ix = std::max(0, cx - rc); ix < std::min(res, cx + rc + 1); ++ix) {
                                        const float ccx = -half + (ix + 0.5f) * cell;
                                        const float ccz = -half + (iz + 0.5f) * cell;
                                        const float d = std::sqrt((ccx - lx) * (ccx - lx) + (ccz - lz) * (ccz - lz));
                                        if (d > R) continue;
                                        if (tc->HasHole(ix, iz) != dig) {
                                            tc->SetHole(ix, iz, dig);
                                            changed = true;
                                        }
                                    }
                                } else {
                                    auto& H = tc->MutableHeights();
                                    for (int iz = std::max(0, cz - rc); iz <= std::min(res, cz + rc); ++iz)
                                    for (int ix = std::max(0, cx - rc); ix <= std::min(res, cx + rc); ++ix) {
                                        const float vxw = -half + ix * cell;
                                        const float vzw = -half + iz * cell;
                                        const float d = std::sqrt((vxw - lx) * (vxw - lx) + (vzw - lz) * (vzw - lz));
                                        if (d > R) continue;
                                        const float fall = std::pow(1.0f - d / R,
                                            0.5f + editor_state.terrain_brush_falloff * 2.0f);
                                        const float rate = editor_state.terrain_brush_strength;
                                        float& h = H[(size_t)iz * n + ix];
                                        switch (editor_state.terrain_brush_mode) {
                                            case 0: h += rate * fall * dt * 12.0f; break;  // raise
                                            case 1: h -= rate * fall * dt * 12.0f; break;  // lower
                                            case 2: { float avg = (tc->HeightAt(ix-1,iz) + tc->HeightAt(ix+1,iz)
                                                                 + tc->HeightAt(ix,iz-1) + tc->HeightAt(ix,iz+1)) * 0.25f;
                                                      h += (avg - h) * std::min(1.0f, rate * fall * dt * 6.0f); break; }
                                            case 3: h += (flat_target - h) * std::min(1.0f, rate * fall * dt * 6.0f); break;
                                        }
                                        changed = true;
                                    }
                                }
                                if (changed) {
                                    // Report the brushed rect so the chunked mesh
                                    // cache rebuilds only the touched chunks.
                                    tc->MarkDirtyRect(cx - rc, cz - rc, cx + rc, cz + rc);
                                    editor_state.editor_scene->MarkModified();
                                }
                            }
                        }
                    }
                }
            }

            // ---- Terrain texture painting (Phase C) ----
            // Same ray-march + ring as sculpting, but LMB paints the active
            // layer's weight into the splatmap.
            if (editor_state.terrain_paint_active && scene && image_drawn &&
                editor_state.selected_entity_id != 0) {
                auto sel = scene->GetEntityById(editor_state.selected_entity_id);
                auto tc  = sel ? sel->GetComponent<schizo::scene::TerrainComponent>() : nullptr;
                if (tc) {
                    const float vx = io.MousePos.x - image_min.x;
                    const float vy = io.MousePos.y - image_min.y;
                    if (vx >= 0 && vy >= 0 && vx < viewport_size.x && vy < viewport_size.y) {
                        auto [ro, rd] = editor_state.viewport_camera.GetPickingRay(
                            vx, vy, viewport_size.x, viewport_size.y);
                        const glm::vec3 base = sel->GetTransform()->GetWorldPosition();
                        const float half = tc->GetSize() * 0.5f;
                        const float cell = tc->CellSize();
                        const float step = std::max(0.2f, cell * 0.5f);
                        const float maxT = tc->GetSize() * 4.0f + 200.0f;
                        bool hit = false; glm::vec3 hitp(0.0f);
                        for (float t = 0.0f; t < maxT; t += step) {
                            glm::vec3 lp = (ro + rd * t) - base;
                            if (lp.x < -half || lp.x > half || lp.z < -half || lp.z > half) continue;
                            if (lp.y <= tc->SampleHeightLocal(lp.x, lp.z)) {
                                hit = true; hitp = ro + rd * t; break;
                            }
                        }
                        if (hit) {
                            editor_state.terrain_brush_hit   = hitp;
                            editor_state.terrain_brush_valid = true;
                            auto to_scr = [&](glm::vec3 w) -> ImVec2 {
                                glm::mat4 p = proj_matrix; p[1][1] *= -1.0f;
                                glm::vec4 c = p * view_matrix * glm::vec4(w, 1.0f);
                                if (c.w <= 0.0f) return ImVec2(-1e9f, -1e9f);
                                glm::vec3 ndc = glm::vec3(c) / c.w;
                                return ImVec2(image_min.x + (ndc.x * 0.5f + 0.5f) * viewport_size.x,
                                              image_min.y + (ndc.y * 0.5f + 0.5f) * viewport_size.y);
                            };
                            ImDrawList* dl = ImGui::GetWindowDrawList();
                            ImVec2 prev; bool first = true;
                            for (int s = 0; s <= 32; ++s) {
                                float a = (float)s / 32.0f * 6.2831853f;
                                ImVec2 sp = to_scr(hitp + glm::vec3(std::cos(a), 0.0f, std::sin(a)) *
                                                              editor_state.terrain_brush_radius);
                                if (!first && sp.x > -1e8f && prev.x > -1e8f)
                                    dl->AddLine(prev, sp, IM_COL32(80, 200, 255, 220), 1.5f);
                                prev = sp; first = false;
                            }
                            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                                const float dt = io.DeltaTime > 0.0f ? io.DeltaTime : 0.016f;
                                const float amount = editor_state.terrain_paint_strength *
                                                     std::min(1.0f, dt * 8.0f);
                                tc->PaintSplatLocal(hitp.x - base.x, hitp.z - base.z,
                                                    editor_state.terrain_brush_radius,
                                                    editor_state.terrain_paint_layer,
                                                    amount, editor_state.terrain_brush_falloff);
                                editor_state.editor_scene->MarkModified();
                            }
                        }
                    }
                }
            }

            // Handle entity selection through picking and gizmo interaction
            // (suppressed while sculpting/painting so a click edits terrain, not selection).
            if (!editor_state.terrain_sculpt_active && !editor_state.terrain_paint_active &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) && scene && image_drawn) {
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
                                // Remember where this drag started, for undo.
                                editor_state.gizmo_undo_entity = 0;
                                if (auto gsc = editor_state.editor_scene->GetScene()) {
                                    if (auto ge = FindEntityById(gsc, editor_state.selected_entity_id)) {
                                        if (auto* gt = ge->GetTransform()) {
                                            editor_state.gizmo_undo_entity   = editor_state.selected_entity_id;
                                            editor_state.gizmo_undo_position = gt->GetLocalPosition();
                                            editor_state.gizmo_undo_rotation = gt->GetLocalRotation();
                                            editor_state.gizmo_undo_scale    = gt->GetLocalScale();
                                        }
                                    }
                                }
                                
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
                        editor_state.gizmo_drag_origin =
                            selected_transform->GetLocalPosition();
                    }
                    
                    // Get mode-specific updates
                    auto gmode = editor_state.transform_gizmo.GetMode();
                    if (gmode == schizo::editor::GizmoMode::Translate) {
                        glm::vec3 current_pos = selected_transform->GetLocalPosition();
                        glm::vec3 new_pos = editor_state.transform_gizmo.UpdateDrag(current_mouse_glm, current_pos);

                        // Snapping (4.7). Ctrl enables it for this drag even
                        // when the toggle is off -- holding a key beats a mode
                        // you can forget you are in.
                        const bool snap_now =
                            editor_state.snap.enabled || ImGui::GetIO().KeyCtrl;
                        if (snap_now) {
                            const float step = editor_state.snap.translate;
                            new_pos = editor_state.snap.relative
                                ? schizo::editor::snap_position_relative(
                                      new_pos, editor_state.gizmo_drag_origin, step)
                                : schizo::editor::snap_position(new_pos, step);
                        }
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
                            glm::quat out = q * selected_transform->GetLocalRotation();
                            if (editor_state.snap.enabled || ImGui::GetIO().KeyCtrl)
                                out = schizo::editor::snap_rotation(
                                    out, editor_state.snap.rotate_deg);
                            selected_transform->SetLocalRotation(out);
                        }
                    } else if (gmode == schizo::editor::GizmoMode::Scale) {
                        glm::vec3 current_scale = selected_transform->GetLocalScale();
                        glm::vec3 new_scale = editor_state.transform_gizmo.UpdateDrag(current_mouse_glm, current_scale);
                        if (editor_state.snap.enabled || ImGui::GetIO().KeyCtrl)
                            new_scale = schizo::editor::snap_scale(
                                new_scale, editor_state.snap.scale);
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
                    // One undo entry for the whole drag.
                    if (editor_state.gizmo_undo_entity) {
                        PushGizmoTransformCommand(
                            editor_state, editor_state.editor_scene->GetScene(),
                            editor_state.gizmo_undo_entity,
                            editor_state.gizmo_undo_position,
                            editor_state.gizmo_undo_rotation,
                            editor_state.gizmo_undo_scale);
                        editor_state.gizmo_undo_entity = 0;
                    }
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
                    const char* dropped = (const char*)mesh_payload->Data;
                    if (scene && editor_state.selected_entity_id != 0) {
                        auto entity = scene->GetEntityById(editor_state.selected_entity_id);
                        if (entity && dropped && dropped[0]) {
                            const std::string proj = schizo::editor::import_asset_into_project(dropped, "models");
                            if (!proj.empty()) {
                                entity->SetMesh(proj);
                                editor_state.editor_scene->MarkModified();
                                editor_state.set_status("Applied mesh: " + proj + "  (on '" + entity->GetName() + "')");
                            } else {
                                editor_state.set_status(std::string("Mesh not found on disk: ") + dropped);
                            }
                        }
                    } else {
                        editor_state.set_status("Select an entity first, then drop the mesh on it.");
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
    
    ImGui::Begin("Preferences", &editor_state.show_preferences, flags);  // docked window = child; End() must always run
    {
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
    ImGui::Begin("Scene Playback", &editor_state.show_playback_controls, flags);  // docked window = child; End() must always run
    {
        ImGui::TextUnformatted("Scene Playback Controls:");
        ImGui::Separator();
        
        auto scene = editor_state.editor_scene->GetScene();
        bool can_play = scene && !editor_state.scene_playback_manager->IsPlaying();
        
        // Play button
        ImGui::BeginDisabled(!can_play);
        if (ImGui::Button("Play (F5)##playback", ImVec2(80, 0))) {
            BeginPlayMode(editor_state, scene);
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
            EndPlayMode(editor_state, scene);
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
// Performance Overlay (Master Plan Stage 14 — Pillar N: one profiler overlay)
// ============================================================================
//
// A single window surfacing every hot path the engine exposes, replacing the
// older Phase-6 CPU/GPU panels that read a now-superseded profiler:
//   N1 CPU    — canonical gws::profile::Profiler per-tag zones (the same ones
//               the spdlog frame report prints), sorted + colored by tag.
//   N2 GPU    — engine::vulkan::GPUProfiler per-pass timestamp timings.
//   N3 Memory — gws::memory per-tag live/peak + any registered allocators.
//   N4 Net    — engine::network::NetProfiler view (offline until a transport
//               is live in the session; lights up in Stage 7 integration).

// Fed by the networking layer when a session is live (Stage 7 integration);
// stays zero in a plain editor session, so the Net section reads "offline".
engine::network::NetProfiler g_editor_net_profiler;

static ImU32 PerfTagColor(const char* tag) {
    // Stable pastel hue derived from the tag so each subsystem keeps its color.
    uint32_t h = 2166136261u;
    for (const char* c = tag; *c; ++c) h = (h ^ static_cast<uint8_t>(*c)) * 16777619u;
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB((h % 360) / 360.0f, 0.55f, 0.85f, r, g, b);
    return ImGui::GetColorU32(ImVec4(r, g, b, 1.0f));
}

static void PerfFmtBytes(char* buf, size_t n, uint64_t bytes) {
    const char* unit[] = {"B", "KB", "MB", "GB"};
    double v = static_cast<double>(bytes);
    int i = 0;
    while (v >= 1024.0 && i < 3) { v /= 1024.0; ++i; }
    std::snprintf(buf, n, "%.1f %s", v, unit[i]);
}

// Spawn a local multiplayer test session (Unreal-style "Play as N clients"):
// save the current scene to a shared file, become the host, and launch
// `n_clients` extra editor processes that auto-load that scene and join. All
// instances loading the same file keeps the scene-order NetIds aligned.
static void LaunchMultiplayerSession(EditorState& editor_state, int n_clients, uint16_t port) {
#ifdef _WIN32
    // Save the current scene to an ABSOLUTE path in the host's working directory
    // so every spawned client loads the exact same file (a relative path would
    // depend on each process's CWD and could silently miss / fall back to the
    // default scene — which is what made clients "load a different scene").
    char cwd[MAX_PATH] = {0};
    GetCurrentDirectoryA(MAX_PATH, cwd);
    const std::string shared_scene = std::string(cwd) + "\\__mp_session.scene";
    bool saved = editor_state.editor_scene &&
                 editor_state.editor_scene->SaveScene(shared_scene);
    if (!saved) {
        spdlog::error("[net] launcher: failed to save shared scene to {}", shared_scene);
        return;
    }
    spdlog::info("[net] launcher: shared scene saved to {}", shared_scene);

    if (!editor_state.net_session.active())
        editor_state.net_session.host(port);
    editor_state.show_network_window = true;

    char exe[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, exe, MAX_PATH) == 0) {
        spdlog::error("[net] launcher: GetModuleFileName failed");
        return;
    }
    for (int i = 0; i < n_clients; ++i) {
        // Quote both the exe and the scene path (both may contain spaces).
        // --net-game: spawned clients are game windows (viewport-only), not
        // full editor instances.
        std::string cmd = std::string("\"") + exe + "\" --net-join 127.0.0.1:" +
                          std::to_string(port) + " --scene \"" + shared_scene +
                          "\" --net-game";
        std::vector<char> mut(cmd.begin(), cmd.end());
        mut.push_back('\0');
        STARTUPINFOA si; ZeroMemory(&si, sizeof si); si.cb = sizeof si;
        PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof pi);
        // lpCurrentDirectory = the host's CWD, so children resolve relative
        // asset paths (../../assets/...) exactly like the host does.
        if (CreateProcessA(nullptr, mut.data(), nullptr, nullptr, FALSE, 0,
                           nullptr, cwd, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            spdlog::info("[net] launched client {} -> 127.0.0.1:{}", i + 1, port);
        } else {
            spdlog::error("[net] launcher: CreateProcess failed (err {})",
                          static_cast<unsigned long>(GetLastError()));
        }
    }
#else
    (void)editor_state; (void)n_clients; (void)port;
    spdlog::warn("[net] multiplayer launcher is Windows-only");
#endif
}

// ── Multiplayer / Network panel (Stage 7) ──────────────────────────────────
// Host / Join / Disconnect + live session status. Docked window, so End() must
// run unconditionally (docked windows are child windows internally).
void ShowNetworkPanel(EditorState& editor_state) {
    if (!editor_state.show_network_window) return;
    ImGui::Begin("Multiplayer", &editor_state.show_network_window);
    {
        auto& net = editor_state.net_session;
        const auto& st = net.status();

        auto role_name = [](schizo::editor::NetRole r) {
            switch (r) {
                case schizo::editor::NetRole::Host:   return "Host (server)";
                case schizo::editor::NetRole::Client: return "Client";
                default:                              return "Offline";
            }
        };
        auto fmt_bytes = [](uint64_t b) {
            static char buf[32];
            const char* u[] = {"B", "KB", "MB", "GB"};
            double v = static_cast<double>(b); int i = 0;
            while (v >= 1024.0 && i < 3) { v /= 1024.0; ++i; }
            std::snprintf(buf, sizeof buf, "%.1f %s", v, u[i]);
            return buf;
        };

        if (!net.active()) {
            ImGui::TextDisabled("Not connected.");
            ImGui::Separator();
            ImGui::TextUnformatted("Host a session:");
            ImGui::SetNextItemWidth(90);
            ImGui::InputText("Port##host", editor_state.net_host_port,
                             sizeof editor_state.net_host_port,
                             ImGuiInputTextFlags_CharsDecimal);
            ImGui::SameLine();
            if (ImGui::Button("Host")) {
                const int port = std::atoi(editor_state.net_host_port);
                if (port > 0 && port < 65536)
                    net.host(static_cast<uint16_t>(port));
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Join a session:");
            ImGui::SetNextItemWidth(160);
            ImGui::InputText("IP##join", editor_state.net_join_ip,
                             sizeof editor_state.net_join_ip);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            ImGui::InputText("Port##join", editor_state.net_join_port,
                             sizeof editor_state.net_join_port,
                             ImGuiInputTextFlags_CharsDecimal);
            ImGui::SameLine();
            if (ImGui::Button("Join")) {
                const int port = std::atoi(editor_state.net_join_port);
                if (port > 0 && port < 65536)
                    net.join(editor_state.net_join_ip, static_cast<uint16_t>(port));
            }
            ImGui::TextDisabled("Both instances must load the same scene.\n"
                                "The host's moving objects replicate to clients.");

            ImGui::Separator();
            ImGui::TextUnformatted("Local test (in-editor PIE):");
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Clients", &editor_state.net_launch_clients);
            editor_state.net_launch_clients =
                std::max(1, std::min(editor_state.net_launch_clients, 7));
            ImGui::SameLine();
            if (ImGui::Button("Host + Launch")) {
                const int port = std::atoi(editor_state.net_host_port);
                if (port > 0 && port < 65536)
                    LaunchMultiplayerSession(editor_state,
                                             editor_state.net_launch_clients,
                                             static_cast<uint16_t>(port));
            }
            ImGui::TextDisabled("Saves the scene, hosts here, and spawns N client\n"
                                "windows that auto-join. Move each with Arrow keys.");
        } else {
            const bool ok = st.connected;
            const ImVec4 green(0.4f, 0.9f, 0.4f, 1.0f), amber(0.9f, 0.7f, 0.3f, 1.0f);
            ImGui::Text("Role:      %s", role_name(st.role));
            ImGui::Text("Endpoint:  %s", st.endpoint.c_str());
            if (st.role == schizo::editor::NetRole::Host) {
                if (ok) ImGui::TextColored(green, "%zu client(s) connected", st.peer_count);
                else    ImGui::TextColored(amber, "%s", "waiting for clients...");
            } else {
                ImGui::TextColored(ok ? green : amber, "%s", ok ? "connected" : "connecting...");
            }
            ImGui::Separator();
            ImGui::Text("Tick:       %llu", static_cast<unsigned long long>(st.tick));
            ImGui::Text("Peers:      %zu", st.peer_count);
            ImGui::Text("Replicated: %zu entities", st.replicated);
            ImGui::Text("Avatars:    %zu players", st.avatars);
            ImGui::Text("My avatar:  %llu", static_cast<unsigned long long>(st.my_avatar));
            ImGui::Text("Sent:       %s", fmt_bytes(st.bytes_sent));
            ImGui::Text("Received:   %s", fmt_bytes(st.bytes_recv));
            ImGui::Separator();
            ImGui::TextDisabled("Move your player with the Arrow keys.\n"
                                "Each player is a coloured '[net] Player N' cube.");
            if (ImGui::Button("Disconnect")) net.shutdown();
        }
    }
    ImGui::End();
}

void ShowPerformanceOverlay(EditorState& editor_state) {
    if (!editor_state.show_performance) return;

    ImGui::SetNextWindowPos(ImVec2(1090, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(410, 580), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Performance", &editor_state.show_performance)) { ImGui::End(); return; }

    // ---- Frame header + rolling history (last completed frame's stats) ----
    auto& cpu = gws::profile::Profiler::instance();
    const double frame_ms = cpu.last_frame_ns() / 1.0e6;
    static float hist[120] = {};
    static int   hist_i = 0;
    hist[hist_i] = static_cast<float>(frame_ms);
    hist_i = (hist_i + 1) % IM_ARRAYSIZE(hist);
    char overlay[64];
    std::snprintf(overlay, sizeof overlay, "%.2f ms  (%.0f FPS)",
                  frame_ms, frame_ms > 0.0 ? 1000.0 / frame_ms : 0.0);
    ImGui::PlotLines("##frame", hist, IM_ARRAYSIZE(hist), hist_i, overlay, 0.0f, 33.0f, ImVec2(0.0f, 45.0f));
    ImGui::Separator();

    // ---- N1 CPU (canonical scoped-zone profiler) ----
    if (ImGui::CollapsingHeader("CPU (N1)", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::vector<std::pair<std::string, gws::profile::Profiler::TagStat>>
            rows(cpu.last_frame().begin(), cpu.last_frame().end());
        std::sort(rows.begin(), rows.end(),
                  [](const auto& a, const auto& b) { return a.second.total_ns > b.second.total_ns; });
        const double denom = frame_ms > 0.0 ? frame_ms : 1.0;
        if (rows.empty()) ImGui::TextDisabled("no zones this frame");
        for (const auto& [tag, s] : rows) {
            const double ms = s.total_ns / 1.0e6;
            char lbl[96];
            std::snprintf(lbl, sizeof lbl, "%s  %.2fms x%u", tag.c_str(), ms, s.calls);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, PerfTagColor(tag.c_str()));
            ImGui::ProgressBar(static_cast<float>(ms / denom), ImVec2(-FLT_MIN, 0.0f), lbl);
            ImGui::PopStyleColor();
        }
    }

    // ---- N2 GPU (per-pass timestamps) ----
    if (ImGui::CollapsingHeader("GPU (N2)", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& gpu = engine::vulkan::GPUProfiler::instance();
        const auto& passes = gpu.get_all_timings();
        const float gpu_total = gpu.get_total_gpu_time_ms();
        ImGui::Text("total %.2f ms   load %.0f%%", gpu_total, gpu.get_gpu_load_percent());
        if (passes.empty()) {
            ImGui::TextDisabled("no GPU timings (timestamps disabled?)");
        } else {
            std::vector<const engine::vulkan::GPUProfiler::PassTiming*> ps;
            for (const auto& [name, p] : passes) ps.push_back(&p);
            std::sort(ps.begin(), ps.end(),
                      [](const auto* a, const auto* b) { return a->duration_ms > b->duration_ms; });
            const float gdenom = gpu_total > 0.0f ? gpu_total : 1.0f;
            for (const auto* p : ps) {
                char lbl[96];
                std::snprintf(lbl, sizeof lbl, "%s  %.2fms", p->name.c_str(), p->duration_ms);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, PerfTagColor(p->name.c_str()));
                ImGui::ProgressBar(p->duration_ms / gdenom, ImVec2(-FLT_MIN, 0.0f), lbl);
                ImGui::PopStyleColor();
            }
        }
    }

    // ---- N3 Memory (per-tag attribution + registered allocators) ----
    if (ImGui::CollapsingHeader("Memory (N3)", ImGuiTreeNodeFlags_DefaultOpen)) {
        const gws::memory::MemorySnapshot snap = gws::memory::snapshot_memory();
        char tb[32];
        PerfFmtBytes(tb, sizeof tb, snap.total_live_bytes);
        ImGui::Text("live %s in %zu allocs", tb, snap.total_live_count);
        bool any = false;
        for (const auto& u : snap.tags) {
            if (u.stats.live_bytes == 0 && u.stats.peak_bytes == 0) continue;
            any = true;
            char lb[32], pb[32];
            PerfFmtBytes(lb, sizeof lb, u.stats.live_bytes);
            PerfFmtBytes(pb, sizeof pb, u.stats.peak_bytes);
            ImGui::BulletText("%-9s %s  (peak %s, x%zu)", u.name, lb, pb, u.stats.live_count);
        }
        if (!any) {
#if GWS_MEMORY_TRACKING
            ImGui::TextDisabled("per-tag tracking on; nothing routed through allocate_tracked yet");
#else
            ImGui::TextDisabled("per-tag tracking compiled out (release build)");
#endif
        }

        auto allocs = gws::memory::AllocatorRegistry::instance().snapshot();
        if (!allocs.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("allocators (used / reserved, frag):");
            for (const auto& [name, st] : allocs) {
                const float occ = st.total_reserved
                    ? static_cast<float>(st.total_allocated) / static_cast<float>(st.total_reserved) : 0.0f;
                char ub[32], rb[32];
                PerfFmtBytes(ub, sizeof ub, st.total_allocated);
                PerfFmtBytes(rb, sizeof rb, st.total_reserved);
                char lbl[112];
                std::snprintf(lbl, sizeof lbl, "%s  %s/%s  frag %zu%%", name.c_str(), ub, rb, st.fragmentation);
                ImGui::ProgressBar(occ, ImVec2(-FLT_MIN, 0.0f), lbl);
            }
        }
    }

    // ---- N4 Network (bandwidth / RTT / loss / rollback) ----
    if (ImGui::CollapsingHeader("Network (N4)", ImGuiTreeNodeFlags_DefaultOpen)) {
        const engine::network::NetProfileView& v = g_editor_net_profiler.view();
        if (v.peers == 0 && v.total_bytes_sent == 0 && v.total_bytes_recv == 0) {
            ImGui::TextDisabled("offline — no active transport in this session");
        } else {
            ImGui::Text("peers %zu   RTT %u ms   loss %.1f%%", v.peers, v.rtt_ms, v.packet_loss * 100.0f);
            ImGui::Text("up %.1f kbit/s   down %.1f kbit/s",
                        v.send_bps * 8.0 / 1000.0, v.recv_bps * 8.0 / 1000.0);
            ImGui::Text("rollback last %zu (max %zu)", v.rollback_last, v.rollback_max);
        }
    }

    // ---- N5 Frame capture (one-frame draw/dispatch-list snapshot) ----
    if (ImGui::CollapsingHeader("Frame Capture (N5)", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& fc = gws::profile::FrameCapture::instance();
        if (ImGui::Button(fc.armed() ? "Arming... (next frame)" : "Capture Next Frame"))
            fc.arm();
        if (fc.has_capture()) {
            const gws::profile::CapturedFrame& cap = fc.last();
            ImGui::SameLine();
            if (ImGui::Button("Save .txt")) {
                const std::string txt = fc.to_text();
                if (std::FILE* f = std::fopen("frame_capture.txt", "wb")) {
                    std::fwrite(txt.data(), 1, txt.size(), f);
                    std::fclose(f);
                    spdlog::info("[frame-capture] wrote frame_capture.txt ({} bytes)", txt.size());
                }
            }
            ImGui::Text("frame #%llu  -  %zu draws, %llu tris",
                        static_cast<unsigned long long>(cap.frame_index), cap.draw_count(),
                        static_cast<unsigned long long>(cap.total_triangles()));
            ImGui::BeginChild("##capture_list", ImVec2(0.0f, 160.0f), true);
            for (const gws::profile::CapturedDraw& d : cap.draws) {
                ImGui::Text("%-11s %-16s sm%u  v%u i%u%s",
                            d.pass.c_str(), d.label.c_str(), d.submesh,
                            d.vertices, d.indices, d.blend ? " [blend]" : "");
            }
            ImGui::EndChild();
        } else {
            ImGui::TextDisabled("no capture yet - click Capture Next Frame");
        }
    }

    ImGui::End();
}

// ============================================================================
// Debug Panels Window
// ============================================================================

void ShowDebugPanels(EditorState& editor_state) {
    if (!editor_state.show_debug_panels) return;
    
    ImGui::SetNextWindowPos(ImVec2(1500, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 800), ImGuiCond_FirstUseEver);
    
    ImGui::Begin("Debug Systems", &editor_state.show_debug_panels);  // docked window = child; End() must always run
    {
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

int main(int argc, char** argv) {
    // --- Multiplayer CLI (Stage 7 PIE launcher) -------------------------------
    // Spawned client/host instances use these to auto-connect + load the shared
    // scene on startup, so the "Play Multiplayer" launcher can bring up a whole
    // session with no manual clicking:
    //   --net-host <port>       host a session on <port>
    //   --net-join <ip:port>    join a session
    //   --scene <path>          load this scene before connecting
    //   --net-game              game window: viewport-only fullscreen UI (no
    //                           editor panels) — used for spawned client windows
    //   --project <path>        open this project (its manifest) directly,
    //                           bypassing the in-editor launcher (the Hub uses
    //                           this to open a project in its bound engine).
    std::string startup_scene;
    std::string startup_join_ip;
    std::string startup_project;
    uint16_t    startup_join_port = 0;
    uint16_t    startup_host_port = 0;
    bool        game_window_mode  = false;
    bool        startup_probe     = false;   // --startup-probe: time init, then exit
    int         frame_limit       = 0;       // --frames N: render N frames, then exit
    bool        probe_inner_loop  = false;   // --probe-inner-loop: time the budgeted rows
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--net-host" && i + 1 < argc) {
            startup_host_port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (a == "--net-join" && i + 1 < argc) {
            const std::string hp = argv[++i];
            const size_t colon = hp.find(':');
            if (colon != std::string::npos) {
                startup_join_ip   = hp.substr(0, colon);
                startup_join_port = static_cast<uint16_t>(std::atoi(hp.c_str() + colon + 1));
            }
        } else if (a == "--scene" && i + 1 < argc) {
            startup_scene = argv[++i];
        } else if (a == "--project" && i + 1 < argc) {
            startup_project = argv[++i];
        } else if (a == "--net-game") {
            game_window_mode = true;
        } else if (a == "--frames" && i + 1 < argc) {
            // Render N frames and exit. Unlike --startup-probe this actually
            // DRAWS, so it exercises the paths that only run once something is
            // on screen — asset loading, the async OBJ path, the draw
            // collector. That makes it the smoke test a GPU-equipped machine
            // can run non-interactively, instead of "it opened and looked
            // fine".
            frame_limit = std::atoi(argv[++i]);
        } else if (a == "--probe-inner-loop") {
            // Measure the inner-loop rows that need a live editor: entering
            // play, and an asset becoming drawable. These were listed as
            // UNMEASURED for the whole of Phase 2 because nothing could drive
            // the editor -- which made the phase's own exit criterion
            // unverifiable. See DEVELOPER_EXPERIENCE.md §1.1.
            probe_inner_loop = true;
        } else if (a == "--startup-probe") {
            // Initialise everything, report how long it took, then exit without
            // entering the main loop. This is what makes "editor cold start" —
            // a budgeted inner-loop row — measurable at all; without it the
            // number can only be eyeballed with a stopwatch.
            startup_probe = true;
        }
    }

    const auto gws_startup_t0 = std::chrono::steady_clock::now();

    try {
        spdlog::set_level(spdlog::level::info);
        // Flush every info+ line immediately so logs survive a hard kill / crash
        // and show up live when stdout is redirected to a file (otherwise the C
        // runtime fully-buffers a redirected stream and the tail is lost).
        spdlog::flush_on(spdlog::level::info);

        // ---- Diagnostics: persistent logging + crash handler (install EARLY) ----
        // Reports/dumps/logs go to %LOCALAPPDATA%/GameWorldshaper/diagnostics so
        // they survive the process and are easy to find (and send) after a crash.
        std::string diag_dir;
        if (const char* la = std::getenv("LOCALAPPDATA"))
            diag_dir = std::string(la) + "\\GameWorldshaper\\diagnostics";
        else
            diag_dir = "diagnostics";
        gws::diag::init_logging(diag_dir, "editor");
        {
            gws::diag::CrashConfig cc;
            cc.app_name   = "editor";
            cc.version    = GWS_ENGINE_VERSION;
            cc.report_dir = diag_dir;
            cc.context_provider = [sp = startup_project, ss = startup_scene]() {
                std::string s;
                s += std::string("project: ") + (sp.empty() ? "(none / launcher)" : sp) + "\n";
                if (!ss.empty()) s += "scene(arg): " + ss + "\n";
                return s;
            };
            gws::diag::install_crash_handler(cc);
        }

        // Mirror all default-logger output into the in-editor "Output" panel.
        // Installed here (before the heavy init logging) so startup lines are
        // captured too. Lives in console_panel.cpp.
        schizo::editor::install_console_log_sink();
        spdlog::info("=== Project Schizo Editor (Vulkan) v{} ===", GWS_ENGINE_VERSION);

        // Lock the engine content base BEFORE any asset load (self-heals the CWD
        // to a dir containing assets/, so bundled defaults resolve no matter how
        // the editor was launched — repo root, Hub with the exe dir, or install).
        schizo::editor::init_base_dir();

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
        // Runtime texture handling (Master Plan Stage 2). Owns the shared
        // SamplerCache + engine-wide default textures, deduplicates textures by
        // content-addressed AssetId, consumes cooked `.ctex` (BC) when present,
        // and hot-reloads changed source images in place. Declared after the
        // device so it is destroyed *before* it (its defaults/samplers free via
        // the still-live device). Textures loaded through it get generated mips,
        // anisotropic + per-asset samplers, and correct per-slot sRGB.
        // ----------------------------------------------------------------
        TextureManager texture_manager(&device);
        {
            const char* cdirs[] = { "cooked", "bin/cooked", "../cooked", "assets/cooked" };
            for (const char* d : cdirs) {
                std::error_code cec;
                if (std::filesystem::is_directory(d, cec)) { texture_manager.set_cooked_root(d); break; }
            }
        }
        // Flag set by the reload listener when any texture hot-reloaded this
        // poll; the loop then re-writes affected material descriptor sets (a
        // reload rebuilds the texture's VkImageView, so a material referencing
        // it must rewrite or it samples a destroyed view).
        bool textures_reloaded = false;
        texture_manager.add_reload_listener(
            [&textures_reloaded](uint64_t id, gws::renderer::gpu::Texture* t) {
                textures_reloaded = true;
                spdlog::info("Editor: texture {:016x} hot-reloaded (gen {})",
                             id, t ? t->generation() : 0u);
            });

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
        // Scene-relative sky path currently baked into env_map. The frame loop
        // compares this to the active scene's GetSkyHdr() and live-reloads on a
        // change (covers both editing the field AND switching scenes).
        std::string applied_sky_hdr;
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::path hdr_path;

            // Scene-bound sky: prefer the STARTUP scene's SKY_HDR (peeked from the
            // .scene header) so a project/scene opens with its own declared sky.
            auto peek_scene_sky = [](const std::string& scene_file) -> std::string {
                std::ifstream f(scene_file);
                std::string line;
                while (std::getline(f, line)) {
                    if (line.compare(0, 8, "SKY_HDR=") == 0) return line.substr(8);
                    if (line.compare(0, 13, "ENTITY_COUNT=") == 0) break;   // end of header
                }
                return {};
            };
            {
                std::string scene_path;
                if (!startup_project.empty()) {
                    schizo::project::ProjectManifest pm;
                    if (schizo::project::ProjectManifest::load(startup_project, pm))
                        scene_path = pm.default_scene_path();
                } else if (!startup_scene.empty()) {
                    scene_path = startup_scene;
                }
                if (!scene_path.empty()) {
                    std::string sky = peek_scene_sky(scene_path);
                    applied_sky_hdr = sky;   // seed so frame 1 doesn't redundantly reload
                    if (!sky.empty()) {
                        fs::path p(sky);
                        if (p.is_relative() && !startup_project.empty())
                            p = fs::path(startup_project).parent_path() / p;
                        if (fs::exists(p, ec)) hdr_path = p;
                    }
                }
            }

            // Fallback: first assets/skies/*.hdr in the working dir.
            if (hdr_path.empty()) {
                const fs::path skies_dir = "assets/skies";
                if (fs::exists(skies_dir, ec) && fs::is_directory(skies_dir, ec))
                    for (const auto& entry : fs::directory_iterator(skies_dir, ec)) {
                        const auto ext = entry.path().extension().string();
                        if (ext == ".hdr" || ext == ".HDR") { hdr_path = entry.path(); break; }
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

        // Volumetric sun lighting / light shafts — ray-marches the sun's
        // shadow map for single-scattered in-scattering, additively
        // composited into the HDR. Runs after Lighting + SSR, before
        // Transparent (it is an HDR effect, drawn before tone-mapping).
        // Dramatic shafts need a shadow-casting directional light + occluders;
        // without a shadow map it degrades to a soft sun-ward haze.
        auto volumetric_light = (shadow_map)
            ? VulkanVolumetricLightPass::create(
                  &device, g_buffer.get(),
                  lighting->get_output_view(),
                  VK_FORMAT_R16G16B16A16_SFLOAT,
                  shadow_map->get_shadow_view(),
                  shadow_map->get_shadow_sampler(),
                  kW, kH)
            : nullptr;

        // Froxel volumetric fog (Stage 3.3) — 3D froxel grid: per-froxel
        // scatter (height fog + sun + local lights), front-to-back integrate,
        // composite into HDR. Reads the SAME light buffer the deferred pass
        // uses so point/spot lights scatter in the fog. Off by default (a
        // heavy always-on effect); toggle in the Post-Processing panel.
        auto froxel_fog = (shadow_map)
            ? VulkanFroxelFogPass::create(
                  &device, g_buffer.get(),
                  lighting->get_output_view(),
                  VK_FORMAT_R16G16B16A16_SFLOAT,
                  shadow_map->get_shadow_view(),
                  shadow_map->get_shadow_sampler(),
                  lighting->get_light_buffer(),
                  kW, kH)
            : nullptr;
        if (froxel_fog) froxel_fog->set_enabled(false);  // opt-in (heavy; Post-Processing panel)

        // DDGI — dynamic diffuse GI via irradiance probes (Master Plan §3.2).
        // Probe rays trace the same TLAS the RT effects use; hits are re-shaded
        // from the RT instance SSBO; the composite adds probe irradiance onto
        // the lit HDR. Requires hardware RT (create returns nullptr without it).
        // Off by default — toggle in the Post-Processing panel.
        auto ddgi = (env_map && device.has_ray_tracing())
            ? VulkanDdgiPass::create(
                  &device, g_buffer.get(),
                  lighting->get_output_view(),
                  VK_FORMAT_R16G16B16A16_SFLOAT,
                  env_map->get_view(), env_map->get_sampler(),
                  kW, kH)
            : nullptr;
        // DDGI grid auto-fit: the default probe grid only covers a ~30-unit box
        // at the origin, so any scene that's bigger or offset gets no coverage
        // and GI looks flat/wrong. We refit the grid to the scene's world AABB
        // (computed from the opaque draw list each frame) whenever the user
        // enables DDGI or presses "Fit to scene". These persist across frames.
        bool      ddgi_autofit_pending = true;   // fit once as soon as draws exist

        // Stage 5 skinned-animation TEST RIG — a procedural boxy biped that
        // exercises the whole animation stack (state machine → root motion →
        // foot IK → GPU compute skinning → G-buffer draw). Off by default;
        // toggle in the Post-Processing panel ("Animation Demo").
        auto anim_demo = schizo::editor::SkinnedAnimDemo::create(
            &device, mat_layout, mat_pool, &texture_manager, glm::vec3(0.0f, 0.0f, 0.0f));
        if (anim_demo) anim_demo->set_enabled(false);

        // Path B: a rigged character imported from a real .gltf/.glb at runtime
        // (File > Import Skinned Model). Driven through the same skin path as the
        // procedural demo above. Null until the user imports one.
        std::unique_ptr<schizo::editor::ImportedSkinnedActor> imported_actor;

        // Runtime game-UI HUD demo (gws_ui framework, ImGui-rasterised). Off by
        // default; toggle in the Post-Processing panel.
        schizo::editor::GameUiDemo game_ui;

        // Volumetric clouds — full pipeline (noise bake → raymarch → temporal
        // resolve → composite). Runs after Lighting/SSR, before the light
        // shafts (clouds are distant sky; shafts scatter in front of them).
        auto clouds = VulkanCloudPass::create(
            &device, g_buffer.get(),
            lighting->get_output_view(),
            VK_FORMAT_R16G16B16A16_SFLOAT,
            kW, kH);

        // Feed the cloud shadow map to the deferred lighting pass (clouds
        // darken the ground) and the light-shaft pass (god rays through gaps).
        if (clouds) {
            lighting->set_cloud_shadow(clouds->get_shadow_view(),
                                       clouds->get_shadow_sampler());
            if (volumetric_light)
                volumetric_light->set_cloud_shadow(clouds->get_shadow_view(),
                                                   clouds->get_shadow_sampler());
            if (ssr)
                ssr->set_cloud_sky(clouds->get_cloud_sky_view(),
                                   clouds->get_cloud_sky_sampler());
        }

        // Water surfaces (terrain expansion) — renders WaterComponent rects
        // into the HDR target between SSR and the atmospheric composites.
        auto water_pass = env_map
            ? VulkanWaterPass::create(
                  &device, g_buffer.get(),
                  lighting->get_output_view(),
                  VK_FORMAT_R16G16B16A16_SFLOAT,
                  env_map->get_view(), env_map->get_sampler(),
                  kW, kH)
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

        // ---- Live sky hot-swap ----------------------------------------------
        // Rebuilds env_map's contents in place from a scene-relative HDR path
        // (empty = procedural) and re-binds the passes that sample IBL. The base
        // cubemap view handle is preserved by reload(), so SSR/DDGI/water — which
        // captured it at create() and have no setter — pick up the new sky with
        // no rebind. Called from the frame loop when the scene's sky changes.
        auto apply_sky = [&](const std::string& scene_rel) {
            if (!env_map) return;
            namespace fs = std::filesystem;
            std::error_code ec;
            // Resolve the scene-relative path: CWD is the project root (project
            // sandbox chdir), so a project-relative path resolves directly; fall
            // back to <project-dir>/rel, else procedural.
            std::string resolved;
            if (!scene_rel.empty()) {
                fs::path p(scene_rel);
                if (fs::exists(p, ec)) {
                    resolved = p.string();
                } else if (!startup_project.empty()) {
                    fs::path pp = fs::path(startup_project).parent_path() / scene_rel;
                    if (fs::exists(pp, ec)) resolved = pp.string();
                }
                if (resolved.empty())
                    spdlog::warn("[sky] '{}' not found on disk — using procedural sky", scene_rel);
            }
            env_map->reload(resolved);   // waits for device idle internally
            // ORDER MATTERS: reload() DESTROYED + recreated the IBL images, so the
            // lighting pass still holds the OLD (freed) IBL view handles. Every
            // setter below re-runs update_descriptor_set(), which rewrites the
            // WHOLE descriptor set (env + IBL) — so the IBL views must be refreshed
            // to the new handles FIRST. Otherwise the very first setter records a
            // freed VkImageView via vkUpdateDescriptorSets and the driver faults
            // (this was the scene-switch crash). The base cubemap view handle is
            // PRESERVED by reload(), so it stays valid the whole time.
            if (env_map->ibl_ready()) {
                lighting->set_ibl_textures(
                    env_map->get_irradiance_view(),  env_map->get_sampler(),
                    env_map->get_prefiltered_view(), env_map->get_sampler(),
                    env_map->get_brdf_lut_view(),    env_map->get_brdf_lut_sampler(),
                    env_map->get_prefilter_mips());
                if (transparent)
                    transparent->set_ibl_textures(
                        env_map->get_irradiance_view(),  env_map->get_sampler(),
                        env_map->get_prefiltered_view(), env_map->get_sampler(),
                        env_map->get_brdf_lut_view(),    env_map->get_brdf_lut_sampler(),
                        env_map->get_prefilter_mips());
            }
            lighting->set_env_cubemap(env_map->get_view(), env_map->get_sampler());
            applied_sky_hdr = scene_rel;
            spdlog::info("[sky] live-reloaded: '{}'", scene_rel.empty() ? "(procedural)" : scene_rel);
        };

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
        graph_cfg.occlusion_culler = nullptr;   // per-draw occlusion queries (hang hazard) stay OFF
        // HZB occlusion culling ENABLED (Stage 3.C). The prior disable was
        // because build_and_readback() was never called, so the CPU HZB held
        // garbage and dropped visible objects. With the pyramid now built each
        // frame (after Geometry), the AABB-vs-HZB test is conservative (samples
        // the FURthest depth over the footprint, treats near-plane straddles as
        // visible) with one frame of latency — standard, accepted popping.
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

        // Per-entity skinned characters (SkinnedMeshComponent). Keyed by entity
        // so two characters animate independently; see skinned_actor_cache.h.
        schizo::editor::SkinnedActorCache skinned_actors;

        // Per-terrain-entity GPU mesh cache. Rebuilds a terrain's grid mesh
        // only when its heightmap version changes (each sculpt edit).
        schizo::editor::TerrainMeshCache terrain_cache;
        // Per-terrain-entity splat material cache (splatmap + 4 tiling layers).
        // Rebuilds when the splatmap / layer paths / tiling change.
        schizo::editor::TerrainGpuCache terrain_gpu_cache;

        // ---- HZB occlusion-cull state (persists across frames) ----
        // The CPU pyramid is one frame old, so the AABB test must use the
        // view-proj of the frame that BUILT it — testing with the current
        // camera against last frame's depth falsely culled anything newly
        // revealed by camera motion (the "objects vanish while moving" bug).
        glm::mat4 hzb_prev_vp(1.0f);
        glm::vec3 hzb_prev_cam_pos(0.0f);
        glm::vec3 hzb_prev_cam_fwd(0.0f, 0.0f, -1.0f);
        bool      hzb_prev_valid = false;
        // Occlusion hysteresis: a draw must test occluded on TWO consecutive
        // frames before it's actually dropped (one grace frame absorbs
        // readback latency + test jitter). Keyed by mesh+submesh+position.
        std::unordered_map<uint64_t, uint8_t> hzb_occluded_streak;

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
        // Game windows never touch editor.ini (they draw no docked layout and
        // must not clobber the editor's persisted one).
        ImGui::GetIO().IniFilename = game_window_mode ? nullptr : "editor.ini";
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

        // Recent-projects list for the launcher (from the user config dir).
        schizo::project::ProjectsRegistry projects_registry;
        projects_registry.load();

        g_editor_state = &editor_state;
        glfwSetDropCallback(glfw_window, DropCallback);

        // One-time dock-layout migration. editor.ini persists the dock layout,
        // but a layout saved before a panel existed (e.g. Terminal / Output)
        // won't place that panel — it floats. Bump kDockLayoutVersion whenever
        // the docked-panel set changes: on a version mismatch we force one
        // rebuild of the default arrangement, which ImGui then persists so
        // later launches restore it (with the user's splitter sizes).
        {
            int saved = 0;
            if (std::ifstream vf{"editor_layout.version"}) vf >> saved;
            // Only REQUEST the rebuild here. The version stamp is written after
            // the rebuilt layout is actually persisted (in the dockspace block),
            // so a crash before that re-triggers the migration next launch
            // rather than stranding new panels as floating windows.
            if (saved != kEditorDockLayoutVersion)
                editor_state.request_reset_layout = true;
        }

        editor_state.asset_browser  = std::make_unique<schizo::editor::AssetBrowserPanel>();
        // Double-clicking a .scene in the browser loads it.
        editor_state.asset_browser->on_open_scene =
            [&editor_state](const std::string& p) {
                if (editor_state.editor_scene && editor_state.editor_scene->LoadScene(p))
                    spdlog::info("[AssetBrowser] loaded scene '{}'", p);
                else
                    spdlog::warn("[AssetBrowser] failed to load scene '{}'", p);
            };
        editor_state.material_editor = std::make_unique<schizo::editor::MaterialEditorPanel>();
        editor_state.asset_import_dialog = std::make_unique<schizo::editor::AssetImportDialog>();
        editor_state.scene_playback_manager = std::make_unique<schizo::editor::ScenePlaybackManager>();
        editor_state.character_panel = std::make_unique<schizo::editor::CharacterControllerPanel>();
        editor_state.ability_panel   = std::make_unique<schizo::editor::AbilitySystemPanel>();
        editor_state.network_panel   = std::make_unique<schizo::editor::NetworkSystemPanel>();
        spdlog::info("Editor state and panels initialized");

        // Custom scripts (Stage 12): register language backends + bind the
        // engine API table (entity/transform, input, spawn, physics, render,
        // audio) that every backend marshals onto.
        editor_state.script_system.register_host(".py",
            schizo::editor::make_python_host());
        if (auto cpp_host = schizo::editor::make_cpp_host()) {
            editor_state.script_system.register_host(".cpp", std::move(cpp_host));
            editor_state.script_system.register_host(".cc",
                schizo::editor::make_cpp_host());
        }
        if (auto cs_host = schizo::editor::make_cs_host())
            editor_state.script_system.register_host(".cs", std::move(cs_host));
        schizo::editor::bind_editor_script_api(editor_state.script_api,
                                               &editor_state.script_ctx);
        spdlog::info("[script] backends registered: .py (Python/pocketpy), "
                     ".cpp/.cc (native C++ via g++), .cs (C#/.NET)");

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

                // A small water pool off to the side (terrain expansion demo).
                if (auto water = scene->CreateEntity("Water Pool")) {
                    water->GetTransform()->SetLocalPosition(glm::vec3(12.0f, 0.35f, 0.0f));
                    if (auto wc = water->AddComponent<schizo::scene::WaterComponent>())
                        wc->SetSize(glm::vec2(14.0f, 10.0f));
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
        // Surface the frame allocator in the Stage 14 memory profiler (N3): a
        // linear arena, so used/reserved occupancy with zero fragmentation.
        gws::memory::ScopedAllocatorProbe frame_alloc_probe(
            "frame (transient)", [&frame_allocator]() {
                gws::memory::AllocationStats st;
                st.total_allocated = frame_allocator.total_used();
                st.total_reserved  = static_cast<size_t>(frame_allocator.current().capacity()) *
                                     frame_allocator.worker_count();
                st.peak_used       = st.total_allocated;
                st.allocation_count = 0;
                st.fragmentation    = 0;  // bump allocator — no external fragmentation
                return st;
            });
        spdlog::info("Engine foundation online: {} job workers, frame allocator {} MiB x2 per worker",
                     kJobWorkers, 8);

        // ----------------------------------------------------------------
        // Stage 6 audio: open the playback device. Per-entity AudioSource /
        // AudioListener components are driven each frame by EditorAudioDriver;
        // the listener defaults to the editor camera when no AudioListener
        // entity is active.
        // ----------------------------------------------------------------
        gws::audio::AudioEngine audio;
        if (!audio.init())
            spdlog::warn("[audio] no playback device available — audio disabled");

        // Drives every entity's AudioSource/AudioListener component from the OOP
        // scene each frame (clip decode-on-first-play + per-entity voice map).
        schizo::editor::EditorAudioDriver audio_driver(audio);

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
        editor_state.ecs_bridge = &ecs_bridge;   // inspector authors gameplay components on it
        editor_scene.SetEcsBridge(&ecs_bridge);  // scene save/load persist the .gameplay sidecar (F3)
        ecs_bridge.seed_demo_content();          // so Inventory/Equipment have real items to test with
        {   // F5: load any author-written .items data files into the item catalog
            const int n = ecs_bridge.load_gameplay_data("assets/gameplay");
            if (n > 0) spdlog::info("Loaded {} item def(s) from assets/gameplay/*.items", n);
        }
        bool ecs_shadow_logged = false;
        bool ecs_persist_logged = false;
        bool ecs_snapshot_logged = false;

        // ----------------------------------------------------------------
        // Multiplayer startup (PIE launcher): a spawned instance loads the
        // shared scene and auto-hosts/joins so a whole session comes up with no
        // clicking. Applied after full init, before the loop.
        // ----------------------------------------------------------------
        if (!startup_scene.empty()) {
            editor_state.editor_scene->LoadScene(startup_scene);
            spdlog::info("[net] startup loaded shared scene: {}", startup_scene);
        }
        if (startup_host_port != 0) {
            editor_state.net_session.host(startup_host_port);
            editor_state.show_network_window = true;
        } else if (startup_join_port != 0 && !startup_join_ip.empty()) {
            editor_state.net_session.join(startup_join_ip, startup_join_port);
            editor_state.show_network_window = true;
        }

        // --project <path>: the Hub opens a project directly in its bound engine
        // version — load the manifest (features + scene) and skip the launcher.
        if (!startup_project.empty()) {
            schizo::project::ProjectManifest pm;
            if (schizo::project::ProjectManifest::load(startup_project, pm)) {
                editor_state.project        = pm;
                editor_state.features       = pm.features;
                editor_state.project_loaded = true;
                schizo::editor::set_project_root(pm.project_dir);  // sandbox CWD
                if (editor_state.asset_browser)                    // re-scope the browser to THIS project
                    editor_state.asset_browser->Reroot();          // (was rooted at the engine dir at construction)
                const std::string scene_path = pm.default_scene_path();
                std::error_code sec;
                if (std::filesystem::exists(scene_path, sec))
                    editor_state.editor_scene->LoadScene(scene_path);
                else {
                    editor_state.editor_scene->NewScene(pm.name);
                    editor_state.editor_scene->SaveScene(scene_path);
                }
                editor_state.in_launcher = false;
                spdlog::info("[project] opened via --project: '{}' ({})", pm.name, scene_path);
            } else {
                spdlog::error("[project] --project load failed: {}", startup_project);
            }
        }

        // Net-spawned / game-window instances skip the launcher and run with
        // every feature on (they load the shared scene above or from CLI args).
        // A normal launch starts in the launcher (in_launcher defaults true).
        if (game_window_mode || !startup_scene.empty() ||
            startup_host_port != 0 || startup_join_port != 0) {
            editor_state.in_launcher = false;
        }

        // ----------------------------------------------------------------
        // Command palette (4.2)
        // ----------------------------------------------------------------
        // Registered here rather than beside each menu item on purpose: the
        // point of the palette is that there is ONE list of what the editor can
        // do. A command that only exists inside a menu callback is invisible to
        // it, and to anything else that wants to drive the editor by name.
        {
            auto& cmds = editor_state.commands;
            auto& st   = editor_state;

            cmds.add("New Scene", "File", "Ctrl+N", [&st] {
                st.editor_scene->NewScene("Untitled");
            });
            cmds.add("Save Scene", "File", "Ctrl+S", [&st, glfw_window] {
                auto fp = st.editor_scene->GetSceneFilepath();
                if (fp.empty()) fp = SaveSceneDialogNative(glfw_window);
                if (!fp.empty()) st.editor_scene->SaveScene(fp);
            });
            cmds.add("Open Scene", "File", "Ctrl+O", [&st, glfw_window] {
                const std::string path = OpenSceneDialogNative(glfw_window);
                if (!path.empty()) st.editor_scene->LoadScene(path);
            });

            cmds.add("Undo", "Edit", "Ctrl+Z", [&st] {
                if (st.undo_redo_manager.CanUndo()) st.undo_redo_manager.Undo();
            });
            cmds.add("Redo", "Edit", "Ctrl+Y", [&st] {
                if (st.undo_redo_manager.CanRedo()) st.undo_redo_manager.Redo();
            });

            cmds.add("Play", "Run", "", [&st] {
                if (st.scene_playback_manager && !st.scene_playback_manager->IsPlaying())
                    BeginPlayMode(st, st.editor_scene->GetScene());
            });
            cmds.add("Stop", "Run", "", [&st] {
                if (st.scene_playback_manager && st.scene_playback_manager->IsPlaying())
                    EndPlayMode(st, st.editor_scene->GetScene());
            });

            cmds.add("Bake Navmesh", "Tools", "", [&st] {
                auto sc = st.editor_scene->GetScene();
                st.nav_stats = schizo::editor::bake_navmesh_from_scene(sc, st.scene_navmesh);
            });
            cmds.add("Toggle World Streaming", "Tools", "", [&st] {
                st.world_streaming.set_enabled(!st.world_streaming.enabled());
            });
            cmds.add("Toggle Snapping", "Tools", "hold Ctrl", [&st] {
                st.snap.enabled = !st.snap.enabled;
            });
            cmds.add("Toggle Relative Snapping", "Tools", "", [&st] {
                st.snap.relative = !st.snap.relative;
            });

            cmds.add("Toggle Scene Hierarchy", "Window", "", [&st] {
                st.show_scene_hierarchy = !st.show_scene_hierarchy;
            });
            cmds.add("Toggle Inspector", "Window", "", [&st] {
                st.show_inspector = !st.show_inspector;
            });
            cmds.add("Toggle Asset Browser", "Window", "", [&st] {
                st.show_asset_browser = !st.show_asset_browser;
            });
            cmds.add("Toggle Logic Graph", "Window", "", [&st] {
                st.show_logic_graph = !st.show_logic_graph;
            });
            cmds.add("Toggle Post-Processing", "Window", "", [&st] {
                st.show_post_processing = !st.show_post_processing;
            });
            cmds.add("Preferences", "Window", "", [&st] {
                st.show_preferences = !st.show_preferences;
            });

            spdlog::info("[palette] {} commands registered", cmds.size());
        }

        // ----------------------------------------------------------------
        // Main loop
        // ----------------------------------------------------------------
        // Background workers for import/copy/cook. Started here so they exist
        // for the whole loop and are joined in the shutdown block below.
        editor_state.tasks.start();
        // Parse OBJ meshes on a worker; the GPU upload still happens on this
        // thread when the parse completes. Opt-in, so the cache stays fully
        // synchronous for headless tools that have no runner.
        asset_cache.set_task_runner(&editor_state.tasks);
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

        // Recreate the swapchain (and the ImGui framebuffers that wrap its
        // image views) to match the current window framebuffer size. Required
        // whenever the OS window is resized or maximized — otherwise the
        // swapchain goes OUT_OF_DATE, acquire_next_image() returns ~0u, and
        // indexing imgui_framebuffers[~0u] crashes. Safe to call any time; it
        // waits for the GPU to go idle first. The 3D scene's offscreen targets
        // are sized to the Viewport panel (not the window) so they don't need
        // recreation here — ImGui::Image() rescales them.
        auto recreate_swapchain_resources = [&]() {
            int fbw = 0, fbh = 0;
            glfwGetFramebufferSize(glfw_window, &fbw, &fbh);
            if (fbw == 0 || fbh == 0) return;  // minimized — caller skips the frame
            vkDeviceWaitIdle(device.get_device());
            for (VkFramebuffer fb : imgui_framebuffers)
                if (fb) vkDestroyFramebuffer(device.get_device(), fb, nullptr);
            swapchain->recreate(static_cast<uint32_t>(fbw),
                                static_cast<uint32_t>(fbh));
            imgui_framebuffers.assign(swapchain->get_image_count(), VK_NULL_HANDLE);
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
            spdlog::info("Swapchain recreated: {}x{}",
                         swapchain->get_width(), swapchain->get_height());
        };

        // Spot-light cookie texture (loaded on demand, cached by path). One
        // shared cookie slot — multiple cookie-spots use the last one bound.
        std::unique_ptr<gws::renderer::gpu::Texture> cookie_tex;
        std::string cookie_path_loaded;

        // --startup-probe: everything above is initialisation. Report the cost
        // and leave without rendering a frame, so cold start is a number rather
        // than an impression. See innerloop_check / DEVELOPER_EXPERIENCE.md §1.1.
        if (startup_probe) {
            const double ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - gws_startup_t0).count();
            // Attribute the cost: how much of cold start is runtime GLSL
            // compilation? This is the number that decides whether converting
            // the 17 compile_glsl sites to precompiled SPIR-V is worth doing
            // (issue #16) — rather than assuming it from another engine's
            // documented problem, which is how that claim got overstated.
            const double gms = gws::renderer::gpu::gws_runtime_glsl_ms();
            std::printf("{\"startup_ms\":%.1f,\"runtime_glsl_ms\":%.1f,\"runtime_glsl_count\":%d,\"glsl_share_pct\":%.1f}\n",
                        ms, gms, gws::renderer::gpu::gws_runtime_glsl_count(),
                        ms > 0.0 ? (gms / ms * 100.0) : 0.0);
            spdlog::info("[startup-probe] initialised in {:.1f} ms; exiting without entering the main loop", ms);
            return 0;
        }

        // --probe-inner-loop: drive the two budgeted rows that need a live
        // editor, report them, and leave. Deliberately placed AFTER the task
        // runner is started, so the asset row measures the path the editor
        // really uses (parse on a worker, upload here) rather than a
        // synchronous shortcut that would flatter the number.
        if (probe_inner_loop) {
            using clock = std::chrono::steady_clock;
            auto ms_since = [](clock::time_point t) {
                return std::chrono::duration<double, std::milli>(clock::now() - t).count();
            };

            double play_ms = -1.0, asset_ms = -1.0, gltf_ms = -1.0;

            // ---- play-mode entry -------------------------------------------
            if (auto pscene = editor_state.editor_scene->GetScene()) {
                const auto t0 = clock::now();
                if (editor_state.scene_playback_manager->StartPlayback(pscene)) {
                    play_ms = ms_since(t0);          // StartPlayback returns once playable
                    editor_state.scene_playback_manager->StopPlayback();
                } else {
                    spdlog::warn("[probe] play-mode entry not measured: playback would not start");
                }
            }

            // ---- asset to viewport -----------------------------------------
            // From "ask for this mesh" to "it has draw items", including the
            // worker hop. Cache is cleared first or this would time a hit.
            {
                const std::string mesh = "assets/test_cooked/cube3d.obj";
                asset_cache.clear();
        skinned_actors.clear();      // skinned meshes + materials
                asset_cache.set_task_runner(&editor_state.tasks);
                const auto t0 = clock::now();
                const gws::renderer::gpu::Scene* loaded = nullptr;
                for (int i = 0; i < 20000 && !loaded; ++i) {
                    editor_state.tasks.poll();
                    loaded = asset_cache.get_or_load(mesh, &device, mat_layout,
                                                     mat_pool, &texture_manager);
                    if (!loaded) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                if (loaded && !loaded->draw_items.empty()) asset_ms = ms_since(t0);
                else spdlog::warn("[probe] asset-to-viewport not measured: '{}' did not load", mesh);
            }

            // Same again for glTF. Both formats parse on a worker now, and they
            // take different code paths, so measuring only OBJ would leave the
            // one most real assets use unverified.
            {
                const std::string gmesh = "assets/models/test_rig.gltf";
                const auto t0 = clock::now();
                const gws::renderer::gpu::Scene* loaded = nullptr;
                for (int i = 0; i < 20000 && !loaded; ++i) {
                    editor_state.tasks.poll();
                    loaded = asset_cache.get_or_load(gmesh, &device, mat_layout,
                                                     mat_pool, &texture_manager);
                    if (!loaded) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                if (loaded && !loaded->draw_items.empty()) gltf_ms = ms_since(t0);
                else spdlog::warn("[probe] gltf load not measured: '{}' did not load", gmesh);
            }

            std::printf("{\"play_mode_entry_ms\":%.1f,\"asset_to_viewport_ms\":%.1f,"
                        "\"gltf_to_viewport_ms\":%.1f}\n",
                        play_ms, asset_ms, gltf_ms);
            editor_state.tasks.stop();
            return 0;
        }

        while (!glfwWindowShouldClose(glfw_window)) {
            glfwPollEvents();

            // Poll source-image hot reload ~once/second (not every frame — it
            // stats watched files). Reloads changed textures in place and fires
            // the reload listener registered above. If anything reloaded,
            // re-write the descriptor sets of cached glTF-scene materials (their
            // texture views were rebuilt) — the GPU is already idle from the
            // reload's wait_idle, but wait again to be safe across frames.
            // Every frame now: the shared AssetWatcher throttles its own scans,
            // so the old 60-frame gate only added up to a second of latency on
            // top of the settle window.
            texture_manager.poll_hot_reload();
            if (textures_reloaded) {
                device.wait_idle();
                asset_cache.rewrite_all_materials();
                terrain_gpu_cache.rewrite_all_materials();   // terrain layer textures too
                textures_reloaded = false;
            }

            // Deliver finished background work on this thread, where touching
            // the scene and the GPU is legal.
            editor_state.tasks.poll();

            glm::vec3 origin_shift_this_frame(0.0f);

            // World streaming + floating origin, driven by the viewport camera
            // (3.3). The rebase shift comes back out because this owns the
            // camera: applying it to entities but not the camera would make the
            // world appear to teleport.
            if (editor_state.world_streaming.enabled()) {
                const glm::vec3 shift = editor_state.world_streaming.update(
                    editor_state.editor_scene->GetScene(),
                    editor_state.viewport_camera.GetPosition());
                if (shift != glm::vec3(0.0f))
                    editor_state.viewport_camera.SetPosition(
                        editor_state.viewport_camera.GetPosition() + shift);
                origin_shift_this_frame = shift;

                // The rebase moved the world; the baked navmesh and everything
                // the agents remember about it are still in the old frame.
                // Without this an agent survives a rebase by walking toward
                // where its goal used to be, over geometry that is no longer
                // under it -- and rebase shifts are far larger than the mesh,
                // so it is off the navmesh entirely, not merely inaccurate.
                if (shift != glm::vec3(0.0f)) {
                    editor_state.scene_navmesh.translate(shift);
                    editor_state.npc_agents.apply_origin_shift(shift);
                    if (editor_state.scene_playback_manager)
                        editor_state.scene_playback_manager->ApplyOriginShift(shift);
                    // The imported preview actor is not a scene entity, so the
                    // rebase loop never reaches it -- it would sit at the old
                    // origin while the level moved out from under it.
                    if (imported_actor)
                        imported_actor->set_world_pos(imported_actor->world_pos() + shift);
                    if (anim_demo) anim_demo->apply_origin_shift(shift);
                }

                // Replication is absolute and every peer rebases on its own
                // camera, so two clients streaming different parts of the map
                // do not share a coordinate frame. Set every frame rather than
                // on change: joining mid-session has to pick up the offset that
                // already accumulated.
                editor_state.net_session.set_origin_offset(
                    editor_state.world_streaming.total_shift());
            }

            // Scene-file watch. Re-seeded whenever the open file changes OR the
            // scene was just saved -- without the second case the editor's own
            // save would look like an external edit and prompt about itself.
            {
                const std::string& spath = editor_state.editor_scene->GetSceneFilepath();
                const bool dirty_now = editor_state.editor_scene->HasUnsavedChanges();
                const bool just_saved = editor_state.watched_scene_dirty && !dirty_now;
                if (spath != editor_state.watched_scene_path || just_saved) {
                    editor_state.scene_watcher.clear();
                    editor_state.watched_scene_path = spath;
                    editor_state.scene_changed_on_disk = false;
                    if (!spath.empty()) {
                        editor_state.scene_watcher.watch(spath, [&editor_state](const std::string&) {
                            editor_state.scene_changed_on_disk = true;
                        });
                    }
                }
                editor_state.watched_scene_dirty = dirty_now;

                using namespace std::chrono;
                editor_state.scene_watcher.poll(
                    duration<double>(steady_clock::now().time_since_epoch()).count());

                if (editor_state.scene_changed_on_disk) {
                    if (!editor_state.editor_scene->HasUnsavedChanges()) {
                        // Nothing to lose -- reload silently, which is what you
                        // want after a git pull.
                        const std::string path = editor_state.editor_scene->GetSceneFilepath();
                        if (!path.empty() && editor_state.editor_scene->LoadScene(path)) {
                            editor_state.selected_entity_id = 0;
                            editor_state.set_status("Scene changed on disk — reloaded");
                            spdlog::info("[scene] '{}' changed on disk; reloaded (no unsaved changes)", path);
                        }
                        editor_state.scene_changed_on_disk = false;
                        editor_state.watched_scene_path.clear();   // force a re-seed
                    } else if (!editor_state.show_scene_reload_prompt) {
                        // There IS something to lose. Ask.
                        editor_state.show_scene_reload_prompt = true;
                        spdlog::warn("[scene] '{}' changed on disk and there are unsaved changes",
                                     editor_state.editor_scene->GetSceneFilepath());
                    }
                }
            }

            // Mesh hot reload: drop cache entries whose source file changed;
            // the next draw rebuilds them through the ordinary load path. Save
            // from Blender over a model the scene uses and the viewport follows.
            asset_cache.poll_reload([&device]{ device.wait_idle(); });

            // Live sky hot-swap: rebuild the environment map in place when the
            // active scene's sky HDR changes (Inspector edit OR scene switch).
            // Compared against the scene's stored string, so a missing file that
            // falls back to procedural isn't retried every frame.
            if (editor_state.editor_scene) {
                if (auto sky_scene = editor_state.editor_scene->GetScene()) {
                    if (sky_scene->GetSkyHdr() != applied_sky_hdr)
                        apply_sky(sky_scene->GetSkyHdr());
                }
            }

            // Match the swapchain to the window before doing any per-frame work.
            // Done here (before begin_frame / the ImGui frame) so a resize never
            // leaves a half-built frame. Minimized (0-size) → skip the frame.
            {
                int fbw = 0, fbh = 0;
                glfwGetFramebufferSize(glfw_window, &fbw, &fbh);
                if (fbw == 0 || fbh == 0) {
                    glfwWaitEvents();   // sleep until restored (no busy spin)
                    continue;
                }
                if (static_cast<uint32_t>(fbw) != swapchain->get_width() ||
                    static_cast<uint32_t>(fbh) != swapchain->get_height()) {
                    recreate_swapchain_resources();
                }
            }

            // Recycle the per-frame transient allocator (double-buffered, so
            // last frame's results survive into this frame's first reads).
            frame_allocator.begin_frame();
            GWS_PROFILE_FRAME_BEGIN();

            double now_wall = glfwGetTime();
            float delta_time = static_cast<float>(now_wall - last_frame_wall);
            last_frame_wall = now_wall;
            if (delta_time > kMaxFrameDt) delta_time = kMaxFrameDt;
            if (delta_time < 0.0f) delta_time = 0.0f;

            // Tick the skinned-animation test rig (CPU: state machine + root
            // motion + foot IK). Done early so the pose is current for both the
            // skinning dispatch and the DrawItem appended below.
            if (anim_demo && anim_demo->enabled()) anim_demo->advance(delta_time);

            // Consume a requested rigged-model import (device + pools in scope).
            if (!editor_state.pending_skinned_import.empty()) {
                // Import onto an ENTITY rather than into a global. Before 3.8
                // this replaced a single unique_ptr, so the editor could show
                // exactly one character and you could not place it with the
                // gizmo. Now it becomes a SkinnedMeshComponent, which means it
                // has a Transform, saves with the scene, and can coexist with
                // other characters.
                const std::string rig = editor_state.pending_skinned_import;
                editor_state.pending_skinned_import.clear();

                if (auto sc = editor_state.editor_scene->GetScene()) {
                    // Assign to the selection when there is one; otherwise make
                    // an entity for it, so the import is never silently lost.
                    auto target = FindEntityById(sc, editor_state.selected_entity_id);
                    if (!target) {
                        target = sc->CreateEntity(
                            std::filesystem::path(schizo::editor::utf8_path(rig)).stem().string());
                        editor_state.selected_entity_id = target ? target->GetId() : 0;
                    }
                    if (target) {
                        auto* smc = target->GetSkinnedMeshComponent();
                        smc->gltf_path  = rig;
                        smc->clip_index = 0;
                        smc->playing    = true;
                        smc->speed      = 1.0f;
                        editor_state.editor_scene->MarkModified();
                        editor_state.set_status("Rigged character on '" + target->GetName() + "'");
                        spdlog::info("[skinned] '{}' assigned to entity '{}'", rig, target->GetName());
                    }
                }
            }
            if (imported_actor) imported_actor->advance(delta_time);

            // NPC agents (3.5) — the consumer that makes the baked navmesh and
            // the rigged characters observable. Play-mode only: an agent that
            // patrols while you are editing would move things under the gizmo.
            if (editor_state.scene_playback_manager &&
                editor_state.scene_playback_manager->IsPlaying()) {
                editor_state.npc_agents.update(editor_state.editor_scene->GetScene(),
                                               editor_state.scene_navmesh, delta_time,
                                               editor_state.ecs_bridge);
            }

            // Particle emitters (3.9). Fed the rebase shift because particles
            // hold WORLD-space positions: without it an emitter walks away from
            // its own smoke the first time the floating origin moves.
            editor_state.particles.update(
                editor_state.editor_scene->GetScene(), delta_time,
                editor_state.viewport_camera.GetPosition(), glm::vec3(0.0f, 1.0f, 0.0f),
                origin_shift_this_frame);
            // Deliberately NOT cleared here: the skinned-character tick below
            // also needs it, and the declaration at the top of the loop body
            // already resets it every frame. Clearing here made the rebase
            // invisible to locomotion, which is the one place it matters most.

            // Per-entity skinned characters: build on demand, pose, and place
            // from the entity's Transform. Done before the command buffer opens
            // so record_skin below has an up-to-date pose.
            if (auto sk_scene = editor_state.editor_scene->GetScene()) {
                for (const auto& ent : sk_scene->GetEntities()) {
                    if (!ent) continue;
                    auto* smc = ent->GetSkinnedMeshComponent();
                    if (!smc || !smc->active()) continue;
                    auto* actor = skinned_actors.get_or_create(
                        ent->GetId(), smc->gltf_path, &device,
                        mat_layout, mat_pool, &texture_manager);
                    if (!actor) continue;
                    auto* sk_tf = ent->GetTransform();
                    if (!sk_tf) continue;
                    // Choose the clip from how fast the entity is ACTUALLY
                    // moving, when asked to. This is what makes an NPC agent
                    // walking a navmesh path play its walk cycle without either
                    // system knowing the other exists.
                    int want_clip = smc->clip_index;
                    if (smc->drive_from_motion) {
                        auto& loco = editor_state.locomotion[ent->GetId()];
                        // A rebase teleports the entity; without this it reads
                        // as an enormous one-frame speed and snaps every
                        // character into its run cycle.
                        if (origin_shift_this_frame != glm::vec3(0.0f))
                            schizo::editor::locomotion_apply_origin_shift(loco, origin_shift_this_frame);
                        const bool mv = schizo::editor::locomotion_wants_move(
                            loco, sk_tf->GetWorldPosition(), delta_time, smc->move_threshold);
                        want_clip = mv ? smc->move_clip : smc->idle_clip;
                    }
                    if (static_cast<size_t>(want_clip) != actor->clip_index())
                        actor->set_clip(static_cast<size_t>(want_clip));
                    if (smc->playing) actor->advance(delta_time * smc->speed);
                    else              actor->refresh_pose();   // hold the pose, not bind pose
                    actor->set_model(sk_tf->GetWorldMatrix());
                }
                // Release actors whose entity is gone or no longer skinned.
                skinned_actors.prune([&sk_scene](uint32_t id) {
                    for (const auto& e : sk_scene->GetEntities())
                        if (e && e->GetId() == id) {
                            auto* c = e->GetSkinnedMeshComponent();
                            return c && c->active();
                        }
                    return false;
                });
            }

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
                    if (game_window_mode) {
                        // Game windows: first ESC releases the cursor, second
                        // quits (stopping playback would just strand a blank
                        // window — there's no editor UI to fall back to).
                        if (playing &&
                            editor_state.scene_playback_manager->IsCursorCaptured()) {
                            editor_state.scene_playback_manager->SetCursorCaptured(false);
                        } else {
                            spdlog::info("ESC — closing game window");
                            glfwSetWindowShouldClose(glfw_window, GLFW_TRUE);
                        }
                    } else if (playing) {
                        if (editor_state.scene_playback_manager->IsCursorCaptured()) {
                            editor_state.scene_playback_manager->SetCursorCaptured(false);
                        } else {
                            EndPlayMode(editor_state, editor_state.editor_scene->GetScene());
                        }
                    } else {
                        spdlog::info("ESC — exiting editor");
                        prev_esc = cur_esc;
                        break;
                    }
                }
                prev_esc = cur_esc;
            }
            if (!game_window_mode) {
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
            } // !game_window_mode (editor hotkeys)

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
            // so it doesn't conflict with the rename dialog. Disabled in game
            // windows (no editing there).
            if (!game_window_mode) {
                static bool prev_delete = false;
                bool cur_delete = key(GLFW_KEY_DELETE);
                if (cur_delete && !prev_delete &&
                    !ImGui::GetIO().WantTextInput &&
                    editor_state.selected_entity_id != 0) {
                    auto sc = editor_state.editor_scene->GetScene();
                    auto ent = sc ? sc->GetEntityById(editor_state.selected_entity_id)
                                  : nullptr;
                    if (ent) {
                        PushDeleteEntityCommand(editor_state, sc, ent);
                    }
                }
                prev_delete = cur_delete;
            }

            // Free-fly camera (WASD) — only in edit view. In Play mode (incl. a
            // multiplayer session) WASD drives the first-person character.
            float cam_spd = 0.1f;
            const bool playing_now_cam = editor_state.scene_playback_manager &&
                                         editor_state.scene_playback_manager->IsPlaying();
            if (!playing_now_cam) {
                if (key(GLFW_KEY_W))     editor_state.viewport_camera.MoveLocal( cam_spd, 0.f, 0.f);
                if (key(GLFW_KEY_S))     editor_state.viewport_camera.MoveLocal(-cam_spd, 0.f, 0.f);
                if (key(GLFW_KEY_A))     editor_state.viewport_camera.MoveLocal(0.f, -cam_spd, 0.f);
                if (key(GLFW_KEY_D))     editor_state.viewport_camera.MoveLocal(0.f,  cam_spd, 0.f);
                if (key(GLFW_KEY_SPACE)) editor_state.viewport_camera.MoveLocal(0.f, 0.f,  cam_spd);
            }

            // Multiplayer: entering a session auto-starts Play mode so each
            // instance is a real first-person character (once; if the scene has
            // no 'Player' entity it falls back to the edit view). Leaving the
            // session stops the play we started.
            if (editor_state.net_session.active() && editor_state.scene_playback_manager) {
                if (!editor_state.scene_playback_manager->IsPlaying() &&
                    !editor_state.net_autoplay_started &&
                    !editor_state.net_autoplay_failed) {
                    auto scn = editor_state.editor_scene->GetScene();
                    // Clients don't simulate props locally — their dynamic
                    // colliders become kinematic bodies that follow the host's
                    // replicated transforms (so collisions match what's drawn).
                    editor_state.scene_playback_manager->SetNetClientMode(
                        editor_state.net_session.role() == schizo::editor::NetRole::Client);
                    if (scn && editor_state.scene_playback_manager->StartPlayback(scn)) {
                        editor_state.net_autoplay_started = true;
                        spdlog::info("[net] entered Play mode for the multiplayer session");
                    } else {
                        editor_state.net_autoplay_failed = true;
                        spdlog::warn("[net] no 'Player' entity — session stays in edit view");
                    }
                }
            } else if (editor_state.net_autoplay_started &&
                       editor_state.scene_playback_manager) {
                editor_state.scene_playback_manager->StopPlayback();
                editor_state.scene_playback_manager->SetNetClientMode(false);
                editor_state.net_autoplay_started = false;
                editor_state.net_autoplay_failed  = false;
            }

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

            // Custom scripts (Stage 12): refresh the API context and drive every
            // ScriptComponent. Runs right after the physics/scene update (so
            // scripts see settled transforms) and before net replication (so a
            // host script's changes replicate the same frame).
            {
                const bool script_play = editor_state.scene_playback_manager &&
                                         editor_state.scene_playback_manager->IsPlaying();
                editor_state.script_play_time =
                    script_play ? editor_state.script_play_time + delta_time : 0.0;

                auto& ctx    = editor_state.script_ctx;
                ctx.scene    = editor_state.editor_scene->GetScene();
                ctx.playback = editor_state.scene_playback_manager.get();
                ctx.window   = glfw_window;
                ctx.bridge   = editor_state.ecs_bridge;   // gameplay ECS (G0–G4) for scripts
                ctx.dt       = delta_time;                // for continuous calls (e.g. drive)
                // Cursor delta for scripts (independent of the play-mode
                // capture pipeline, which only tracks while captured).
                {
                    static double last_sx = 0.0, last_sy = 0.0;
                    static bool   primed  = false;
                    double sx = 0.0, sy = 0.0;
                    glfwGetCursorPos(glfw_window, &sx, &sy);
                    ctx.mouse_dx = primed ? static_cast<float>(sx - last_sx) : 0.0f;
                    ctx.mouse_dy = primed ? static_cast<float>(sy - last_sy) : 0.0f;
                    last_sx = sx; last_sy = sy; primed = true;
                }
                editor_state.script_api.dt   = delta_time;
                editor_state.script_api.time = editor_state.script_play_time;
                editor_state.script_system.update(ctx.scene, script_play,
                                                  delta_time, editor_state.script_api);
            }

            // Multiplayer: report this instance's player (entity id + position;
            // the play character if playing, else the camera) and pump the
            // session. The id lets replication skip the local player. Runs after
            // the play Update so the reported position is this frame's, and so
            // host-authoritative prop transforms overwrite the local sim.
            if (editor_state.net_session.active()) {
                glm::vec3 my_pos = editor_state.viewport_camera.GetPosition();
                uint32_t  my_id  = 0;
                if (editor_state.scene_playback_manager &&
                    editor_state.scene_playback_manager->IsPlaying()) {
                    if (auto* pl = editor_state.scene_playback_manager->GetPlayerEntity()) {
                        my_pos = pl->GetTransform()->GetWorldPosition();
                        my_id  = pl->GetId();
                    }
                }
                editor_state.net_session.set_local_player(my_id, my_pos);
            }
            editor_state.net_session.tick_frame(
                editor_state.editor_scene->GetScene(), delta_time);

            // Host: ghost kinematic capsules for remote players, so clients
            // physically push dynamic props in the authoritative simulation
            // (results replicate back to everyone next snapshot).
            if (editor_state.net_session.role() == schizo::editor::NetRole::Host &&
                editor_state.scene_playback_manager &&
                editor_state.scene_playback_manager->IsPlaying()) {
                std::vector<glm::vec3> remote_players;
                editor_state.net_session.remote_player_positions(remote_players);
                editor_state.scene_playback_manager->SyncRemotePlayerBodies(
                    remote_players, delta_time);
            }

            // Game windows exist only for their session: once the host is gone
            // (was connected, now isn't), close instead of idling at a dead
            // "connecting..." screen — stale windows from a previous session
            // otherwise pile up next to the new one.
            if (game_window_mode) {
                static bool ever_connected = false;
                if (editor_state.net_session.status().connected) {
                    ever_connected = true;
                } else if (ever_connected) {
                    spdlog::info("[net] host disconnected — closing game window");
                    glfwSetWindowShouldClose(glfw_window, GLFW_TRUE);
                }
            }

            // ------------------------------------------------------------
            // Build ImGui frame (no GPU commands yet)
            // ------------------------------------------------------------
            imgui->begin_frame();

            // Runtime game-UI HUD (gws_ui) — drawn to ImGui's foreground list
            // so it overlays the viewport. No-op unless enabled in the panel.
            game_ui.update_and_render(delta_time);

            // ------------------------------------------------------------
            // Game-window mode (--net-game, spawned multiplayer clients):
            // no menu bar, no dockspace, no panels — just the rendered game
            // stretched over the whole window (the offscreen scene render
            // runs every frame regardless of which panels are drawn).
            // ------------------------------------------------------------
            if (game_window_mode) {
                ImGuiIO& gio = ImGui::GetIO();
                // ShowViewport is skipped, so feed the camera aspect manually
                // (the projection reads viewport_panel_size each frame).
                editor_state.viewport_panel_size = { gio.DisplaySize.x, gio.DisplaySize.y };
                if (editor_state.viewport_texture_id != VK_NULL_HANDLE) {
                    ImGui::GetBackgroundDrawList()->AddImage(
                        (ImTextureID)(void*)editor_state.viewport_texture_id,
                        ImVec2(0.0f, 0.0f), gio.DisplaySize);
                }
                // Minimal session HUD.
                {
                    const auto& nst = editor_state.net_session.status();
                    ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_Always);
                    ImGui::SetNextWindowBgAlpha(0.45f);
                    ImGui::Begin("##net_hud", nullptr,
                                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);
                    ImGui::Text("MULTIPLAYER  %s  |  players: %zu  |  ESC quits",
                                nst.connected ? "connected" : "connecting...",
                                nst.avatars + 1);
                    ImGui::End();
                }
            } else if (editor_state.in_launcher) {
                // --------------------------------------------------------
                // Project launcher — shown before any project is open. It
                // draws over the whole window; the editor menu/dockspace/
                // panels are skipped until a project is chosen or created.
                // --------------------------------------------------------
                schizo::project::ProjectManifest chosen;
                bool launcher_quit = false;
                if (schizo::project::draw_launcher(projects_registry, chosen, launcher_quit)) {
                    editor_state.project        = chosen;
                    editor_state.features       = chosen.features;
                    editor_state.project_loaded = true;
                    // Enter the project sandbox (project folder becomes the CWD)
                    // and re-scope the asset browser to it.
                    schizo::editor::set_project_root(chosen.project_dir);
                    if (editor_state.asset_browser) editor_state.asset_browser->Reroot();

                    // Load the project's default scene, creating it if missing.
                    const std::string scene_path = chosen.default_scene_path();
                    std::error_code sec;
                    if (std::filesystem::exists(scene_path, sec)) {
                        editor_state.editor_scene->LoadScene(scene_path);
                    } else {
                        editor_state.editor_scene->NewScene(chosen.name);
                        editor_state.editor_scene->SaveScene(scene_path);
                    }
                    editor_state.in_launcher = false;
                    spdlog::info("[project] opened '{}' ({}) features=0x{:x}",
                                 chosen.name, scene_path, editor_state.features.raw());
                }
                if (launcher_quit)
                    glfwSetWindowShouldClose(glfw_window, GLFW_TRUE);
            } else {
            // ------------------------------------------------------------
            // Unity-style docked workspace. The main menu bar is emitted
            // first so it reserves the top strip (reducing the viewport work
            // area); the dockspace then fills the rest of the window. Every
            // panel docks into this one space and tiles it — no free-floating
            // overlap. The dockspace is LOCKED (NoUndocking | NoDockingSplit)
            // so the user can only resize splitters or close panels; closing
            // a panel makes its neighbours expand to refill the window.
            // ------------------------------------------------------------
            ShowMainMenuBar(editor_state, glfw_window);
            {
                ImGuiID dockspace_id = ImGui::GetID("EditorDockSpace");
                // Build the default layout on first run (no saved dock data in
                // editor.ini yet) or when the user picks Window > Reset Layout.
                // Checked BEFORE submitting the dockspace, because submitting
                // creates an (empty) node and would defeat the null check.
                if (editor_state.request_reset_layout ||
                    ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
                    BuildEditorDockLayout(dockspace_id,
                                          ImGui::GetMainViewport()->WorkSize);
                    editor_state.request_reset_layout = false;
                    // Persist the rebuilt layout NOW, then stamp the version, so
                    // the two are committed together — a crash before this point
                    // re-triggers the migration instead of leaving the version
                    // bumped but the new panels unsaved (floating) next launch.
                    ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
                    std::ofstream("editor_layout.version") << kEditorDockLayoutVersion;
                }
                ImGui::DockSpaceOverViewport(
                    dockspace_id, ImGui::GetMainViewport(),
                    ImGuiDockNodeFlags_NoUndocking | ImGuiDockNodeFlags_NoDockingSplit);
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
                        ext != ".jpeg" && ext != ".tga" && ext != ".wav" &&
                        ext != ".mp3" && ext != ".flac" && ext != ".ogg") {
                        spdlog::warn("[Drop] Skipping unsupported file: {}", src);
                        continue;
                    }
                    fs::path dst = target_dir / src_path.filename();
                    // Copy on a worker. A dropped asset can be hundreds of MB
                    // and copying it inline froze the editor for the whole
                    // transfer, with no progress and no way out.
                    const std::string label = "Import " + src_path.filename().string();
                    editor_state.tasks.submit(label,
                        [src, dst](gws::tasks::TaskContext& ctx) {
                            std::error_code ec;
                            const auto total = fs::file_size(fs::path(src), ec);
                            ctx.set_progress(total ? 0.0f : -1.0f, "copying");

                            // Chunked rather than fs::copy_file so the task can
                            // report progress and honour cancellation. A big
                            // import that cannot be stopped is only half a fix.
                            std::ifstream in(fs::path(src), std::ios::binary);
                            std::ofstream out(dst, std::ios::binary | std::ios::trunc);
                            if (!in || !out) throw std::runtime_error("could not open source or destination");

                            std::vector<char> buf(1 << 20);
                            uintmax_t copied = 0;
                            while (in) {
                                if (ctx.cancelled()) {
                                    out.close();
                                    std::error_code rm;
                                    fs::remove(dst, rm);   // never leave a partial asset behind
                                    return;
                                }
                                in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
                                const auto got = in.gcount();
                                if (got <= 0) break;
                                out.write(buf.data(), got);
                                if (!out) throw std::runtime_error("write failed (disk full?)");
                                copied += static_cast<uintmax_t>(got);
                                if (total) ctx.set_progress(static_cast<float>(copied) /
                                                            static_cast<float>(total));
                            }
                        },
                        [&editor_state, src, dst](const gws::tasks::TaskInfo& info) {
                            // Runs on the editor thread, so touching the asset
                            // browser here is legal.
                            using S = gws::tasks::TaskState;
                            if (info.state == S::Succeeded) {
                                spdlog::info("[Drop] Imported {} -> {}", src, dst.string());
                                if (editor_state.asset_browser)
                                    editor_state.asset_browser->RefreshAssets();
                            } else if (info.state == S::Cancelled) {
                                spdlog::info("[Drop] Import cancelled: {}", src);
                            } else {
                                spdlog::error("[Drop] Failed to import {} -> {}: {}",
                                              src, dst.string(), info.error);
                            }
                        });
                    ++imported;
                }
                editor_state.dropped_files.clear();
                (void)imported;
            }

            // (ShowMainMenuBar is emitted above, before the dockspace.)
            ShowSaveDialog(editor_state);
            ShowOpenDialog(editor_state);
            ShowRenameDialog(editor_state);
            ShowPlayChangesDialog(editor_state);
            ShowTaskPanel(editor_state);
            ShowSceneReloadPrompt(editor_state);
            if (editor_state.show_demo_window)
                ImGui::ShowDemoWindow(&editor_state.show_demo_window);
            ShowViewport(editor_state);
            ShowSceneHierarchy(editor_state);
            ShowInspector(editor_state);
            ShowAssetBrowser(editor_state);
            ShowPlaybackControls(editor_state);
            ShowPerformanceOverlay(editor_state);

            // Transient status toast (mesh apply/import result, etc.), top-center.
            if (!editor_state.status_message.empty()) {
                const double age = ImGui::GetTime() - editor_state.status_message_time;
                if (age > 5.0) {
                    editor_state.status_message.clear();
                } else {
                    const float alpha = age < 4.0 ? 1.0f : static_cast<float>(1.0 - (age - 4.0));
                    const ImGuiViewport* vp = ImGui::GetMainViewport();
                    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                                   vp->WorkPos.y + 42.0f),
                                            ImGuiCond_Always, ImVec2(0.5f, 0.0f));
                    ImGui::SetNextWindowBgAlpha(0.85f * alpha);
                    const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                               ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
                    if (ImGui::Begin("##status_toast", nullptr, f)) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, alpha));
                        ImGui::TextUnformatted(editor_state.status_message.c_str());
                        ImGui::PopStyleColor();
                    }
                    ImGui::End();
                }
            }

            // Feature-gated panels: only shown if the project enables the system.
            if (editor_state.feature_on(schizo::project::Feature::Networking))
                ShowNetworkPanel(editor_state);
            if (editor_state.feature_on(schizo::project::Feature::Combat))
                ShowDebugPanels(editor_state);
            // Project Settings — add/remove features on the open project. Saving
            // the manifest persists the change for next launch.
            if (editor_state.show_project_settings) {
                if (schizo::project::draw_feature_settings(editor_state.features,
                                                           &editor_state.show_project_settings)) {
                    editor_state.project.features = editor_state.features;
                    if (!editor_state.project.project_dir.empty()) {
                        const std::string mp =
                            (std::filesystem::path(editor_state.project.project_dir) /
                             schizo::project::kManifestFilename).string();
                        schizo::project::ProjectManifest::save(mp, editor_state.project);
                    }
                }
            }
            // "Output" — the editor's own log output mirrored into a panel.
            if (editor_state.show_output) {
                if (!editor_state.console)
                    editor_state.console =
                        std::make_unique<schizo::editor::ConsolePanel>();
                editor_state.console->Render(&editor_state.show_output);
            }
            // Embedded OS shell terminal — created on first show so the shell
            // isn't spawned until the panel is opened; kept alive across hides.
            if (editor_state.show_terminal) {
                if (!editor_state.terminal)
                    editor_state.terminal =
                        std::make_unique<schizo::editor::TerminalPanel>();
                editor_state.terminal->Render(&editor_state.show_terminal);
            }
            if (editor_state.asset_import_dialog &&
                editor_state.asset_import_dialog->IsOpen())
                editor_state.asset_import_dialog->RenderDialog();
            ShowPreferences(editor_state);

            // Command palette (4.2). Ctrl+P opens it. Drawn last among the
            // panels so it sits above them, and checked with the Ctrl modifier
            // rather than a raw key so it cannot fire while someone is typing
            // a name into a text field.
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P, false))
                schizo::editor::open_command_palette(editor_state.show_command_palette);
            if (editor_state.show_command_palette) {
                schizo::editor::draw_command_palette(
                    editor_state.show_command_palette,
                    editor_state.commands,
                    editor_state.editor_scene->GetScene(),
                    [&editor_state](uint32_t id) { editor_state.selected_entity_id = id; });
            }

            // Post-processing controls — inline here because post_processing
            // lives in main()'s scope (the free Show* helpers only get
            // EditorState). Toggles + live sliders for every effect.
            if (post_processing && editor_state.show_post_processing) {
                ImGui::Begin("Post-Processing", &editor_state.show_post_processing);  // docked window = child; End() must always run
                {
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

                    // Volumetric sun lighting / light shafts (god rays).
                    if (volumetric_light) {
                        ImGui::Separator();
                        ImGui::TextUnformatted("Volumetric Light (god rays)");
                        bool vol = volumetric_light->is_enabled();
                        if (ImGui::Checkbox("Enable##vol", &vol))
                            volumetric_light->set_enabled(vol);
                        if (vol) {
                            auto& vc = volumetric_light->mutable_config();
                            ImGui::SliderFloat("  Intensity##vol",   &vc.intensity,    0.0f, 5.0f);
                            ImGui::SliderFloat("  Density##vol",      &vc.density,      0.0f, 0.2f, "%.3f");
                            ImGui::SliderFloat("  Anisotropy##vol",   &vc.anisotropy,   0.0f, 0.95f);
                            ImGui::SliderFloat("  Max distance##vol", &vc.max_distance, 10.0f, 300.0f);
                            ImGui::SliderInt  ("  Steps##vol",        &vc.num_steps,    8, 128);
                        }
                    }

                    // Froxel volumetric fog (Stage 3.3).
                    if (froxel_fog) {
                        ImGui::Separator();
                        ImGui::TextUnformatted("Volumetric Fog (froxel)");
                        bool fon = froxel_fog->is_enabled();
                        if (ImGui::Checkbox("Enable##fog", &fon))
                            froxel_fog->set_enabled(fon);
                        if (fon) {
                            auto& fc = froxel_fog->mutable_config();
                            ImGui::SliderFloat("  Density##fog",       &fc.density,        0.0f, 0.08f, "%.4f");
                            ImGui::SliderFloat("  Height base##fog",   &fc.height_base,   -50.0f, 100.0f);
                            ImGui::SliderFloat("  Height falloff##fog",&fc.height_falloff, 0.0f, 0.5f, "%.3f");
                            ImGui::SliderFloat("  Range##fog",         &fc.max_distance,  20.0f, 400.0f);
                            ImGui::SliderFloat("  Sun scatter##fog",   &fc.sun_intensity,  0.0f, 3.0f);
                            ImGui::SliderFloat("  Anisotropy##fog",    &fc.anisotropy,     0.0f, 0.9f);
                            ImGui::SliderFloat("  Local lights##fog",  &fc.local_intensity,0.0f, 4.0f);
                            ImGui::ColorEdit3 ("  Ambient##fog",       &fc.ambient.x);
                        }
                    }

                    // Animation test rig (Stage 5) — procedural skinned biped.
                    if (anim_demo) {
                        ImGui::Separator();
                        ImGui::TextUnformatted("Animation Demo (skinned)");
                        bool aon = anim_demo->enabled();
                        if (ImGui::Checkbox("Enable##animdemo", &aon))
                            anim_demo->set_enabled(aon);
                        ImGui::TextDisabled("  boxy biped @ origin: state machine +");
                        ImGui::TextDisabled("  root motion + foot IK + GPU skinning");
                    }

                    // Runtime game-UI HUD (gws_ui framework).
                    {
                        ImGui::Separator();
                        ImGui::TextUnformatted("Game UI HUD (gws_ui)");
                        bool uon = game_ui.enabled();
                        if (ImGui::Checkbox("Enable##gameui", &uon))
                            game_ui.set_enabled(uon);
                        ImGui::TextDisabled("  anchored HUD: health/XP/ability bars,");
                        ImGui::TextDisabled("  resolution-scaled, over the viewport");
                    }

                    // DDGI probe-grid global illumination (Master Plan §3.2).
                    if (ddgi) {
                        ImGui::Separator();
                        ImGui::TextUnformatted("Global Illumination (DDGI)");
                        bool gon = ddgi->is_enabled();
                        if (ImGui::Checkbox("Enable##ddgi", &gon)) {
                            ddgi->set_enabled(gon);
                            if (gon) ddgi_autofit_pending = true;  // fit on enable
                        }
                        if (gon) {
                            auto& gc = ddgi->mutable_config();
                            if (ImGui::Button("Fit grid to scene##ddgi"))
                                ddgi_autofit_pending = true;
                            ImGui::SameLine();
                            ImGui::TextDisabled("(auto on enable)");
                            ImGui::SliderFloat ("  Intensity##ddgi",   &gc.intensity,   0.0f, 4.0f);
                            ImGui::SliderFloat ("  Hysteresis##ddgi",  &gc.hysteresis,  0.5f, 0.995f, "%.3f");
                            ImGui::SliderFloat ("  Normal bias##ddgi", &gc.normal_bias, 0.0f, 1.0f);
                            ImGui::DragFloat3  ("  Grid origin##ddgi", &gc.origin.x,    0.5f);
                            ImGui::DragFloat3  ("  Probe spacing##ddgi", &gc.spacing.x, 0.1f, 0.5f, 10.0f);
                            ImGui::TextDisabled("  %u probes (%dx%dx%d, fixed at startup)",
                                                ddgi->probe_count(), gc.counts.x,
                                                gc.counts.y, gc.counts.z);
                        }
                    }

                    // Volumetric clouds.
                    if (clouds) {
                        ImGui::Separator();
                        ImGui::TextUnformatted("Volumetric Clouds");
                        bool con = clouds->is_enabled();
                        if (ImGui::Checkbox("Enable##cloud", &con))
                            clouds->set_enabled(con);
                        if (con) {
                            auto& cc = clouds->mutable_config();
                            ImGui::SliderFloat("  Coverage##cloud",   &cc.coverage,    0.0f, 1.0f);
                            ImGui::SliderFloat("  Cloud size##cloud",  &cc.shape_scale, 0.0004f, 0.0040f, "%.4f");
                            ImGui::SliderFloat("  Wispiness##cloud",   &cc.detail_strength, 0.0f, 1.0f);
                            ImGui::SliderFloat("  Brightness##cloud",  &cc.brightness,  0.5f, 6.0f);
                            ImGui::SliderFloat("  Density##cloud",     &cc.density,     0.1f, 4.0f);
                            ImGui::SliderFloat("  Extinction##cloud",  &cc.extinction,  0.01f, 0.4f, "%.3f");
                            ImGui::SliderFloat("  Wind speed##cloud",  &cc.wind_speed,  0.0f, 40.0f);
                            ImGui::SliderInt  ("  March steps##cloud", &cc.march_steps, 16, 128);
                            ImGui::SliderInt  ("  Light steps##cloud", &cc.light_steps, 2, 12);
                            ImGui::SliderFloat("  Bottom alt##cloud",  &cc.cloud_bottom, 100.0f, 2000.0f);
                            ImGui::SliderFloat("  Top alt##cloud",     &cc.cloud_top,    200.0f, 4000.0f);
                            ImGui::SliderFloat("  Sky flatness##cloud", &cc.earth_radius, 10000.0f, 200000.0f, "%.0f");
                            ImGui::SliderFloat("  Max distance##cloud", &cc.max_march, 500.0f, 12000.0f, "%.0f");
                            // Render-resolution scale (recreates the cloud buffers).
                            const char* res_items[] = { "Quarter", "Half", "Full" };
                            const float res_scales[] = { 0.25f, 0.5f, 1.0f };
                            int res_cur = (cc.resolution_scale <= 0.3f) ? 0
                                        : (cc.resolution_scale >= 0.9f ? 2 : 1);
                            if (ImGui::Combo("  Resolution##cloud", &res_cur, res_items, 3)) {
                                cc.resolution_scale = res_scales[res_cur];
                                clouds->rebuild_targets();
                            }
                            ImGui::Checkbox   ("  Temporal accumulation##cloud", &cc.temporal);
                        }
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
            } // end of editor UI (skipped entirely in game-window mode)

            // ------------------------------------------------------------
            // GPU frame
            // ------------------------------------------------------------
            vkWaitForFences(device.get_device(), 1, &frame_fences[current_frame],
                            VK_TRUE, UINT64_MAX);
            // The previous frame's occlusion queries are guaranteed available
            // after its fence. Pull the results into the culler so this
            // frame can skip occluded draws.
            if (graph) graph->resolve_occlusion_queries();
            // Same fence guarantees the GPU finished the previous frame's
            // timestamp writes — pull per-pass GPU timings into the profiler
            // the Stage 14 Performance overlay reads (N2). Only feeds when the
            // device supports timestamps; otherwise the GPU section stays empty.
            if (graph && graph->resolve_timings()) graph->update_gpu_profiler();
            // Same story for the HZB readback — the GPU finished copying the
            // HZB mip to the host buffer before signalling this fence.
            if (hzb_culler) hzb_culler->pull_readback();
            uint32_t image_index = swapchain->acquire_next_image(
                acquire_sems[current_frame]);
            if (image_index == UINT32_MAX) {
                // Swapchain went out of date between the top-of-loop size check
                // and here (mid-drag resize / SUBOPTIMAL surface). Recreate and
                // re-acquire once; if still bad, discard this already-built
                // ImGui frame cleanly (balance NewFrame + profiler) and skip.
                recreate_swapchain_resources();
                image_index = swapchain->acquire_next_image(acquire_sems[current_frame]);
                if (image_index == UINT32_MAX) {
                    ImGui::EndFrame();
                    GWS_PROFILE_FRAME_END();
                    continue;
                }
            }
            vkResetFences(device.get_device(), 1, &frame_fences[current_frame]);

            VkCommandBuffer cmd = frame_cmds[current_frame];
            vkResetCommandBuffer(cmd, 0);

            VkCommandBufferBeginInfo begin_info{
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &begin_info);

            // Record the skinned-rig compute skinning FIRST, so its output
            // vertex buffer is ready before the RT-BLAS build, shadow pass, and
            // geometry pass all read it this frame.
            if (anim_demo && anim_demo->enabled()) anim_demo->record_skin(cmd);
            if (imported_actor) imported_actor->record_skin(cmd);
            // Skinning dispatch for every per-entity character, before the
            // shadow/geometry passes read the skinned vertex buffers.
            if (auto sk_scene2 = editor_state.editor_scene->GetScene()) {
                for (const auto& ent : sk_scene2->GetEntities()) {
                    if (!ent) continue;
                    auto* smc = ent->GetSkinnedMeshComponent();
                    if (!smc || !smc->active()) continue;
                    if (auto* a = skinned_actors.get_or_create(
                            ent->GetId(), smc->gltf_path, &device,
                            mat_layout, mat_pool, &texture_manager))
                        a->record_skin(cmd);
                }
            }

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
                // Per-light frustum culling: a local light whose influence
                // sphere (position, range) can't reach anything the camera sees
                // is skipped, so we don't burn one of the 128 light slots on it.
                // Directional lights are infinite, so they're never culled.
                const gws::renderer::gpu::Frustum light_frustum =
                    gws::renderer::gpu::Frustum::from_matrix(cam.proj * cam.view);
                uint32_t lights_total = 0, lights_culled = 0;
                const auto& scene_entities = editor_scene.GetScene()->GetEntities();
                for (const auto& entity : scene_entities) {
                    if (!entity || !entity->IsActiveInHierarchy()) continue;
                    auto lc = entity->GetComponent<schizo::scene::LightComponent>();
                    if (!lc || !lc->IsEnabled()) continue;
                    ++lights_total;
                    if (lc->GetType() != schizo::scene::LightType::Directional &&
                        !light_frustum.is_sphere_visible(lc->GetPosition(), lc->GetRange())) {
                        ++lights_culled;
                        continue;
                    }
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
                            const float inner_cos = glm::cos(glm::radians(angles_deg.x));
                            // Cookie/gobo: load (cached by path), bind, and build
                            // the projection matrix the shader samples it through.
                            glm::mat4 cookie_vp(1.0f);
                            bool      has_cookie = false;
                            const std::string& ck = lc->GetCookiePath();
                            if (!ck.empty()) {
                                if (ck != cookie_path_loaded) {
                                    cookie_tex = gws::renderer::gpu::Texture::create_from_file(
                                        &device, ck, /*srgb=*/true);
                                    cookie_path_loaded = ck;
                                    if (cookie_tex)
                                        lighting->set_cookie(cookie_tex->view(), cookie_tex->sampler());
                                    else
                                        spdlog::warn("[cookie] failed to load: {}", ck);
                                }
                                if (cookie_tex) {
                                    const glm::vec3 pos = lc->GetPosition();
                                    const glm::vec3 dir = glm::normalize(
                                        glm::length(lc->GetDirection()) > 0.0f
                                            ? lc->GetDirection() : glm::vec3(0, -1, 0));
                                    const glm::vec3 up = (std::abs(dir.y) > 0.99f)
                                        ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
                                    const float fov = glm::radians(
                                        glm::clamp(angles_deg.y * 2.0f, 5.0f, 170.0f));
                                    const float rng = std::max(lc->GetRange(), 0.5f);
                                    cookie_vp = glm::perspective(fov, 1.0f, 0.05f, rng) *
                                                glm::lookAt(pos, pos + dir, up);
                                    has_cookie = true;
                                }
                            }
                            lighting->add_spot_light(lc->GetPosition(), lc->GetDirection(),
                                                     color, intensity, lc->GetRange(),
                                                     outer_cos, inner_cos, shadow,
                                                     cookie_vp, has_cookie);
                            break;
                        }
                        case schizo::scene::LightType::Area: {
                            // Rect lies in the entity's local XY plane; -Y so the
                            // emitting normal points along the entity's forward (-Z).
                            const glm::quat rot = lc->GetRotation();
                            const glm::vec2 sz  = lc->GetAreaSize();
                            const glm::vec3 right = rot * glm::vec3(sz.x * 0.5f, 0.0f, 0.0f);
                            const glm::vec3 up    = rot * glm::vec3(0.0f, -sz.y * 0.5f, 0.0f);
                            lighting->add_area_light(lc->GetPosition(), right, up, color,
                                                     intensity, lc->GetRange(),
                                                     lc->IsTwoSided());
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
                if (lights_culled > 0) {
                    static int light_cull_log = 0;
                    if ((light_cull_log++ % 240) == 0)
                        spdlog::debug("light cull: {}/{} local lights skipped (off-screen)",
                                      lights_culled, lights_total);
                }
            }

            // Build per-frame draw list from the editor scene's entities.
            // Entities with a MeshRendererComponent become DrawItems; others
            // (lights, empty parents) are skipped.
            mat_cache.prune(editor_scene.GetScene());
            terrain_cache.prune(editor_scene.GetScene());
            terrain_gpu_cache.prune(editor_scene.GetScene());

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
              ecs_bridge.sync_and_run(editor_scene.GetScene());
              ecs_bridge.tick_gameplay(delta_time); }   // G0: effects/abilities/triggers/timers/events

            // Scene logic graph: start/stop with Play, and feed key presses to
            // On Key nodes while playing.
            {
                static bool logic_was_playing = false;
                const bool logic_playing = editor_state.scene_playback_manager &&
                                           editor_state.scene_playback_manager->IsPlaying();
                if (logic_playing && !logic_was_playing) ecs_bridge.start_logic();
                if (!logic_playing && logic_was_playing) ecs_bridge.stop_logic();
                logic_was_playing = logic_playing;
                if (logic_playing) {
                    ecs_bridge.logic_tick(delta_time);   // On Tick / On Flag
                    static bool logic_prev_key[128] = {false};
                    for (int k = 32; k < 97; ++k) {   // space..'`' (letters/digits/common)
                        const bool down = glfwGetKey(glfw_window, k) == GLFW_PRESS;
                        if (down && !logic_prev_key[k]) ecs_bridge.logic_on_key(k);      // On Key
                        if (!down && logic_prev_key[k]) ecs_bridge.logic_on_key_up(k);   // On Key Up
                        logic_prev_key[k] = down;
                    }
                }
            }

            // Draw the gameplay UI (HUD + inventory/character/quest windows) for any
            // entity a script opened via "ui.*" intent tags. Scripts can't draw ImGui,
            // so they set the intent tags and the engine renders it here.
            schizo::editor::draw_gameplay_ui(ecs_bridge);
            if (editor_state.show_logic_graph)
                schizo::editor::draw_logic_graph_panel(
                    ecs_bridge, &editor_state.show_logic_graph,
                    &editor_state.logic_edits,
                    [&editor_state, &ecs_bridge](const schizo::editor::CoalescedEdit& e) {
                        PushLogicGraphCommand(editor_state, &ecs_bridge, e);
                    });

            { GWS_PROFILE_ZONE("build_draw_items");
              schizo::editor::build_draw_items(
                  editor_scene.GetScene(), prim_cache, mat_cache, asset_cache,
                  terrain_cache, terrain_gpu_cache, &device, mat_layout, mat_pool,
                  opaque_draws, transparent_draws, &texture_manager, &ecs_bridge); }

            // Add the skinned test rig to the opaque list (drawn + shadowed).
            // Its vertex buffer was already skinned above; the bind-pose bounds
            // are conservative for culling.
            if (anim_demo && anim_demo->enabled())
                opaque_draws.push_back(anim_demo->draw_item());
            if (imported_actor)
                opaque_draws.push_back(imported_actor->draw_item());
            // Per-entity skinned characters join the opaque list like any other
            // draw, so they get culling, shadows and the G-buffer for free.
            if (auto sk_scene3 = editor_state.editor_scene->GetScene()) {
                for (const auto& ent : sk_scene3->GetEntities()) {
                    if (!ent) continue;
                    auto* smc = ent->GetSkinnedMeshComponent();
                    if (!smc || !smc->active()) continue;
                    if (auto* a = skinned_actors.get_or_create(
                            ent->GetId(), smc->gltf_path, &device,
                            mat_layout, mat_pool, &texture_manager))
                        opaque_draws.push_back(a->draw_item());
                }
            }

            // DDGI grid auto-fit: enclose the scene's world AABB (from the
            // opaque draw list) with the fixed probe grid so probes actually
            // cover the geometry. Runs on first enable / "Fit to scene". A
            // grid that doesn't fit is the #1 reason DDGI looks flat or wrong.
            if (ddgi && ddgi_autofit_pending && !opaque_draws.empty()) {
                glm::vec3 mn( std::numeric_limits<float>::infinity());
                glm::vec3 mx(-std::numeric_limits<float>::infinity());
                for (const auto& d : opaque_draws) {
                    if (!d.mesh) continue;
                    const auto& lb = d.mesh->bounding_box();
                    for (int c = 0; c < 8; ++c) {
                        const glm::vec4 corner((c & 1) ? lb.max.x : lb.min.x,
                                               (c & 2) ? lb.max.y : lb.min.y,
                                               (c & 4) ? lb.max.z : lb.min.z, 1.0f);
                        const glm::vec3 w = glm::vec3(d.model * corner);
                        mn = glm::min(mn, w);
                        mx = glm::max(mx, w);
                    }
                }
                if (mn.x <= mx.x) {
                    auto& gc = ddgi->mutable_config();
                    const glm::vec3 pad = (mx - mn) * 0.06f + glm::vec3(0.75f);
                    const glm::vec3 lo  = mn - pad, hi = mx + pad;
                    const glm::vec3 ext = glm::max(hi - lo, glm::vec3(1.0f));
                    const glm::vec3 div =
                        glm::max(glm::vec3(gc.counts) - glm::vec3(1.0f), glm::vec3(1.0f));
                    gc.origin       = lo;
                    gc.spacing      = ext / div;
                    gc.max_ray_dist = glm::length(ext) * 1.1f;  // reach across the scene
                    ddgi_autofit_pending = false;
                    spdlog::info("DDGI: grid fit to AABB ({:.1f},{:.1f},{:.1f})..({:.1f},{:.1f},{:.1f}), "
                                 "spacing ({:.2f},{:.2f},{:.2f})",
                                 lo.x, lo.y, lo.z, hi.x, hi.y, hi.z,
                                 gc.spacing.x, gc.spacing.y, gc.spacing.z);
                }
            }

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

            // Front-to-back sort of the opaque list (early-z: near objects
            // fill the depth buffer first, so far fragments fail the depth
            // test before shading — big win on overdraw-heavy scenes). Ties
            // broken by material so identical materials draw back-to-back
            // and the descriptor rebind count drops. The WBOIT transparent
            // list is order-independent — left untouched.
            {
                GWS_PROFILE_ZONE("draw_sort");
                const size_t n = opaque_draws.size();
                std::vector<float> depth_key(n);
                for (size_t i = 0; i < n; ++i) {
                    const auto& d = opaque_draws[i];
                    const glm::vec3 c = d.mesh
                        ? glm::vec3(d.model * glm::vec4(d.mesh->bounding_box().center(), 1.0f))
                        : glm::vec3(d.model[3]);
                    depth_key[i] = -(cam.view * glm::vec4(c, 1.0f)).z;  // + = in front
                }
                std::vector<uint32_t> order(n);
                for (size_t i = 0; i < n; ++i) order[i] = static_cast<uint32_t>(i);
                std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
                    if (depth_key[a] != depth_key[b]) return depth_key[a] < depth_key[b];
                    return opaque_draws[a].material < opaque_draws[b].material;
                });
                std::vector<gws::renderer::gpu::DrawItem> sorted;
                sorted.reserve(n);
                for (uint32_t idx : order) sorted.push_back(opaque_draws[idx]);
                opaque_draws.swap(sorted);
            }

            // HZB occlusion test for the opaque draw list — projects each
            // entity's world-space AABB into NDC and compares against the
            // CPU-side HZB from the previous frame. Filtering happens HERE
            // (indices line up with opaque_draws) — the render graph no
            // longer applies was_visible() itself, because it frustum-culls
            // its copy first and the shifted indices read the wrong draw's
            // visibility (the old "random objects disappear" bug).
            std::vector<gws::renderer::gpu::DrawItem> geometry_draws;
            bool hzb_did_filter = false;
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
                // Camera-motion gate: the pyramid is one frame old, so on a
                // cut or fast pan/rotation the test would falsely cull
                // objects the old depth never saw. Skip occlusion those
                // frames (frustum culling still applies).
                const glm::vec3 view_fwd = glm::normalize(
                    glm::vec3(-cam.view[0][2], -cam.view[1][2], -cam.view[2][2]));
                const bool camera_moved_fast = hzb_prev_valid &&
                    (glm::length(cam.position - hzb_prev_cam_pos) > 1.0f ||
                     glm::dot(view_fwd, hzb_prev_cam_fwd) < 0.996f);

                if (draw_n && aabb_mins && aabb_maxs &&
                    hzb_prev_valid && !camera_moved_fast) {
                    // Test with the view-proj of the frame that BUILT the
                    // current pyramid — never the current camera.
                    hzb_culler->test_visibility(aabb_mins, aabb_maxs, draw_n,
                                                hzb_prev_vp);

                    // Filter with 2-frame hysteresis: only drop a draw that
                    // tested occluded two frames running. Keyed by mesh +
                    // submesh + quantized position (primitives SHARE Mesh
                    // objects across entities, so the mesh pointer alone
                    // would alias different instances).
                    auto draw_key = [](const gws::renderer::gpu::DrawItem& d) -> uint64_t {
                        uint64_t h = 1469598103934665603ull;
                        auto mix = [&h](uint64_t v) { h ^= v; h *= 1099511628211ull; };
                        mix(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(d.mesh)));
                        mix(d.submesh_index);
                        const glm::vec3 t = glm::vec3(d.model[3]);
                        mix(static_cast<uint64_t>(static_cast<int64_t>(t.x * 100.0f)));
                        mix(static_cast<uint64_t>(static_cast<int64_t>(t.y * 100.0f)));
                        mix(static_cast<uint64_t>(static_cast<int64_t>(t.z * 100.0f)));
                        return h;
                    };
                    geometry_draws.reserve(draw_n);
                    std::unordered_map<uint64_t, uint8_t> next_streak;
                    next_streak.reserve(hzb_occluded_streak.size() + 8);
                    for (size_t i = 0; i < draw_n; ++i) {
                        if (hzb_culler->was_visible(static_cast<uint32_t>(i))) {
                            geometry_draws.push_back(opaque_draws[i]);
                            continue;
                        }
                        const uint64_t key = draw_key(opaque_draws[i]);
                        auto sit = hzb_occluded_streak.find(key);
                        const uint8_t streak = static_cast<uint8_t>(
                            (sit != hzb_occluded_streak.end() ? sit->second : 0) + 1);
                        next_streak[key] = std::min<uint8_t>(streak, 8);
                        if (streak < 2) geometry_draws.push_back(opaque_draws[i]);
                    }
                    hzb_occluded_streak.swap(next_streak);
                    hzb_did_filter = true;
                } else {
                    // No trustworthy pyramid this frame — everything visible,
                    // and streaks reset so nothing is culled on stale data.
                    hzb_occluded_streak.clear();
                }

                // Next frame's pyramid is built from THIS frame's depth, so
                // remember this frame's camera for next frame's test.
                hzb_prev_vp      = cam.proj * cam.view;
                hzb_prev_cam_pos = cam.position;
                hzb_prev_cam_fwd = view_fwd;
                hzb_prev_valid   = true;
            }
            graph->set_draw_items(hzb_did_filter ? geometry_draws : opaque_draws);

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

            // Stage 6: drive per-entity AudioSource/AudioListener components.
            // The listener defaults to the editor camera (basis from the view
            // matrix rows: right/up/-forward in world) when no AudioListener
            // entity is active.
            if (audio.running()) {
                const glm::mat3 r(cam.view);
                gws::audio::Listener cam_lis;
                cam_lis.position    = cam.position;
                cam_lis.forward     = -glm::vec3(r[0][2], r[1][2], r[2][2]);
                cam_lis.up          =  glm::vec3(r[0][1], r[1][1], r[2][1]);
                cam_lis.master_gain = 1.0f;

                const bool play_mode = editor_state.scene_playback_manager &&
                                       editor_state.scene_playback_manager->IsPlaying();
                auto scene_for_audio = editor_state.editor_scene->GetScene();
                // No occlusion in edit mode (scene colliders aren't in a physics
                // world here). Play-mode scene occlusion is a follow-up.
                audio_driver.update(scene_for_audio.get(), cam_lis, nullptr, delta_time, play_mode);
            }

            // N5 frame capture: when armed (from the Performance overlay),
            // snapshot this frame's per-stage submission lists. One-shot + a
            // single bool test when not armed, so it's free on normal frames.
            {
                auto& fc = gws::profile::FrameCapture::instance();
                if (fc.begin(frame_count)) {
                    const auto cap_list =
                        [&](const char* pass,
                            const std::vector<gws::renderer::gpu::DrawItem>& list) {
                            for (const auto& di : list) {
                                gws::profile::CapturedDraw d;
                                d.pass    = pass;
                                d.submesh = di.submesh_index;
                                d.blend   = di.is_blend;
                                char id[32];
                                std::snprintf(id, sizeof id, "mesh@%p",
                                              static_cast<const void*>(di.mesh));
                                d.label = id;
                                if (di.mesh) {
                                    d.vertices = di.mesh->vertex_count();
                                    const auto& subs = di.mesh->submeshes();
                                    d.indices = (di.submesh_index < subs.size())
                                        ? subs[di.submesh_index].index_count()
                                        : di.mesh->index_count();
                                }
                                fc.add(std::move(d));
                            }
                        };
                    cap_list("Geometry",    opaque_draws);
                    cap_list("Transparent", transparent_draws);
                    cap_list("Shadow",      shadow_draws);
                    fc.end();
                    spdlog::info("[frame-capture] frame {} -> {} draws, {} triangles",
                                 frame_count, fc.last().draw_count(), fc.last().total_triangles());
                }
            }

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
                if (ddgi) {
                    ddgi->set_tlas(rt_scene->get_tlas_handle());
                    ddgi->set_instance_data_buffer(rt_scene->get_instance_data_buffer());
                    // Probe-hit shading uses the same sun + ambient as the
                    // deferred lighting so bounced light matches direct light.
                    ddgi->set_sun(sun_dir_for_rt, sun_color_for_rt);
                    ddgi->mutable_config().ambient =
                        l_cfg.ambient_color * l_cfg.global_ambient;
                }
                static int rt_log_throttle = 0;
                if ((rt_log_throttle++ % 120) == 0) {
                    // debug level: kept out of the default (info) console + the
                    // Output panel so it doesn't spam every frame; raise the log
                    // level to see it.
                    spdlog::debug("RT scene update: {} instances",
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
            // Cull shadow casters against the LIGHT'S frustum — a perf-only
            // filter. Never cull casters with the CAMERA frustum: an object
            // outside the camera view whose shadow falls INTO the view must
            // still render into the shadow map.
            if (shadow_active && graph->is_frustum_culling_enabled()) {
                GWS_PROFILE_ZONE("shadow_light_cull");
                gws::renderer::gpu::Frustum light_frustum =
                    gws::renderer::gpu::Frustum::from_matrix(shadow_view_proj);
                gws::renderer::gpu::cull_draw_items_frustum(shadow_draws, light_frustum);
            }

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
            // HZB occlusion: build the depth pyramid from THIS frame's depth
            // (compute downsample chain) + copy the readback mip to a host
            // buffer. pull_readback() next frame feeds test_visibility(), which
            // ran at the top of THIS frame against the PREVIOUS frame's pyramid.
            // Without this call the CPU HZB is never populated — the reason the
            // occlusion test was previously dropping objects (garbage data).
            if (hzb_culler) hzb_culler->build_and_readback(cmd);
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
            // Cloud shadow map — produced before lighting so the deferred
            // shading + light shafts can sample where clouds occlude the sun.
            if (clouds) {
                clouds->compute_shadow_map(cmd, cam.position);
                lighting->set_cloud_shadow_params(clouds->get_shadow_params());
            }
            // DDGI probe trace — rays vs this frame's TLAS (built above),
            // recorded before Lighting so the composite right after it adds
            // up-to-date bounce light. The pass itself no-ops until the TLAS
            // + instance buffer are bound and DDGI is enabled in the panel.
            if (ddgi) ddgi->execute_trace(cmd);
            graph->execute_stage(cmd, RenderGraphStage::Lighting,   {});
            // DDGI composite — adds probe irradiance (indirect diffuse) onto
            // the freshly lit HDR before SSR/clouds/shafts/fog layer over it.
            if (ddgi) ddgi->execute_composite(cmd);
            // SSR — compute reflections + composite into HDR before
            // transparent fragments are drawn. Runs outside the render
            // graph because the graph models only render-pass stages and
            // SSR is a compute + render-pass pair owned by its own class.
            if (ssr) {
                ssr->set_cloud_sky_enabled(clouds && clouds->clouds_visible());
                ssr->execute(cmd, cam.view, cam.proj, cam.position);
            }
            // Water surfaces — collect every active WaterComponent and render
            // them into the HDR target (before clouds/shafts so atmospherics
            // layer on top of the water).
            if (water_pass) {
                water_pass->begin_frame();
                if (auto wscene = editor_state.editor_scene->GetScene()) {
                    for (const auto& went : wscene->GetEntities()) {
                        if (!went || !went->IsActiveInHierarchy()) continue;
                        // Standalone water surfaces.
                        if (auto wc = went->GetComponent<schizo::scene::WaterComponent>()) {
                            water_pass->add_water(
                                went->GetTransform()->GetWorldPosition(),
                                wc->GetSize(), wc->GetDeepColor(), wc->GetShallowColor(),
                                wc->GetWaveHeight(), wc->GetWaveSpeed(), wc->GetWaveScale(),
                                wc->GetClarity(), wc->GetReflectivity());
                        }
                        // Terrain-integrated water: one surface covering the
                        // terrain rect at origin.y + water level.
                        if (auto twc = went->GetComponent<schizo::scene::TerrainComponent>()) {
                            if (twc->IsWaterEnabled()) {
                                glm::vec3 base = went->GetTransform()->GetWorldPosition();
                                base.y += twc->GetWaterLevel();
                                water_pass->add_water(
                                    base, glm::vec2(twc->GetSize()),
                                    twc->GetWaterDeepColor(), twc->GetWaterShallowColor(),
                                    twc->GetWaterWaveHeight(), twc->GetWaterWaveSpeed(),
                                    twc->GetWaterWaveScale(), twc->GetWaterClarity(),
                                    twc->GetWaterReflectivity());
                            }
                        }
                    }
                }
                static float water_time = 0.0f;
                water_time += delta_time;
                water_pass->set_sun(sun_dir_for_rt, sun_color_for_rt);
                water_pass->execute(cmd, cam.view, cam.proj, cam.position, water_time);
            }

            // Volumetric clouds — distant sky layer; composite before the
            // light shafts so shafts scatter in front of the clouds.
            if (clouds) {
                clouds->set_sun(sun_dir_for_rt, sun_color_for_rt);
                clouds->set_ambient(l_cfg.ambient_color);
                clouds->execute(cmd, cam.view, cam.proj, cam.position);
            }
            // Volumetric sun lighting / light shafts — feed it the same sun +
            // shadow VP the deferred lighting/shadow stage used, then march.
            if (volumetric_light) {
                volumetric_light->set_sun(sun_dir_for_rt, sun_color_for_rt);
                volumetric_light->set_shadow_matrix(shadow_view_proj);
                if (clouds) volumetric_light->set_cloud_shadow_params(clouds->get_shadow_params());
                volumetric_light->execute(cmd, cam.view, cam.proj, cam.position);
            }
            // Froxel fog — nearest homogeneous medium; composite last (over the
            // shafts/clouds) so it fogs everything already lit this frame.
            if (froxel_fog && froxel_fog->is_enabled()) {
                froxel_fog->set_sun(sun_dir_for_rt, sun_color_for_rt);
                froxel_fog->set_shadow_matrix(shadow_view_proj);
                froxel_fog->set_light_count(lighting->get_light_count());
                froxel_fog->execute(cmd, cam.view, cam.proj, cam.position);
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
            if (frame_limit > 0 && frame_count >= frame_limit) {
                spdlog::info("[frames] rendered {} frame(s); exiting as requested", frame_count);
                break;
            }

            // Collect this frame's CPU zones and report a breakdown
            // periodically (the full flame-graph UI is Stage 14).
            GWS_PROFILE_FRAME_END();
#if GWS_PROFILE_ENABLED
            if (frame_count % 240 == 0)
                // debug level: per-frame profiler breakdown stays out of the
                // default console + Output panel (the Performance overlay shows
                // live timings); raise the log level to see it here.
                spdlog::debug("[profiler] {}",
                              gws::profile::Profiler::instance().format_report());
#endif
        }

        spdlog::info("Editor closed after {} frames", frame_count);

        // ----------------------------------------------------------------
        // Cleanup
        // ----------------------------------------------------------------
        // Panels that own OS resources/threads go first: the ConPTY terminal
        // joins its reader thread + kills the shell child.
        spdlog::info("[exit] panels (terminal/console)...");
        editor_state.terminal.reset();
        editor_state.console.reset();

        // Stop the job workers before tearing down (joins all threads).
        spdlog::info("[exit] audio.shutdown...");
        audio.shutdown();   // stop + join the audio thread before teardown
        spdlog::info("[exit] jobs.shutdown...");
        gws::jobs::JobSystem::instance().shutdown();
        spdlog::info("[exit] device.wait_idle...");
        device.wait_idle();
        spdlog::info("[exit] imgui/render teardown...");

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

        // Skinned test rig — its Material returns a set to mat_pool, so free it
        // before the pool/layout below (and before the device).
        anim_demo.reset();
        imported_actor.reset();   // returns its Material's set to mat_pool before free

        // Join background workers BEFORE tearing down anything they might
        // touch. stop() cancels outstanding work and deliberately does not run
        // completion callbacks — those expect a live scene and a live device.
        editor_state.tasks.stop();

        // Scene geometry — caches must release their meshes + materials
        // before the descriptor pool / layout are destroyed.
        mat_cache.clear();
        asset_cache.clear();
        terrain_cache.clear();        // terrain chunk meshes (GPU buffers)
        terrain_gpu_cache.clear();    // splat materials + textures
        prim_cache.cube.reset();
        prim_cache.plane.reset();
        prim_cache.sphere.reset();
        prim_cache.cylinder.reset();
        prim_cache.capsule.reset();
        prim_cache.pyramid.reset();
        vkDestroyDescriptorPool(device.get_device(), mat_pool, nullptr);
        vkDestroyDescriptorSetLayout(device.get_device(), mat_layout, nullptr);

        // EVERY Vulkan-resource object must be destroyed BEFORE the device.
        // These are locals declared after `device`, so without explicit resets
        // their destructors would run AFTER device.shutdown() — the long-
        // standing exit crash (vkDestroyPipeline on a dead device inside
        // VulkanHzbCuller::destroy, and silently-invalid teardown for the
        // other passes).
        spdlog::info("[exit] releasing render passes...");
        hzb_culler.reset();
        occlusion_culler.reset();
        rt_scene.reset();
        water_pass.reset();
        froxel_fog.reset();
        ddgi.reset();
        clouds.reset();
        volumetric_light.reset();
        ssr.reset();
        vxao.reset();
        ssao.reset();
        env_map.reset();
        transparent.reset();
        graph.reset();
        post_processing.reset();
        shadow_map.reset();
        lighting.reset();
        g_buffer.reset();

        spdlog::info("[exit] device.shutdown...");
        device.shutdown();
        spdlog::info("[exit] glfw teardown...");
        glfwDestroyWindow(glfw_window);
        glfwTerminate();
        spdlog::info("[exit] clean — running remaining destructors");

        return 0;
    }
    catch (const std::exception& e) {
        spdlog::error("Exception: {}", e.what());
        std::cerr << "ERROR: " << e.what() << std::endl;
        // Caught here, so it never reached std::terminate — write a report anyway
        // (stack from this point + recent log + minidump) so it's diagnosable.
        gws::diag::write_report(std::string("caught std::exception: ") + e.what());
        return 1;
    }
}
