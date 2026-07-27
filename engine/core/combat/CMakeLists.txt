cmake_minimum_required(VERSION 3.20)

# gws_combat — action-combat frame-data substrate: hitbox activation windows
# (startup/active/recovery), per-attack hit dedup, dodge i-frames, parry, poise
# stagger. Camera-agnostic, world-space, deterministic. Headers "combat/<x>.h".
add_library(gws_combat STATIC
    combat.cpp)

target_include_directories(gws_combat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gws_combat PUBLIC glm)
target_compile_features(gws_combat PUBLIC cxx_std_17)
