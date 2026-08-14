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

# componentsync_check: every scene component kind must reach the ECS (3.6). Four
# of thirteen used to -- a gameplay system could not see a light, could not tell
# whether an entity had a collider, and could not read a camera's FOV. Asserts
# both halves: every kind ARRIVES with its values, and no kind appears on an
# entity that does not have one (or a query for "every light" returns the scene).
add_executable(componentsync_check componentsync_check/main.cpp ../editor/src/ecs_bridge.cpp)
target_include_directories(componentsync_check PRIVATE ../editor/include)
target_link_libraries(componentsync_check PRIVATE gws_ecs gws_reflection gws_jobs scene gws_logging)
target_compile_features(componentsync_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(componentsync_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# ecsauthority_check: an ECS write to a Transform must survive the frame. The
# assertions are symmetric on purpose -- a flip that quietly changed behaviour
# for UNtagged entities would be far worse than one that did nothing.
add_executable(ecsauthority_check ecsauthority_check/main.cpp ../editor/src/ecs_bridge.cpp)
target_include_directories(ecsauthority_check PRIVATE ../editor/include)
target_link_libraries(ecsauthority_check PRIVATE gws_ecs gws_reflection gws_jobs scene gws_logging)
target_compile_features(ecsauthority_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(ecsauthority_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# vehiclemove_check: the authority flip seen from the outside. Driving a vehicle
# integrates its ECS transform every frame and the editor exposes that to
# scripts -- so before the flip, a script called drive(), the speed climbed, and
# the car on screen did not move. Asserts the OUTCOME (the scene transform ends
# where the drive model says) rather than the tag, since a tag-only assertion
# would still pass with the sync running the wrong way.
add_executable(vehiclemove_check vehiclemove_check/main.cpp ../editor/src/ecs_bridge.cpp)
target_include_directories(vehiclemove_check PRIVATE ../editor/include)
target_link_libraries(vehiclemove_check PRIVATE gws_ecs gws_reflection gws_jobs scene gws_logging)
target_compile_features(vehiclemove_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(vehiclemove_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# scenereflect_check: scene components describe themselves (3.7, tractable half).
# Adding one field used to mean four hand edits with a silent failure mode -- a
# component that saves but does not load. The load-bearing assertion is that an
# UNREGISTERED field is caught, since that is the mistake this prevents.
# reflectio_check: reflected components survive save and load. Particle emitters
# and NPC agents were never written to the scene file at all -- configure one,
# save, reopen, and every setting was silently back at its default. The
# assertion that earns its keep is that a field NO serializer code names still
# round-trips, because the text is generated from the reflection.
# sceneio_check: the scene FILE gives back the scene that was saved. The format
# had no round-trip test, which is how emitters and NPC agents came to be
# missing from it entirely. reflectio_check covers the reflection layer; this
# covers the wiring -- a library that round-trips perfectly and a serializer
# that never calls it look identical from the library's side.
add_executable(sceneio_check sceneio_check/main.cpp ../editor/src/scene_serializer.cpp)
target_include_directories(sceneio_check PRIVATE ../editor/include ../engine/scene/include)
target_link_libraries(sceneio_check PRIVATE scene gws_reflection gws_ecs gws_logging glm)
target_compile_features(sceneio_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(sceneio_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

add_executable(reflectio_check reflectio_check/main.cpp)
target_include_directories(reflectio_check PRIVATE ../engine/scene/include)
target_link_libraries(reflectio_check PRIVATE gws_reflection gws_ecs glm)
target_compile_features(reflectio_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(reflectio_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

add_executable(scenereflect_check scenereflect_check/main.cpp)
target_include_directories(scenereflect_check PRIVATE ../engine/scene/include)
target_link_libraries(scenereflect_check PRIVATE gws_reflection gws_ecs glm)
target_compile_features(scenereflect_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(scenereflect_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# locomotion_check: idle-vs-walk must not flicker. The rule looks trivial and
# its failure mode is purely visual -- a standing character twitching between
# clips every frame. Pure function of position deltas, so it is assertions
# rather than something a human notices later.
add_executable(locomotion_check locomotion_check/main.cpp)
target_include_directories(locomotion_check PRIVATE ../editor/include)
target_link_libraries(locomotion_check PRIVATE glm)
target_compile_features(locomotion_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(locomotion_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# npcagent_check: perception, decision and movement, connected. Each part was
# verified separately and driven by nothing. Asserts OBSERVABLE behaviour --
# the failure mode is an agent that ticks happily and never moves.
add_executable(npcagent_check npcagent_check/main.cpp ../editor/src/ecs_bridge.cpp)
target_include_directories(npcagent_check PRIVATE ../editor/include)
target_link_libraries(npcagent_check PRIVATE gws_ai gws_ecs gws_reflection gws_jobs scene gws_logging)
target_compile_features(npcagent_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(npcagent_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# emitter_check: particles must actually simulate on real entities. The system
# was verified and then referenced only by a feature-toggle enum. Pins the
# connection -- especially the rebase contract, whose failure is silent.
add_executable(emitter_check emitter_check/main.cpp)
target_include_directories(emitter_check PRIVATE ../editor/include)
target_link_libraries(emitter_check PRIVATE gws_vfx scene gws_logging)
target_compile_features(emitter_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(emitter_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# includes_check: a file naming a fixed-width type must include <cstdint>. This
# is the lint for the bug that broke the v0.6.0 release. Compiling headers alone
# does NOT catch it -- verified, not assumed: on this toolchain <memory> pulls
# <cstdint> in transitively so the header compiles fine, while CI's libstdc++
# does not. Anything depending on which standard header leaks which type can
# only reproduce it by luck; a text scan cannot.
add_executable(includes_check includes_check/main.cpp)
target_compile_features(includes_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(includes_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# snap_check: the rounding rules are the part that can be subtly wrong. Snapping
# negatives the wrong way, or drifting when a drag re-applies it every frame, is
# invisible on one drag and obvious after twenty -- by which point the symptom is
# "the walls don't meet" and the cause is three files away.
add_executable(snap_check snap_check/main.cpp)
target_include_directories(snap_check PRIVATE ../editor/include)
target_link_libraries(snap_check PRIVATE glm)
target_compile_features(snap_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(snap_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# shadergraph_check: codegen is where a shader graph fails SILENTLY. A graph
# emitting statements in the wrong order, dropping a branch, or writing an
# integer literal produces a shader that compiles cleanly and shades subtly
# wrong -- and "this material looks a bit off" points nowhere near the cause.
# Asserts declaration order, explicit conversion, cycle refusal, determinism
# and dead-code exclusion.
add_executable(shadergraph_check shadergraph_check/main.cpp
                                 ../engine/core/shadergraph/material_graph.cpp)
target_include_directories(shadergraph_check PRIVATE ../engine/core/shadergraph)
target_compile_features(shadergraph_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(shadergraph_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# curve_check: the half of 4.5 that can be wrong without LOOKING wrong. A
# gradient that reads its last stop early, or a curve that steps where it should
# ease, is invisible in a screenshot and obvious in motion -- where it reads as
# "the effect looks cheap" rather than as a bug with a location. Asserts the
# SHAPE of the interpolation, not just its endpoints.
add_executable(curve_check curve_check/main.cpp)
target_include_directories(curve_check PRIVATE ../engine/core)
target_link_libraries(curve_check PRIVATE glm)
target_compile_features(curve_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(curve_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# palette_check: the command palette RANKS the way a person expects. A palette
# that finds the right command but puts it fourth is one people stop using --
# the whole value is that the first hit is the one you meant. These assert
# order, not membership: "the list contains Save Scene" is true of a palette
# that returns everything in registration order.
add_executable(palette_check palette_check/main.cpp)
target_include_directories(palette_check PRIVATE ../editor/include)
target_compile_features(palette_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(palette_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# phase3exit_check: Phase 3's exit criterion, RUN rather than asserted. Every
# clause of it has its own passing check; none of them covers the sentence as a
# whole, and this phase's recurring lesson is that the bugs live between the
# parts. One scene: terrain in, navmesh baked from it, an agent walking it,
# locomotion driven by real motion, streaming forcing a rebase -- then all of it
# has to keep working.
add_executable(phase3exit_check phase3exit_check/main.cpp ../editor/src/ecs_bridge.cpp)
target_include_directories(phase3exit_check PRIVATE ../editor/include)
target_link_libraries(phase3exit_check PRIVATE gws_ai gws_world gws_ecs gws_reflection gws_jobs scene gws_logging)
target_compile_features(phase3exit_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(phase3exit_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# navrebase_check: streaming and AI agents were verified separately; this is the
# interaction, which is where the bug was. A floating-origin rebase moves every
# entity but not the baked navmesh, nor the world-space goals and waypoints the
# agents remember -- and shifts are hundreds of units against a mesh tens across,
# so the agent lands nowhere near it and nothing reports an error.
add_executable(navrebase_check navrebase_check/main.cpp)
target_link_libraries(navrebase_check PRIVATE gws_ai)
target_compile_features(navrebase_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(navrebase_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# navbake_check: the navmesh must describe the scene that actually exists. The
# builder was verified long ago; its INPUT never was -- the only thing feeding it
# was a synthetic grid inside a demo. Asserts what the bake contains AND what it
# deliberately excludes, because both failures are silent.
add_executable(navbake_check navbake_check/main.cpp)
target_include_directories(navbake_check PRIVATE ../editor/include)
target_link_libraries(navbake_check PRIVATE gws_ai scene gws_logging)
target_compile_features(navbake_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(navbake_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# cullbench_check: parallel culling has to be FASTER, and agree with serial.
# Establishes where the crossover is, so the threshold in culling.cpp is a
# measurement rather than a guess.
add_executable(cullbench_check cullbench_check/main.cpp)
target_link_libraries(cullbench_check PRIVATE gws_jobs glm)
target_compile_features(cullbench_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(cullbench_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# editcoalesce_check: one drag must be one undo entry. The awkward cases are
# gestures people make without thinking -- dragging back to the start, clicking
# a checkbox, switching fields mid-drag -- and none can be checked by reading
# code. Keeping the rule ImGui-free is what makes them assertions.
add_executable(editcoalesce_check editcoalesce_check/main.cpp ../editor/src/edit_coalescer.cpp)
target_include_directories(editcoalesce_check PRIVATE ../editor/include)
target_compile_features(editcoalesce_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(editcoalesce_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# shadersource_check: the GLSL a human edits must still compile to the SPIR-V
# that ships. shaders/lighting_pass.frag had been sitting broken in the tree
# because nothing ever fed it to a compiler. Compares the instruction stream
# minus debug info, so a toolchain upgrade does not read as drift.
add_executable(shadersource_check shadersource_check/main.cpp)
target_include_directories(shadersource_check PRIVATE ../engine/renderer/gpu/vulkan)
target_compile_features(shadersource_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(shadersource_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# undo_check: undo must act on the entity you acted on. Pins the scene-level
# invariants the editor commands depend on — id-based lookup (names are not
# unique) and identity preserved across remove/re-add, which is what makes
# delete-undo correct.
add_executable(undo_check undo_check/main.cpp)
target_link_libraries(undo_check PRIVATE scene)
target_compile_features(undo_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(undo_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# taskrunner_check: background work must not freeze the editor, and its
# completion callbacks must land on the editor thread. Both are the kind of
# property that seems fine until a driver crash appears days later in code that
# looks correct, so they are asserted directly.
add_executable(taskrunner_check taskrunner_check/main.cpp)
target_link_libraries(taskrunner_check PRIVATE gws_jobs)
target_compile_features(taskrunner_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(taskrunner_check PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# assetpath_check: a non-ASCII asset filename must still load. Caught a real
# failure where a 64 MB model could not be opened at all — narrow ifstream paths
# are read in the ANSI codepage on Windows. No ASCII-named test asset can catch
# this, which is exactly why it needs a check of its own.
add_executable(assetpath_check assetpath_check/main.cpp)
target_link_libraries(assetpath_check PRIVATE gws_asset_pipeline gws_logging)
target_include_directories(assetpath_check PRIVATE ../editor/include)
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

# physrebase_check: a floating-origin rebase must be a change of coordinates and
# nothing else. Without it the Jolt world stays at the old origin and writes
# pre-rebase positions back over the entities -- the dynamic half of the level
# snaps back while the static half moves. The player is a CharacterVirtual, not
# a body, so it is not in the body list and gets left behind on its own.
add_executable(physrebase_check physrebase_check/main.cpp)
target_link_libraries(physrebase_check PRIVATE gws_physics)
target_compile_features(physrebase_check PRIVATE cxx_std_20)
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(physrebase_check PRIVATE -static -static-libgcc -static-libstdc++)
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
