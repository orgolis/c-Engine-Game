cmake_minimum_required(VERSION 3.20)
project(GameWorldshaper 
    VERSION 0.1.0 
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

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

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

# ufbx (single-file FBX loader — feeds the asset cooker's FBX importer)
add_subdirectory(third_party/ufbx)

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
add_subdirectory(third_party/tinyusdz)

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
