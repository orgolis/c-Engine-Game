# Editor executable
add_executable(editor)

# Include directories
target_include_directories(editor
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link libraries
target_link_libraries(editor
    PRIVATE
        engine
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
