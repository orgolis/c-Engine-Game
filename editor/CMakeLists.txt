# Editor executable
add_executable(editor)

# Include directories
target_include_directories(editor
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../engine/window/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../engine/scene/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../engine/renderer/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../engine/renderer/gpu
        ${CMAKE_CURRENT_SOURCE_DIR}/../engine/include
        ${CMAKE_SOURCE_DIR}/third_party/imgui
        ${CMAKE_SOURCE_DIR}/third_party/imgui/backends
        ${CMAKE_SOURCE_DIR}/third_party/glfw/include
        ${CMAKE_SOURCE_DIR}/third_party/pocketpy   # Python script backend (Stage 12)
)

# Link libraries
target_link_libraries(editor
    PRIVATE
        engine
        scene
        window
        character
        ability
        network
        renderer
        gws_jobs           # work-stealing job system (Stage 0.3)
        gws_profiler       # core CPU profiler - scoped zones (Stage 0.6)
        gws_ecs            # ECS World + components (Stage 1)
        gws_serialization  # reflection-driven archive (net snapshots, Stage 7)
        gws_asset_pipeline # cooked-asset bundles (Stage 2)
        gws_physics        # Jolt-backed rigid bodies + character (Stage 4)
        gws_audio          # miniaudio device + spatial voice mixer (Stage 6)
        gws_ai             # navmesh + behavior trees (Stage 9)
        gws_ui             # runtime game-UI framework (HUD/menus) — Game-UI pillar
        gws_diagnostics    # crash handler + persistent/ring-buffer logging
)

# Engine version string, for the crash-report header / diagnostics.
target_compile_definitions(editor PRIVATE GWS_ENGINE_VERSION="${PROJECT_VERSION}")

# Add source files
target_sources(editor
    PRIVATE
        src/main.cpp
        src/project.cpp
        src/project_launcher.cpp
        src/project_paths.cpp
        src/ecs_bridge.cpp
        src/component_inspector.cpp
        src/play_mode_changes.cpp
        src/logic_graph_panel.cpp
        src/editor_scene.cpp
        src/scene_serializer.cpp
        src/mesh_generator.cpp
        src/transform_gizmo.cpp
        src/undo_redo_manager.cpp
        src/asset_browser_panel.cpp
        src/material_editor_panel.cpp
        src/asset_import_dialog.cpp
        src/character_controller_panel.cpp
        src/ability_system_panel.cpp
        src/network_system_panel.cpp
        src/net_session.cpp
        src/script_system.cpp
        src/script_host_python.cpp
        src/script_host_cpp.cpp
        src/script_host_cs.cpp
        src/script_api_editor.cpp
        src/script_params.cpp
        src/scene_playback_manager.cpp
        src/terminal_panel.cpp
        src/console_panel.cpp
        # ImGui core
        ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui.cpp
        ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_demo.cpp
        ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_draw.cpp
        ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_tables.cpp
        ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_widgets.cpp
        # ImGui backends
        ${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_glfw.cpp
        ${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_vulkan.cpp
)

# Compiler flags
if(MSVC)
    target_compile_options(editor PRIVATE /W4)
else()
    target_compile_options(editor PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Native Win32 file dialogs (GetOpenFileName / GetSaveFileName) live in
# comdlg32 — MinGW-w64 doesn't auto-link it, MSVC does. Pull it in
# explicitly on Windows so both toolchains build the editor.
if(WIN32)
    # comdlg32: GetOpenFileName/GetSaveFileName (scene dialogs).
    # shell32 + ole32: SHBrowseForFolder + CoTaskMemFree (project-folder picker).
    target_link_libraries(editor PRIVATE comdlg32 shell32 ole32)
endif()

# --- Install / packaging: the editor is packaged (component "engine") as the
#     flat engine release the Hub installs. Everything ships under COMPONENT
#     engine so CPack excludes third-party libs' own install() output. ---
gws_bundle_runtime(editor "${GWS_ENGINE_DEST}")   # runtime DLLs (COMPONENT engine)
install(TARGETS editor RUNTIME DESTINATION "${GWS_ENGINE_DEST}" COMPONENT engine)
# Minimal engine default content. The sky is procedural (no HDR needed) and the
# heavy dev models in assets/models are NOT shipped — projects carry their own.
install(DIRECTORY
            ${CMAKE_SOURCE_DIR}/assets/skies
            ${CMAKE_SOURCE_DIR}/assets/scripts
        DESTINATION "${GWS_ENGINE_DEST}/assets" COMPONENT engine OPTIONAL)
