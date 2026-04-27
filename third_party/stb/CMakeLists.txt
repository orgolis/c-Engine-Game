# stb_image - single-header image loader (PNG, JPG, BMP, etc.)
# Header-only; the .cpp implementing the symbols is in the renderer.

add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(stb_image INTERFACE cxx_std_17)
