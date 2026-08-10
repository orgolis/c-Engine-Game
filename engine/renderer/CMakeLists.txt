cmake_minimum_required(VERSION 3.24)

project(renderer LANGUAGES CXX)

# Renderer module - split into graphics abstraction and implementations
add_library(renderer STATIC)

# Include directories
target_include_directories(renderer PUBLIC include)
target_include_directories(renderer PUBLIC gpu)
target_include_directories(renderer PRIVATE src)
# Header-only, runtime-safe asset-pipeline headers (asset_id.h, texture_format.h)
# consumed by the TextureManager for content-addressed ids + cooked `.ctex`.
target_include_directories(renderer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/asset_pipeline)

# Header files - GPU Abstraction Layer
target_sources(renderer PRIVATE
    gpu/render_device.h
)

# Header files - Vulkan Implementation
target_sources(renderer PRIVATE
    gpu/vulkan/vulkan_device.h
    gpu/vulkan/vulkan_swapchain.h
    gpu/vulkan/vulkan_command_buffer.h
    gpu/vulkan/vulkan_descriptor_set.h
    gpu/vulkan/vulkan_surface.h
    gpu/vulkan/shader_compiler.h
    gpu/vulkan/render_pass.h
    gpu/vulkan/graphics_pipeline.h
    gpu/vulkan/barriers.h
    gpu/vulkan/buffer.h
    gpu/vulkan/image.h
    gpu/vulkan/mesh.h
    gpu/vulkan/material.h
    gpu/vulkan/gltf_loader.h
    gpu/vulkan/vulkan_g_buffer.h
    gpu/vulkan/vulkan_lighting_pass.h
    gpu/vulkan/vulkan_shadow_map.h
    gpu/vulkan/vulkan_post_processing.h
    gpu/vulkan/vulkan_transparent_pass.h
    gpu/vulkan/vulkan_occlusion_culler.h
    gpu/vulkan/vulkan_hzb_culler.h
    gpu/vulkan/vulkan_environment_map.h
    gpu/vulkan/vulkan_ssao_pass.h
    gpu/vulkan/vulkan_ssr_pass.h
    gpu/vulkan/vulkan_volumetric_light_pass.h
    gpu/vulkan/vulkan_cloud_pass.h
    gpu/vulkan/vulkan_ddgi_pass.h
    gpu/vulkan/vulkan_rt_functions.h
    gpu/vulkan/vulkan_rt_scene.h
    gpu/vulkan/vulkan_vxao_pass.h
    gpu/vulkan/vulkan_shader_registry.h
    gpu/vulkan/vulkan_render_graph.h
    gpu/vulkan/vulkan_texture.h
    gpu/vulkan/vulkan_sampler_cache.h
    gpu/vulkan/vulkan_texture_manager.h
    gpu/vulkan/vulkan_virtual_texture.h
    gpu/vulkan/vulkan_scene_mesh.h
    gpu/vulkan/vulkan_skinned_mesh.h
    gpu/vulkan/skinned_gltf_loader.h
    gpu/vulkan/vulkan_scene_material.h
    gpu/vulkan/vulkan_gltf_loader.h
    gpu/vulkan/culling.h
    gpu/vulkan/imgui_vulkan.h
    gpu/vulkan/ui_manager.h
    gpu/vulkan/debug_panels.h
    gpu/vulkan/scene_hierarchy_panel.h
    gpu/vulkan/reflection.h
    gpu/vulkan/inspector_panel.h
    gpu/vulkan/profiler.h
    gpu/vulkan/profiler_panels.h
    gpu/vulkan/debug_visualization.h
    gpu/vulkan/hzb_culler.h
    gpu/vulkan/indirect_dispatcher.h
    gpu/vulkan/transform_gizmo.h
    gpu/vulkan/query_pool.h
    gpu/vulkan/gpu_profiler.h
    gpu/vulkan/gpu_profiler_panels.h
)

