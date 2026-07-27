cmake_minimum_required(VERSION 3.20)

# gws_ai — Stage 9 AI: triangle navmesh (A* + funnel) + behavior trees.
# Headers included as "ai/<x>.h" (include root engine/core, like the other
# gws_* core libs). behavior_tree.h is header-only; navmesh has a .cpp.
add_library(gws_ai STATIC
    navmesh.cpp
    nav_builder.cpp)

target_include_directories(gws_ai PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gws_ai PUBLIC glm)
target_compile_features(gws_ai PUBLIC cxx_std_17)
