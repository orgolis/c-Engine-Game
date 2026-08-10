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

# assetpath_check: a non-ASCII asset filename must still load. Caught a real
# failure where a 64 MB model could not be opened at all — narrow ifstream paths
# are read in the ANSI codepage on Windows. No ASCII-named test asset can catch
# this, which is exactly why it needs a check of its own.
add_executable(assetpath_check assetpath_check/main.cpp)
target_link_libraries(assetpath_check PRIVATE gws_asset_pipeline)
target_compile_features(assetpath_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(assetpath_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# assetwatch_check: the shared file watcher behind every hot reload. Mostly
# negative behaviours — not firing on a half-written file is the property that
# makes reload trustworthy. Time is injected, so the suite stays instant.
add_executable(assetwatch_check assetwatch_check/main.cpp)
target_link_libraries(assetwatch_check PRIVATE gws_assets)
target_compile_features(assetwatch_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(assetwatch_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# playchanges_check: play mode must not eat a tuning pass — and must never write
# to the authored scene unless a change was explicitly ticked. Compiles the
# editor's play_mode_changes.cpp + ecs_bridge.cpp directly; both are GPU-free, so
# this whole feature is verifiable headless.
add_executable(playchanges_check
    playchanges_check/main.cpp
    ../editor/src/play_mode_changes.cpp
    ../editor/src/ecs_bridge.cpp)
target_include_directories(playchanges_check PRIVATE ../editor/include)
target_link_libraries(playchanges_check PRIVATE gws_ecs gws_reflection gws_jobs scene)
target_compile_features(playchanges_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(playchanges_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# gameplay_check: F1/F2 ECS gameplay-authoring data path (authorable registry +
# reflection-driven read/write that the editor inspector uses).
add_executable(gameplay_check gameplay_check/main.cpp)
target_link_libraries(gameplay_check PRIVATE gws_ecs gws_reflection)
target_compile_features(gameplay_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(gameplay_check PRIVATE -static -static-libgcc -static-libstdc++)
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

# dedicated_server: Stage 7 H3 — headless authoritative server (ECS world +
# transport + replication + Jolt physics at a fixed tick, no renderer/audio).
# Deployable binary.
add_executable(dedicated_server dedicated_server/main.cpp)
target_link_libraries(dedicated_server PRIVATE network gws_ecs gws_serialization gws_physics)
target_compile_features(dedicated_server PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(dedicated_server PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# server_check: verifies the dedicated server end-to-end over real loopback UDP
# (connect -> avatar spawn -> input -> authoritative apply -> replicate -> despawn),
# plus Jolt physics stepped inside the server tick (falling body settles +
# replicates + deterministic).
add_executable(server_check server_check/main.cpp)
target_link_libraries(server_check PRIVATE network gws_ecs gws_serialization gws_physics)
target_compile_features(server_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(server_check PRIVATE -static -static-libgcc -static-libstdc++)
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
    ${CMAKE_SOURCE_DIR}/editor/src/script_host_cs.cpp
    ${CMAKE_SOURCE_DIR}/editor/src/script_params.cpp)
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

# texture_check: Stage 2 texture support & handling verification against a real
# Vulkan device (headless): SamplerCache dedup, TextureManager defaults/dedup/
# cooked-.ctex/hot-reload, GPU mip generation, BC7/BC5 compressed upload,
# Material shared-defaults + rewrite, streamable .vt tile access. Links the
# renderer (Texture/Manager/Material live there) + bc7enc for the in-process
# cook; NOT -static (the renderer stack isn't static-link-clean — run with the
# toolchain DLLs on PATH, same as the editor).
add_executable(texture_check texture_check/main.cpp)
target_link_libraries(texture_check PRIVATE renderer gws_asset_pipeline bc7enc)
target_include_directories(texture_check PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/core/asset_pipeline)
target_compile_features(texture_check PRIVATE cxx_std_20)

# model_check: headless custom-model load + culling verification — loads models
# through the editor's own AssetMeshCache (scene_render_bridge.h) and runs the
# CPU frustum cull over the results. Diagnoses "model falls back to a cube"
# (load failure) and false-cull regressions without launching the editor.
add_executable(model_check model_check/main.cpp)
target_include_directories(model_check PRIVATE ${CMAKE_SOURCE_DIR}/editor/include)
target_link_libraries(model_check PRIVATE renderer scene gws_ecs gws_jobs gws_serialization meshoptimizer gws_asset_pipeline)
target_compile_features(model_check PRIVATE cxx_std_20)

# anim_check: Stage 5 animation verification (headless, no device) — two-bone
# + foot IK, and the data-driven AnimationStateMachine (transitions / blend /
# root motion). The animation classes live in the renderer lib (pure CPU math).
add_executable(anim_check anim_check/main.cpp)
target_link_libraries(anim_check PRIVATE renderer)
target_compile_features(anim_check PRIVATE cxx_std_20)

# skinning_check: Stage 5 GPU compute-skinning verification (live device) —
# builds a SkinnedMesh, runs the skinning dispatch with known bone matrices,
# maps the output vertex buffer back, and checks the weighted transforms.
add_executable(skinning_check skinning_check/main.cpp)
target_link_libraries(skinning_check PRIVATE renderer)
target_compile_features(skinning_check PRIVATE cxx_std_20)

# NOTE: hub_check / update_check moved to the standalone GameWorldshaperHub repo
# (its tests/ + hub_selftest) along with the Hub itself.

# skinned_import_check: rigged-glTF import verification (headless, no device) —
# generates a minimal rig, imports it via load_skinned_gltf, and checks the
# skeleton hierarchy, joint remap, inverse-bind, and animation sampling.
add_executable(skinned_import_check skinned_import_check/main.cpp)
target_link_libraries(skinned_import_check PRIVATE renderer)
target_compile_features(skinned_import_check PRIVATE cxx_std_20)

# ai_check: Stage 9 AI verification (headless, no device) — navmesh A*+funnel
# pathfinding (detour around a wall, unreachable islands) + behavior trees
# (selector/sequence running-memory, inverter, cooldown).
add_executable(ai_check ai_check/main.cpp)
target_link_libraries(ai_check PRIVATE gws_ai)
target_compile_features(ai_check PRIVATE cxx_std_20)

# ui_check: game-UI framework verification (headless, no device) — anchored
# layout, resolution-independent scaling, pointer/click semantics, focus
# navigation, HUD draw-list emission.
add_executable(ui_check ui_check/main.cpp)
target_link_libraries(ui_check PRIVATE gws_ui)
target_compile_features(ui_check PRIVATE cxx_std_20)

# world_check: world-streaming verification (headless, no device) — grid math,
# cell load/unload with radius hysteresis, floating-origin rebase preserving
# absolute world positions.
add_executable(world_check world_check/main.cpp)
target_link_libraries(world_check PRIVATE gws_world)
target_compile_features(world_check PRIVATE cxx_std_20)

# vfx_check: particle-system verification (headless, no device) — spawn-by-rate,
# gravity/drag integration, life fade/reap, floating-origin rebase, and
# camera-facing billboard generation (quads face any camera).
add_executable(vfx_check vfx_check/main.cpp)
target_link_libraries(vfx_check PRIVATE gws_vfx)
target_compile_features(vfx_check PRIVATE cxx_std_20)

# combat_check: frame-data combat verification (headless, no device) — hitbox
# active windows, per-attack dedup, facing rotation, dodge i-frames, parry
# punish, multi-hit cadence, poise break, floating-origin consistency.
add_executable(combat_check combat_check/main.cpp)
target_link_libraries(combat_check PRIVATE gws_combat)
target_compile_features(combat_check PRIVATE cxx_std_20)

# crash_check: crash-handler verification — spawns child processes that segfault
# / abort / throw and confirms each leaves a crash report + minidump on disk,
# plus in-process ring-buffer + write_report checks.
add_executable(crash_check crash_check/main.cpp)
target_link_libraries(crash_check PRIVATE gws_diagnostics)
target_compile_features(crash_check PRIVATE cxx_std_20)

# gws: the engine command line (Phase 1, issue #7). One entry point to
# everything the engine does without a GUI — the check suite, cooking,
# validation, builds — with --json on every command so CI, scripts and external
# agents are first-class callers. Links nothing: it drives the sibling tools.
add_executable(gws gws/main.cpp)
target_compile_features(gws PRIVATE cxx_std_20)
target_compile_definitions(gws PRIVATE GWS_ENGINE_VERSION="${PROJECT_VERSION}")
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(gws PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# docgen: the component reference, generated from the authorable registry
# (issue #10). Documentation that cannot drift — it reads the same registration
# the inspector, the save sidecar and replication read. Driven by `gws docs`.
add_executable(docgen docgen/main.cpp)
target_link_libraries(docgen PRIVATE gws_ecs gws_reflection)
target_include_directories(docgen PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_features(docgen PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(docgen PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# headless: run a project's gameplay simulation with no window or renderer
# (issue #64). Driven by `gws run --headless`. Ticks through the SAME shared
# gameplay frame the editor uses, so headless and play mode cannot diverge.
add_executable(headless headless/main.cpp)
target_link_libraries(headless PRIVATE gws_ecs gws_reflection gws_serialization)
target_include_directories(headless PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_features(headless PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(headless PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# projectctl: read/modify a project's gameplay state from outside the editor
# (issue #8). The agent-facing half of the CLI — operates on files so every
# change is a diff. Driven by `gws project`.
add_executable(projectctl projectctl/main.cpp)
target_link_libraries(projectctl PRIVATE gws_ecs gws_reflection gws_serialization)
target_include_directories(projectctl PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_features(projectctl PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(projectctl PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# ---------------------------------------------------------------------------
# Ship the command line with the engine.
#
# Without this the CLI exists only in a dev tree: anyone installing through the
# Hub gets editor.exe and nothing else, so `gws` is unavailable to user
# automation, to the samples repo's CI, and to the Hub's upgrade preflight —
# which is specified to run the engine's own tooling against a candidate
# version. The sibling tools are included because gws drives them: `cook` needs
# assetcook, `run` needs headless, `project` needs projectctl, `docs` needs
# docgen.
#
# CPACK_STRIP_FILES strips these in the package, so the download cost is small.
install(TARGETS gws headless projectctl docgen assetcook
        RUNTIME DESTINATION "${GWS_ENGINE_DEST}" COMPONENT engine)

# innerloop_check: measure the inner loop so it can be defended (issue #13).
# Named *_check so `gws test` discovers it automatically. Report-only unless
# --enforce: timing on a shared runner is noisy, and a gate that cries wolf
# gets ignored — which is the failure this phase exists to prevent.
add_executable(innerloop_check innerloop_check/main.cpp)
target_compile_features(innerloop_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(innerloop_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()