# Source files - Vulkan Implementation
target_sources(renderer PRIVATE
    gpu/vulkan/vulkan_device.cpp
    gpu/vulkan/vulkan_swapchain.cpp
    gpu/vulkan/vulkan_command_buffer.cpp
    gpu/vulkan/vulkan_descriptor_set.cpp
    gpu/vulkan/vulkan_surface.cpp
    gpu/vulkan/shader_compiler.cpp
    gpu/vulkan/render_pass.cpp
    gpu/vulkan/graphics_pipeline.cpp
    gpu/vulkan/barriers.cpp
    gpu/vulkan/buffer.cpp
    gpu/vulkan/image.cpp
    gpu/vulkan/mesh.cpp
    gpu/vulkan/material.cpp
    gpu/vulkan/gltf_loader.cpp
    gpu/vulkan/vulkan_g_buffer.cpp
    gpu/vulkan/vulkan_lighting_pass.cpp
    gpu/vulkan/vulkan_shadow_map.cpp
    gpu/vulkan/vulkan_post_processing.cpp
    gpu/vulkan/vulkan_transparent_pass.cpp
    gpu/vulkan/vulkan_occlusion_culler.cpp
    gpu/vulkan/vulkan_hzb_culler.cpp
    gpu/vulkan/vulkan_environment_map.cpp
    gpu/vulkan/vulkan_ssao_pass.cpp
    gpu/vulkan/vulkan_ssr_pass.cpp
    gpu/vulkan/vulkan_volumetric_light_pass.cpp
    gpu/vulkan/vulkan_water_pass.cpp
    gpu/vulkan/vulkan_froxel_fog_pass.cpp
    gpu/vulkan/vulkan_cloud_pass.cpp
    gpu/vulkan/vulkan_ddgi_pass.cpp
    gpu/vulkan/vulkan_rt_functions.cpp
    gpu/vulkan/vulkan_rt_scene.cpp
    gpu/vulkan/vulkan_vxao_pass.cpp
    gpu/vulkan/vulkan_shader_registry.cpp
    gpu/vulkan/vulkan_render_graph.cpp
    gpu/vulkan/vulkan_texture.cpp
    gpu/vulkan/vulkan_sampler_cache.cpp
    gpu/vulkan/vulkan_texture_manager.cpp
    gpu/vulkan/vulkan_virtual_texture.cpp
    gpu/vulkan/vulkan_scene_mesh.cpp
    gpu/vulkan/vulkan_skinned_mesh.cpp
    gpu/vulkan/skinned_gltf_loader.cpp
    gpu/vulkan/vulkan_scene_material.cpp
    gpu/vulkan/vulkan_gltf_loader.cpp
    gpu/vulkan/culling.cpp
    gpu/vulkan/imgui_vulkan.cpp
    # Phase 6 editor/debug components — excluded pending GCC compat fixes:
    # gpu/vulkan/ui_manager.cpp
    # gpu/vulkan/debug_panels.cpp
    # gpu/vulkan/scene_hierarchy_panel.cpp
    # gpu/vulkan/inspector_panel.cpp
    # gpu/vulkan/profiler.cpp
    # gpu/vulkan/profiler_panels.cpp
    # gpu/vulkan/debug_visualization.cpp
    # gpu/vulkan/hzb_culler.cpp
    # gpu/vulkan/indirect_dispatcher.cpp
    # gpu/vulkan/transform_gizmo.cpp
    # gpu/vulkan/query_pool.cpp
    # gpu/vulkan/gpu_profiler.cpp  (re-enabled below — self-contained, GCC-clean)
    # gpu/vulkan/gpu_profiler_panels.cpp
)

# Stage 14 (Pillar N2): GPU per-pass timing class. Pure C++ (std::map/mutex/
# vector — no Vulkan headers), so it builds under GCC unlike the rest of the
# excluded Phase-6 panels. Re-enabled so the unified Performance overlay can
# read engine::vulkan::GPUProfiler::instance().
target_sources(renderer PRIVATE gpu/vulkan/gpu_profiler.cpp)

# Header files - Core Graphics Abstraction
target_sources(renderer PRIVATE
    include/graphics.h
    include/render_device.h
    include/renderer.h
    include/opengl_device.h
    include/mesh.h
    include/shader.h
)

# Header files - Textures, Materials, Framebuffers
target_sources(renderer PRIVATE
    include/texture.h
    include/material.h
    include/pbr.h
    include/framebuffer.h
    include/graphics_utils.h
)

