cmake_minimum_required(VERSION 3.24)

project(renderer LANGUAGES CXX)

# Renderer module - split into graphics abstraction and implementations
add_library(renderer STATIC)

# Include directories
target_include_directories(renderer PUBLIC include)
target_include_directories(renderer PRIVATE src)

# Header files (for IDE organization)
target_sources(renderer PRIVATE
    include/render_device.h
    include/renderer.h
)

# Source files - Core
target_sources(renderer PRIVATE
    src/renderer.cpp
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
