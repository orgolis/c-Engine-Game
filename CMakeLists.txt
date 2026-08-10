cmake_minimum_required(VERSION 3.20)
project(GameWorldshaper
    VERSION 0.4.3
    DESCRIPTION "A custom 3D game engine for an open-world multiplayer action RPG"
    LANGUAGES C CXX)

# Set C++ standard to C++20 for modern features
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Debug format for parallel compilation - use C7 format (/Z7) instead of /Zi
if(MSVC)
    string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

# Compiler-specific flags
if(MSVC)
    # Windows/MSVC
    add_compile_options(/W4 /wd4251 /wd4275 /wd4459)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    # GCC/Clang (Linux, WSL) — -Werror omitted; pre-existing warnings in
    # physics/tinygltf fail on MinGW. Fix those files before re-enabling.
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# MinGW: large translation units (notably the monolithic editor main.cpp, ~6.5k
# lines) overflow the default COFF object format in Debug (-g), failing to
# assemble with "file too big". Big-obj lifts the section/size limit. Harmless
# for small objects, so applied to every target (incl. third_party subdirs).
if(MINGW)
    add_compile_options(-Wa,-mbig-obj)
endif()

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ============================================================================
# Redistributable runtime (Phase 3): the editor + hub link the MinGW g++ runtime
# dynamically (renderer stack isn't static-link-clean), so on a clean Windows box
# they need these 3 DLLs next to the exe. We copy them beside every built exe
# (so dev builds run without the toolchain on PATH) and install() them so the
# packaged product is self-contained. vulkan-1.dll is a system DLL (GPU drivers).
# ============================================================================
if(MINGW OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    get_filename_component(GWS_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(GWS_RUNTIME_DLLS
        "${GWS_MINGW_BIN}/libgcc_s_seh-1.dll"
        "${GWS_MINGW_BIN}/libstdc++-6.dll"
        "${GWS_MINGW_BIN}/libwinpthread-1.dll")
endif()

# Copy the runtime DLLs next to a target after build, and install() them to
# `dest`. No-op on non-MinGW toolchains.
function(gws_bundle_runtime tgt dest)
    if(GWS_RUNTIME_DLLS)
        foreach(dll ${GWS_RUNTIME_DLLS})
            add_custom_command(TARGET ${tgt} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${dll}" "$<TARGET_FILE_DIR:${tgt}>"
                VERBATIM)
        endforeach()
        install(FILES ${GWS_RUNTIME_DLLS} DESTINATION "${dest}" COMPONENT engine)
    endif()
endfunction()

# The engine package is flat (editor.exe + DLLs + content at the root). CI
# uploads it as a release asset; the Hub downloads it, finds editor.exe, and
# installs it into %LOCALAPPDATA%/GameWorldshaper/Engines/<version>/.
set(GWS_ENGINE_DEST ".")

# Options
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(BUILD_TESTS "Build tests" ON)
option(BUILD_DOCS "Build documentation" OFF)

# Add third-party dependencies early
# spdlog bundled fmt triggers C4459 warning - suppress it
set(SPDLOG_WCHAR_FILENAMES ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/spdlog)

# GLM (header-only math library)
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE third_party/glm)

# EnTT (Entity Component System - header-only)
add_library(entt INTERFACE)
target_include_directories(entt INTERFACE third_party/entt/src)

# GLFW (window and input management)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw)
# Suppress third-party warnings on GCC/Clang (global -Werror would fail glfw)
if(NOT MSVC)
    target_compile_options(glfw PRIVATE -w)
endif()

# Vulkan SDK (graphics API)
find_package(Vulkan REQUIRED)

# Vulkan Memory Allocator (header-only)
add_subdirectory(third_party/vma)

# glslang (shader compilation)
add_subdirectory(third_party/glslang)

# GLAD (OpenGL function loader)
add_subdirectory(third_party/glad)

# tinygltf (glTF 2.0 loader)
add_subdirectory(third_party/tinygltf)

# ufbx (single-file FBX loader — feeds the asset cooker's FBX importer).
# Upstream ships no CMakeLists.txt, so our build glue lives beside the submodule
# rather than inside it — see third_party/ufbx.cmake for why.
include(third_party/ufbx.cmake)

# ENet (reliable-UDP transport — Stage 7 networking)
add_subdirectory(third_party/enet)

# miniaudio (device + decode — Stage 6 audio backend, custom mixer on top)
add_subdirectory(third_party/miniaudio)

# tinyusdz (USD loader — feeds the asset cooker's USD importer). Minimal build:
# library + Tydra only, no tools/tests/examples/audio/exr. NO_WERROR + exceptions
# are required for the MinGW build (its Win32 io path throws + has unused fns).
set(TINYUSDZ_USE_CCACHE           OFF CACHE BOOL "" FORCE)
set(TINYUSDZ_BUILD_TESTS          OFF CACHE BOOL "" FORCE)
set(TINYUSDZ_BUILD_EXAMPLES       OFF CACHE BOOL "" FORCE)
set(TINYUSDZ_BUILD_BENCHMARKS     OFF CACHE BOOL "" FORCE)
set(TINYUSDZ_WITH_USD_TO_GLTF     OFF CACHE BOOL "" FORCE)
set(TINYUSDZ_WITH_PYTHON          OFF CACHE BOOL "" FORCE)
set(TINYUSDZ_WITH_C_API           OFF CACHE BOOL "" FORCE)
set(TINYUSDZ_WITH_TYDRA           ON  CACHE BOOL "" FORCE)
set(TINYUSDZ_WITH_EXR             OFF CACHE BOOL "" FORCE)
set(TINYUSDZ_WITH_TIFF            OFF CACHE BOOL "" FORCE)
set(TINYUSDZ_WITH_AUDIO           OFF CACHE BOOL "" FORCE)
set(TINYUSDZ_WITH_USDFBX          OFF CACHE BOOL "" FORCE)
set(TINYUSDZ_WITH_USDMTLX         OFF CACHE BOOL "" FORCE)
set(TINYUSDZ_WITH_TOOL_USDA_PARSER OFF CACHE BOOL "" FORCE)
set(TINYUSDZ_WITH_TOOL_USDC_PARSER OFF CACHE BOOL "" FORCE)
set(TINYUSDZ_NO_WERROR            ON  CACHE BOOL "" FORCE)
set(TINYUSDZ_CXX_EXCEPTIONS       ON  CACHE BOOL "" FORCE)

# Vendor fix-up (idempotent, applied at configure time).
#
# Upstream passes "-Wa, -mbig-obj" to MinGW GCC. The space makes gcc treat
# "-mbig-obj" as a separate argument, which the assembler then reads as a
# filename and the build dies. It must be "-Wa,-mbig-obj".
#
# This used to be an UNCOMMITTED hand edit inside the submodule working tree,
# which meant it existed only on this machine — a fresh clone got the broken
# flag and could not build. Doing it here makes the fix travel with the repo.
if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(_tinyusdz_cml "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tinyusdz/CMakeLists.txt")
    if(EXISTS "${_tinyusdz_cml}")
        file(READ "${_tinyusdz_cml}" _tinyusdz_src)
        string(FIND "${_tinyusdz_src}" "\"-Wa, -mbig-obj\"" _tinyusdz_bad)
        if(NOT _tinyusdz_bad EQUAL -1)
            string(REPLACE "\"-Wa, -mbig-obj\"" "\"-Wa,-mbig-obj\"" _tinyusdz_src "${_tinyusdz_src}")
            file(WRITE "${_tinyusdz_cml}" "${_tinyusdz_src}")
            message(STATUS "tinyusdz: patched '-Wa, -mbig-obj' -> '-Wa,-mbig-obj' (MinGW assembler)")
        endif()
        unset(_tinyusdz_src)
    endif()
    unset(_tinyusdz_cml)
endif()

# tinyusdz relies on <cstdint> arriving transitively through other headers.
# GCC 13 tightened that and GCC 14+ removed it, so on a newer compiler its
# value-types.hh fails with "'uint32_t' does not name a type" (hundreds of
# them). Local builds on g++ 13.2 happen to survive; CI runners do not.
#
# Force-including the header is the standard remedy and leaves the vendored
# source untouched. Scoped to this add_subdirectory only: the directory's
# COMPILE_OPTIONS are inherited by the subdirectory, then restored.
get_directory_property(_gws_prev_copts COMPILE_OPTIONS)
if(NOT MSVC)
    # CXX only — tinyusdz also compiles bundled C (its vendored lz4), and
    # <cstdint> is a C++ header, so an unguarded flag fails those with
    # "fatal error: cstdint: No such file or directory".
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-include;cstdint>")
endif()
add_subdirectory(third_party/tinyusdz)
set_directory_properties(PROPERTIES COMPILE_OPTIONS "${_gws_prev_copts}")
unset(_gws_prev_copts)

# Jolt Physics (Stage 4 — rigid-body dynamics). Minimal: the `Jolt` static lib
# only. CROSS_PLATFORM_DETERMINISTIC=ON because networking (Stage 7) re-sims
# physics. No GPU-compute (DX12 needs the DX SDK, absent on MinGW), no
# warnings-as-errors, no LTO (breaks MinGW), no FP-exceptions.
set(ENABLE_ALL_WARNINGS              OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION     OFF CACHE BOOL "" FORCE)
set(CROSS_PLATFORM_DETERMINISTIC     ON  CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE    OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12                     OFF CACHE BOOL "" FORCE)
set(USE_ASSERTS                      OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/JoltPhysics/Build)

# stb_image (single-header image loader, PNG/JPG/BMP/etc.)
add_subdirectory(third_party/stb)

# meshoptimizer (LOD simplification + cache optimization)
add_subdirectory(third_party/meshoptimizer)

# --- Vendored codecs (Stage 2: compression + texture block compression) ---
# LZ4 (BSD-2): fast block compression for cooked mesh/anim.
add_library(lz4 STATIC
    third_party/lz4/lz4.c
    third_party/lz4/lz4hc.c
    third_party/lz4/xxhash.c)
target_include_directories(lz4 PUBLIC third_party/lz4)

# Zstd (BSD-3): higher-ratio bulk compression. C sources only; no asm/legacy.
file(GLOB ZSTD_SRC
    third_party/zstd/lib/common/*.c
    third_party/zstd/lib/compress/*.c
    third_party/zstd/lib/decompress/*.c)
add_library(zstd STATIC ${ZSTD_SRC})
target_include_directories(zstd PUBLIC third_party/zstd/lib)
target_compile_definitions(zstd PRIVATE ZSTD_LEGACY_SUPPORT=0 ZSTD_DISABLE_ASM=1)

# bc7enc (MIT/public-domain): BC1/BC3/BC4/BC5 (rgbcx) + BC7 (bc7enc) encoders.
add_library(bc7enc STATIC
    third_party/bc7enc/bc7enc.c
    third_party/bc7enc/bc7enc_impl.cpp)
target_include_directories(bc7enc PUBLIC third_party/bc7enc)

# Dear ImGui (immediate-mode GUI library)
add_library(imgui STATIC)
target_sources(imgui PRIVATE
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_demo.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_vulkan.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
)
target_include_directories(imgui PUBLIC
    third_party/imgui
    third_party/imgui/backends
)
target_link_libraries(imgui PUBLIC
    Vulkan::Vulkan
    glfw
)
set_target_properties(imgui PROPERTIES
    CXX_STANDARD 20
)

# Catch2 for testing
if(BUILD_TESTS)
    add_subdirectory(third_party/catch2)
endif()

# Add subdirectories
add_subdirectory(engine)
add_subdirectory(editor)
# NOTE: the Hub launcher now lives in its own repository (GameWorldshaperHub).
# This repo builds only the engine + editor, and publishes an engine package
# (see the release CI + engine-only CPack layout below) that the Hub installs.
add_subdirectory(game)
add_subdirectory(tools)   # offline content tools (assetcook, Stage 2.2)

# Add tests
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "GameWorldshaper Configuration:")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build Tests: ${BUILD_TESTS}")
message(STATUS "  Build Docs: ${BUILD_DOCS}")
message(STATUS "")

# ============================================================================
# Packaging: CPack produces the ENGINE package (a release asset the Hub installs).
# Layout is flat — editor.exe + runtime DLLs + default content at the package
# root. On each release the CI (.github/workflows/release-engine.yml) runs
# `cpack -G ZIP` and uploads the resulting zip; the Hub downloads it, finds
# editor.exe, and installs it under %LOCALAPPDATA%/GameWorldshaper/Engines/<ver>/.
# ============================================================================
set(CPACK_PACKAGE_NAME "GameWorldshaper-Engine")
set(CPACK_PACKAGE_VENDOR "GameWorldshaper")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "GameWorldshaper engine + editor (Hub-installable package)")
if(WIN32)
    set(CPACK_GENERATOR "ZIP")   # the engine package is a plain zip asset
endif()
# Package ONLY the "engine" component (editor + runtime DLLs + content) into one
# archive, so the third-party libraries' own install() rules (Jolt/GLFW/enet
# headers + libs) are excluded from the release asset.
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_ALL engine)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
set(CPACK_ARCHIVE_FILE_NAME "GameWorldshaper-Engine-${PROJECT_VERSION}-win64")
# Strip debug symbols from the packaged editor.exe + DLLs. The build is a debug
# build (-g), which makes editor.exe ~160 MB; the symbols aren't needed to RUN
# it, so stripping shrinks the download dramatically without a Release rebuild.
set(CPACK_STRIP_FILES TRUE)

include(CPack)
