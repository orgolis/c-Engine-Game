add_library(gws_asset_pipeline INTERFACE)

target_include_directories(gws_asset_pipeline
    INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Cooked assets are serialized with the reflection-driven archive; the mesh
# cook uses meshoptimizer for cache/fetch optimisation + LOD generation;
# gws_compression provides per-asset LZ4/Zstd for the pak read/write path.
target_link_libraries(gws_asset_pipeline INTERFACE gws_serialization meshoptimizer gws_compression)

target_compile_features(gws_asset_pipeline INTERFACE cxx_std_20)
