cmake_minimum_required(VERSION 3.24)

project(renderer LANGUAGES CXX)

# Renderer module - split into graphics abstraction and implementations
add_library(renderer STATIC)

# Include directories
target_include_directories(renderer PUBLIC include)
target_include_directories(renderer PRIVATE src)

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
    include/animation_component.h
    include/skeletal_mesh_component.h
)

# Source files - Core
target_sources(renderer PRIVATE
    src/renderer.cpp
    src/mesh.cpp
    src/shader.cpp
)

# Source files - Textures, Materials, Framebuffers
target_sources(renderer PRIVATE
    src/texture.cpp
    src/material.cpp
    src/pbr.cpp
    src/framebuffer.cpp
    src/graphics_utils.cpp
)

# Source files - Geometry System
target_sources(renderer PRIVATE
    src/geometry.cpp
)

# Source files - Lighting System
target_sources(renderer PRIVATE
    src/lighting.cpp
)

# Source files - Deferred Rendering System
target_sources(renderer PRIVATE
    src/deferred_renderer.cpp
)

# Source files - Post-Processing System
target_sources(renderer PRIVATE
    src/post_processing.cpp
)

# Source files - Frustum Culling
target_sources(renderer PRIVATE
    src/frustum.cpp
)

# Source files - Batching System
target_sources(renderer PRIVATE
    src/batch.cpp
)

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
    src/animation_component.cpp
    src/skeletal_mesh_component.cpp
)

# Source files - OpenGL implementation
target_sources(renderer PRIVATE
    src/opengl/opengl_device.cpp
)

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

# GLEW for OpenGL function loading
# Try to find GLEW, if not found use glfw's built-in support
if(WIN32)
    # Windows: GLEW might not be available, use system OpenGL + glad or glfw
    # For now, we'll rely on glfw's OpenGL support (requires glfwMakeContextCurrent first)
    message(STATUS "Using OpenGL via CMake find_package")
else()
    # Linux: Look for GLEW
    find_package(GLEW QUIET)
    if(GLEW_FOUND)
        target_link_libraries(renderer PRIVATE GLEW::GLEW)
    else()
        message(WARNING "GLEW not found, will use glfw OpenGL support only")
    endif()
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

# Define which graphics API is being used
target_compile_definitions(renderer PRIVATE RENDERER_OPENGL=1)

# Unit tests (add as we implement features)
# add_executable(renderer_tests 
#     src/tests/test_shader_cache.cpp
#     src/tests/test_render_context.cpp
# )
# target_link_libraries(renderer_tests PRIVATE renderer Catch2::Catch2WithMain)
