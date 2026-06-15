add_library(gws_compression STATIC compression.cpp)

target_include_directories(gws_compression PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# LZ4 (fast) + Zstd (ratio) behind one front-end.
target_link_libraries(gws_compression PUBLIC lz4 zstd)

target_compile_features(gws_compression PUBLIC cxx_std_20)
