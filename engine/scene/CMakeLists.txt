cmake_minimum_required(VERSION 3.24)

project(scene LANGUAGES CXX)

# Scene management module
add_library(scene STATIC)

# Include directories
target_include_directories(scene PUBLIC include)
target_include_directories(scene PRIVATE src)

# Header files - Scene Management
target_sources(scene PRIVATE
    include/transform.h
    include/transform_component.h
    include/mesh_renderer_component.h
    include/mesh_component.h
    include/entity.h
    include/scene.h
    include/octree.h
    include/mesh_renderer.h
    include/physics_demo_scene.h
    include/animation_demo_scene.h
    include/entity_factory.h
    include/light_component.h
    include/collider_component.h
    include/camera_component.h
)

# Source files - Scene Management
target_sources(scene PRIVATE
    src/transform.cpp
    src/transform_component.cpp
    src/mesh_renderer_component.cpp
    src/mesh_component.cpp
    src/scene.cpp
    src/entity_factory.cpp
    src/light_component.cpp
    src/camera_component.cpp
    # src/physics_demo_scene.cpp  # TODO: Resolve circular dependency with renderer
    # src/animation_demo_scene.cpp  # TODO: Resolve circular dependency with renderer
    # src/octree.cpp  # Disabled - experimental spatial partitioning
    # src/mesh_renderer.cpp  # Disabled - requires renderer module which is disabled
)

# Link dependencies
target_link_libraries(scene PUBLIC
    glm
    gws_logging
    gws_assets
    physics
)

# Enable GLM experimental extensions
target_compile_definitions(scene PRIVATE GLM_ENABLE_EXPERIMENTAL)

# Compiler settings
set_target_properties(scene PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

if(MSVC)
    target_compile_options(scene PRIVATE /W4)
else()
    target_compile_options(scene PRIVATE -Wall -Wextra -pedantic)
endif()

# Ensure scene is included in engine builds
set_property(GLOBAL APPEND PROPERTY ENGINE_MODULES scene)

# ---------------------------------------------------------------------------
# Header self-containment (see cmake/HeaderSelfContain.cmake).
#
# A header that uses uint32_t without <cstdint> compiles fine in a full build
# and fails in CI, because include order is not a guarantee. That is exactly
# what broke the v0.6.0 release. Compiling each header ALONE is the only way to
# catch it, and a full build never will.
# ---------------------------------------------------------------------------
include("${CMAKE_SOURCE_DIR}/cmake/HeaderSelfContain.cmake")
file(GLOB SCENE_PUBLIC_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
gws_add_header_selfcontain_check(scene_headers_selfcontain
    TARGET  scene
    HEADERS ${SCENE_PUBLIC_HEADERS})
