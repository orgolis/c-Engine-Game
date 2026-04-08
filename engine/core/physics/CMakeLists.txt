cmake_minimum_required(VERSION 3.20)

# Physics module - Advanced physics systems + Rigid body dynamics
add_library(physics
    # Rigid body physics implementation
    collision.cpp
    rigidbody.cpp
    physics_world.cpp
    constraints.cpp
)

target_include_directories(physics
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# Link to math library and memory (no scene dependency to avoid circular refs)
target_link_libraries(physics
    PUBLIC
    math
    memory
    glm
)

# Mark as C++20
target_compile_features(physics PUBLIC cxx_std_20)

# Advanced physics systems (header-only)
# - Vector fields, PDEs, particle systems, numerical methods
# All unified under gws::physics namespace via physics.h
