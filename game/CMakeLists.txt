# Game executable
add_executable(game)

# Include directories
target_include_directories(game
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../engine/window/include
        # ${CMAKE_CURRENT_SOURCE_DIR}/../engine/renderer/include  # Disabled since renderer module is disabled
)

# Link libraries
target_link_libraries(game
    PRIVATE
        engine
        window
        # renderer  # Disabled since renderer module is disabled
)

# Add source files
target_sources(game
    PRIVATE
        src/main.cpp
)

# Compiler flags
if(MSVC)
    target_compile_options(game PRIVATE /W4)
else()
    target_compile_options(game PRIVATE -Wall -Wextra -Wpedantic)
endif()
