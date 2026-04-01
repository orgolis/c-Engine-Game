cmake_minimum_required(VERSION 3.20)

# Advanced physics module - optional, depends on math
add_library(physics INTERFACE)

target_include_directories(physics
    INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Link to math library
target_link_libraries(physics
    INTERFACE
    math
)

# Mark as C++20 interface
target_compile_features(physics INTERFACE cxx_std_20)

# Note: Physics module is mostly header-only
# Complex solvers can be compiled later if needed for performance
