# Editor executable
add_executable(editor)

# Include directories
target_include_directories(editor
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../engine/window/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../engine/scene/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../engine/renderer/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../engine/include
        ${CMAKE_SOURCE_DIR}/third_party/imgui
        ${CMAKE_SOURCE_DIR}/third_party/imgui/backends
        ${CMAKE_SOURCE_DIR}/third_party/glfw/include
        ${CMAKE_SOURCE_DIR}/third_party/glad/include
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
        glad
        opengl32
)

# Add source files
target_sources(editor
    PRIVATE
        src/main.cpp
        src/editor_scene.cpp
        src/scene_serializer.cpp
        src/mesh_generator.cpp
        src/simple_renderer.cpp
        src/viewport_renderer_3d_minimal.cpp
        src/transform_gizmo.cpp
        src/undo_redo_manager.cpp
        src/asset_browser_panel.cpp
        src/material_editor_panel.cpp
        src/asset_import_dialog.cpp
        src/character_controller_panel.cpp
        src/ability_system_panel.cpp
        src/network_system_panel.cpp
        src/scene_playback_manager.cpp
        # ImGui core
        ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui.cpp
        ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_demo.cpp
        ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_draw.cpp
        ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_tables.cpp
        ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_widgets.cpp
        # ImGui backends
        ${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_glfw.cpp
        ${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_opengl3.cpp
)

# Compiler flags
if(MSVC)
    target_compile_options(editor PRIVATE /W4)
else()
    target_compile_options(editor PRIVATE -Wall -Wextra -Wpedantic)
endif()
