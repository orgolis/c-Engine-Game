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

# net_check: Stage 7 transport verification (ENet loopback connect + ping/pong).
add_executable(net_check net_check/main.cpp)
target_link_libraries(net_check PRIVATE network)
target_compile_features(net_check PRIVATE cxx_std_17)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(net_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# repl_check: Stage 7 replication verification (server ECS world -> client world).
add_executable(repl_check repl_check/main.cpp)
target_link_libraries(repl_check PRIVATE network gws_ecs gws_serialization)
target_compile_features(repl_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(repl_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# mp_check: Stage 7 editor NetSession integration (host<->client scene transform
# replication through the editor's NetSession, over real OOP scenes).
add_executable(mp_check mp_check/main.cpp ${CMAKE_SOURCE_DIR}/editor/src/net_session.cpp)
target_include_directories(mp_check PRIVATE ${CMAKE_SOURCE_DIR}/editor/include)
target_link_libraries(mp_check PRIVATE scene network gws_ecs gws_serialization)
target_compile_features(mp_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(mp_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# script_check: Stage 12 scripting verification (ScriptSystem + Python backend
# driven headlessly over a real OOP scene: lifecycle, spawn, hot reload, errors).
add_executable(script_check script_check/main.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/script_system.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/script_host_python.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/script_host_cpp.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/script_host_cs.cpp)
target_include_directories(script_check PRIVATE
    ${CMAKE_SOURCE_DIR}/editor/include
    ${CMAKE_SOURCE_DIR}/third_party/pocketpy)
target_link_libraries(script_check PRIVATE scene)
target_compile_features(script_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(script_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# prediction_check: Stage 7 client-prediction + server-reconciliation under
# simulated latency (header-only prediction.h; links network for the include + glm).
add_executable(prediction_check prediction_check/main.cpp)
target_link_libraries(prediction_check PRIVATE network)
target_compile_features(prediction_check PRIVATE cxx_std_17)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(prediction_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# audio_check: Stage 6 verification (headless, no device) — mixer gain/sum/
# pitch/loop/stop/voice-pool + lock-free queue + spatialisation + the ECS bridge
# (links gws_ecs for the AudioScene test; C++20 for EnTT).
add_executable(audio_check audio_check/main.cpp)
target_link_libraries(audio_check PRIVATE gws_audio gws_ecs)
target_compile_features(audio_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(audio_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# profiler_check: Stage 14 verification — N4 network-profiler (Transport
# NetStats, NetProfiler bandwidth, prediction rollback depth) + N3 memory
# snapshot (per-tag tracker + opt-in AllocatorRegistry). Memory headers are
# header-only, so just add their include dir.
add_executable(profiler_check profiler_check/main.cpp)
target_link_libraries(profiler_check PRIVATE network)
target_include_directories(profiler_check PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/core/memory
    ${CMAKE_SOURCE_DIR}/engine/core/profiler)   # N5 frame_capture.h (header-only)
target_compile_features(profiler_check PRIVATE cxx_std_17)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(profiler_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()
