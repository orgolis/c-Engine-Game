cmake_minimum_required(VERSION 3.20)

# gws_ui — runtime game-UI framework (widgets + anchored layout + draw list +
# input/focus). Separate from the editor's dev-only ImGui. Headers "ui/<x>.h".
add_library(gws_ui STATIC
    canvas.cpp)

target_include_directories(gws_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gws_ui PUBLIC glm)
target_compile_features(gws_ui PUBLIC cxx_std_17)
