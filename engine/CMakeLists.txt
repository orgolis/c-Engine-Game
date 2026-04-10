# Phase 1: Core systems (math, memory, logging, etc.)
add_subdirectory(core)

# Phase 2: Graphics & Rendering
add_subdirectory(window)
# add_subdirectory(renderer)  # Disabled - renderer module has compilation errors, use simplified light component instead

# Phase 4: Scene Management
add_subdirectory(scene)
add_library(engine STATIC)

# Set public include directory
target_include_directories(engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Add source files
target_sources(engine
    PRIVATE
        src/engine.cpp
)

# Link core libraries
target_link_libraries(engine
    PUBLIC
        math
        gws_logging
        gws_file_io
        gws_containers
        input
        gameplay
        window
        # renderer  # Disabled - renderer module has compilation errors
)

# Compiler flags
if(MSVC)
    target_compile_options(engine PRIVATE /W4)
else()
    target_compile_options(engine PRIVATE -Wall -Wextra -Wpedantic)
endif()
