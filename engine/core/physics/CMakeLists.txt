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

# --- Jolt-backed rigid-body physics (Master Plan Stage 4) ---
# `schizo::physics::PhysicsWorld` wraps Jolt behind a Jolt-free engine API
# (jolt_physics.h). Kept separate from the legacy `physics` lib (which stays for
# particles / PDE / VFX solvers). Linking `Jolt` PRIVATE propagates the Jolt
# include dirs + the ABI-defining compile definitions to this lib's sources.
add_library(gws_physics STATIC jolt_physics.cpp)
target_include_directories(gws_physics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gws_physics PUBLIC glm PRIVATE Jolt)
target_compile_features(gws_physics PUBLIC cxx_std_20)
