# Editor executable
add_executable(editor)

# Include directories
target_include_directories(editor
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../engine/window/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../engine/renderer/include
)

# Link libraries
target_link_libraries(editor
    PRIVATE
        engine
        window
        renderer
)

# Add source files
target_sources(editor
    PRIVATE
        src/main.cpp
)

# Compiler flags
if(MSVC)
    target_compile_options(editor PRIVATE /W4)
else()
    target_compile_options(editor PRIVATE -Wall -Wextra -Wpedantic)
endif()
