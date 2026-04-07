cmake_minimum_required(VERSION 3.24)

project(glad LANGUAGES CXX)

# GLAD loader library
add_library(glad STATIC)

# Include directories
target_include_directories(glad PUBLIC include)

# Source files
target_sources(glad PRIVATE
    src/glad.cpp
)

# Link OpenGL
find_package(OpenGL REQUIRED)
target_link_libraries(glad PUBLIC OpenGL::GL)

# C++ standard
set_target_properties(glad PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
