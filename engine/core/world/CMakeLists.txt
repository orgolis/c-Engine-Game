cmake_minimum_required(VERSION 3.20)

# gws_world — world streaming: grid partition + cell load/unload (hysteresis) +
# floating-origin large-world coords. Headers "world/<x>.h".
add_library(gws_world STATIC
    streaming_manager.cpp)

target_include_directories(gws_world PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gws_world PUBLIC glm)
target_compile_features(gws_world PUBLIC cxx_std_17)
