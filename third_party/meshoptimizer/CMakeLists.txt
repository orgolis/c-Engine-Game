# meshoptimizer - mesh optimization library (LOD simplification, vertex cache,
# overdraw reduction, etc.). Vendored from upstream v1.1.

add_library(meshoptimizer STATIC
    src/allocator.cpp
    src/clusterizer.cpp
    src/indexanalyzer.cpp
    src/indexcodec.cpp
    src/indexgenerator.cpp
    src/meshletcodec.cpp
    src/meshletutils.cpp
    src/opacitymap.cpp
    src/overdrawoptimizer.cpp
    src/partition.cpp
    src/quantization.cpp
    src/rasterizer.cpp
    src/simplifier.cpp
    src/spatialorder.cpp
    src/stripifier.cpp
    src/vcacheoptimizer.cpp
    src/vertexcodec.cpp
    src/vertexfilter.cpp
    src/vfetchoptimizer.cpp
)

target_include_directories(meshoptimizer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(meshoptimizer PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(meshoptimizer PRIVATE /wd4100 /wd4127 /wd4244 /wd4267)
endif()
