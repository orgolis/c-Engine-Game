cmake_minimum_required(VERSION 3.20)

# gws_vscript -- the visual scripting core ("Logic Graph"): typed values, the
# node registry, the graph asset, a compiler to bytecode and the VM that runs
# it. No engine dependencies beyond glm, so it is testable headless
# (tools/vscript_check). Headers "vscript/<x>.h".
add_library(gws_vscript STATIC
    value.cpp
    registry.cpp
    compiler.cpp
    vm.cpp
    stdlib.cpp
    io.cpp)

target_include_directories(gws_vscript PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gws_vscript PUBLIC glm)
target_compile_features(gws_vscript PUBLIC cxx_std_20)