# Header files - Geometry System
target_sources(renderer PRIVATE
    include/geometry.h
)

# Header files - Lighting System
target_sources(renderer PRIVATE
    include/light.h
    include/shadow.h
    include/ibl.h
    include/lighting.h
)

# Header files - Deferred Rendering System
target_sources(renderer PRIVATE
    include/g_buffer.h
    include/deferred_renderer.h
)

# Header files - Post-Processing System
target_sources(renderer PRIVATE
    include/post_processing.h
)

# Header files - Frustum Culling
target_sources(renderer PRIVATE
    include/frustum.h
)

# Header files - Batching System
target_sources(renderer PRIVATE
    include/batch.h
)

# Header files - Performance Profiling
target_sources(renderer PRIVATE
    include/profiler.h
)

# Header files - Physics Integration (Scene-level components) - DISABLED since physics module is disabled
# target_sources(renderer PRIVATE
#     include/physics_component.h
#     include/physics_system.h
# )

# Header files - Animation System
target_sources(renderer PRIVATE
    include/skeleton.h
    include/animation.h
    include/animator.h
    include/blend_tree.h
    include/anim_pose.h
    include/ik.h
    include/animation_state_machine.h
    include/animation_component.h
    include/skeletal_mesh_component.h
)

# Source files - Core
target_sources(renderer PRIVATE
    src/renderer.cpp
    src/mesh.cpp
)
# shader.cpp uses glad directly; only compile it for the OpenGL backend.
if(GWS_GRAPHICS_API STREQUAL "opengl")
    target_sources(renderer PRIVATE src/shader.cpp)
endif()

# Source files - Textures, Materials, Framebuffers
target_sources(renderer PRIVATE
    src/pbr.cpp
)
# Legacy material.cpp depends on the OpenGL ShaderProgram API and has
# pre-existing build bugs. Vulkan backend uses gpu/vulkan/material.* instead.
if(GWS_GRAPHICS_API STREQUAL "opengl")
    target_sources(renderer PRIVATE src/material.cpp)
endif()
# texture.cpp / framebuffer.cpp / graphics_utils.cpp belong to the legacy
# OpenGL renderer (they pull in stb_image and OpenGL types). Compile only
# when targeting that backend; the Vulkan path uses gpu/vulkan/image.* etc.
if(GWS_GRAPHICS_API STREQUAL "opengl")
    target_sources(renderer PRIVATE
        src/texture.cpp
        src/framebuffer.cpp
        src/graphics_utils.cpp
    )
endif()

# Source files - Geometry System
target_sources(renderer PRIVATE
    src/geometry.cpp
)

# Source files - Lighting System
# Legacy lighting renderer references the OpenGL Framebuffer API and has
# pre-existing build issues. Compile only on the OpenGL backend; the Vulkan
# backend uses gpu/vulkan/vulkan_lighting_pass instead.
if(GWS_GRAPHICS_API STREQUAL "opengl")
    target_sources(renderer PRIVATE src/lighting.cpp)
endif()

# Source files - Deferred Rendering System
# Legacy OpenGL deferred renderer; the Vulkan backend has its own under
# gpu/vulkan/. Compile only when targeting the legacy OpenGL path.
if(GWS_GRAPHICS_API STREQUAL "opengl")
    target_sources(renderer PRIVATE src/deferred_renderer.cpp)
endif()

# Source files - Post-Processing System
# Legacy post-processing implementation has pre-existing legacy-API issues.
# Vulkan backend uses gpu/vulkan/vulkan_post_processing instead.
if(GWS_GRAPHICS_API STREQUAL "opengl")
    target_sources(renderer PRIVATE src/post_processing.cpp)
endif()

# Source files - Frustum Culling
target_sources(renderer PRIVATE
    src/frustum.cpp
)

# Source files - Batching System
# Legacy CPU-side batching uses the old Material API and has pre-existing
# include/argument-count issues. Vulkan backend will gain its own batcher
# in Phase 5; for now compile only on the legacy OpenGL path.
if(GWS_GRAPHICS_API STREQUAL "opengl")
    target_sources(renderer PRIVATE src/batch.cpp)
