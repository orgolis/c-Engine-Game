cmake_minimum_required(VERSION 3.20)

# gws_vfx — CPU particle system with camera-facing billboards for ability/impact
# VFX. World-space particles integrate under gravity+drag, fade over life, and
# build camera-facing quads for a GPU backend. Rebases with the floating origin.
# Headers "vfx/<x>.h".
add_library(gws_vfx STATIC
    particle_system.cpp)

target_include_directories(gws_vfx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gws_vfx PUBLIC glm)
target_compile_features(gws_vfx PUBLIC cxx_std_17)
