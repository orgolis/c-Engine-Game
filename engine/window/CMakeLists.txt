cmake_minimum_required(VERSION 3.24)

project(window LANGUAGES CXX)

# Window module
add_library(window STATIC)

# Include directories
target_include_directories(window PUBLIC include)

# Source files
target_sources(window PRIVATE
    src/window.cpp
)

# Link dependencies
target_link_libraries(window PUBLIC
    glfw
    glm
    gws_logging
)

# Compiler settings
set_target_properties(window PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

if(MSVC)
    target_compile_options(window PRIVATE /W4)
else()
    target_compile_options(window PRIVATE -Wall -Wextra -pedantic)
endif()

# Unit tests (optional, create when needed)
# add_executable(window_tests src/tests/test_window.cpp)
# target_link_libraries(window_tests PRIVATE window Catch2::Catch2WithMain)