endif()

# Source files - Performance Profiling
target_sources(renderer PRIVATE
    src/profiler.cpp
)

# Source files - Physics Integration (Scene-level components) - DISABLED since physics module is disabled
# target_sources(renderer PRIVATE
#     src/physics_component.cpp
#     src/physics_system.cpp
# )

# Source files - Animation System
target_sources(renderer PRIVATE
    src/skeleton.cpp
    src/animation.cpp
    src/animator.cpp
    src/blend_tree.cpp
    src/ik.cpp
    src/animation_state_machine.cpp
    src/animation_component.cpp
    src/skeletal_mesh_component.cpp
)

# Source files - OpenGL implementation (only when targeting OpenGL).
if(GWS_GRAPHICS_API STREQUAL "opengl")
    target_sources(renderer PRIVATE src/opengl/opengl_device.cpp)
endif()

# ============================================================================
# Graphics API Selection (Vulkan or OpenGL)
# ============================================================================

set(GWS_GRAPHICS_API "vulkan" CACHE STRING "Target graphics API: vulkan or opengl")
set_property(CACHE GWS_GRAPHICS_API PROPERTY STRINGS vulkan opengl)

# GLM_ENABLE_EXPERIMENTAL: legacy renderer code (lighting.cpp et al.) pulls in
# glm/gtx/quaternion.hpp which transitively includes experimental extensions.
target_compile_definitions(renderer PUBLIC GLM_ENABLE_EXPERIMENTAL)

message(STATUS "GameWorldshaper Graphics API: ${GWS_GRAPHICS_API}")

if(GWS_GRAPHICS_API STREQUAL "vulkan")
    message(STATUS "Configuring Vulkan renderer backend")
    
    # Find Vulkan SDK
    find_package(Vulkan REQUIRED)
    
    # Link dependencies
    target_link_libraries(renderer PUBLIC
        glm
        window
        gws_logging
        physics
        scene
        Vulkan::Vulkan
        vma
        glslang_wrapper
    )
    target_link_libraries(renderer PRIVATE
        gws_jobs        # parallel frustum culling above a threshold
        tinygltf
        stb_image
        meshoptimizer
        imgui
    )
    
    # Vulkan-specific compile definitions
    target_compile_definitions(renderer PRIVATE GWS_USE_VULKAN=1)
    
    # Optional: Enable volk for dynamic loading (if available)
    # find_package(volk QUIET)
    # if(volk_FOUND)
    #     target_link_libraries(renderer PRIVATE volk::volk)
    #     target_compile_definitions(renderer PRIVATE GWS_USE_VOLK=1)
    # endif()

elseif(GWS_GRAPHICS_API STREQUAL "opengl")
    message(STATUS "Configuring OpenGL renderer backend (legacy)")
    
    # Link dependencies
    target_link_libraries(renderer PUBLIC
        glm
        window
        gws_logging
        glad
        physics
        scene
    )
    
    # OpenGL support
    find_package(OpenGL REQUIRED)
    target_link_libraries(renderer PRIVATE OpenGL::GL)
    
    # GLEW for OpenGL function loading (optional)
    if(WIN32)
        message(STATUS "Using OpenGL via CMake find_package")
    else()
        find_package(GLEW QUIET)
        if(GLEW_FOUND)
            target_link_libraries(renderer PRIVATE GLEW::GLEW)
        endif()
    endif()
    
    target_compile_definitions(renderer PRIVATE RENDERER_OPENGL=1)

else()
    message(FATAL_ERROR "Invalid GWS_GRAPHICS_API: ${GWS_GRAPHICS_API}. Use 'vulkan' or 'opengl'")
endif()

# Compiler settings
set_target_properties(renderer PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

if(MSVC)
    target_compile_options(renderer PRIVATE /W4)
else()
    target_compile_options(renderer PRIVATE -Wall -Wextra -pedantic)
endif()

# Unit tests (add as we implement features)
# add_executable(renderer_tests 
#     src/tests/test_shader_cache.cpp
#     src/tests/test_render_context.cpp
# )
# target_link_libraries(renderer_tests PRIVATE renderer Catch2::Catch2WithMain)
