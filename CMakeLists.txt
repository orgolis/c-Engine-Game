cmake_minimum_required(VERSION 3.20)
project(GameWorldshaper 
    VERSION 0.1.0 
    DESCRIPTION "A custom 3D game engine for an open-world multiplayer action RPG"
    LANGUAGES CXX)

# Set C++ standard to C++20 for modern features
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler-specific flags
if(MSVC)
    # Windows/MSVC
    add_compile_options(/W4 /WX /wd4251 /wd4275 /wd4459)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    # GCC/Clang (Linux, WSL)
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
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

# Add subdirectories
add_subdirectory(engine)
add_subdirectory(editor)
add_subdirectory(game)

# Add tests
if(BUILD_TESTS)
    enable_testing()
    # add_subdirectory(tests)
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "GameWorldshaper Configuration:")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build Tests: ${BUILD_TESTS}")
message(STATUS "  Build Docs: ${BUILD_DOCS}")
message(STATUS "")
