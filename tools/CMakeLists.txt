# Offline content tools (Master Plan Stage 2.2)

# assetcook: import -> cook -> bundle, with incremental cook DB.
# gltf_import / fbx_import / usd_import are the single TUs compiling
# tinygltf / ufbx / tinyusdz respectively.
add_executable(assetcook
    assetcook/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/core/asset_pipeline/gltf_import.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/core/asset_pipeline/fbx_import.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/core/asset_pipeline/usd_import.cpp)
target_link_libraries(assetcook PRIVATE gws_asset_pipeline bc7enc stb_image tinygltf ufbx tinyusdz_static)
# tinyusdz exports its headers PRIVATE, so add its src dir for usd_import.cpp.
target_include_directories(assetcook PRIVATE ${CMAKE_SOURCE_DIR}/third_party/tinyusdz/src)
target_compile_features(assetcook PRIVATE cxx_std_20)

# Self-contained on MinGW/GCC so the CLI runs without the toolchain's runtime
# DLLs alongside it (the editor links static too).
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(assetcook PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# physics_check: Stage 4 Jolt physics verification (drop / determinism / scale /
# character / ECS bridge).
add_executable(physics_check physics_check/main.cpp)
target_link_libraries(physics_check PRIVATE gws_physics gws_ecs)
target_compile_features(physics_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(physics_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()
