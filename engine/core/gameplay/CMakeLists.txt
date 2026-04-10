cmake_minimum_required(VERSION 3.20)

# Gameplay systems - runtime foundations

add_library(gameplay STATIC)

# Header files
target_sources(gameplay
    PRIVATE
        stat_modifier.cpp
        creature.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            stat_modifier.h
            character_stats.h
            ability_system.h
            combat_system.h
            creature.h
            deterministic_state.h
)

# Include directories
target_include_directories(gameplay
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# Dependencies
target_link_libraries(gameplay
    PUBLIC
        glm
        spdlog::spdlog
        input
        physics
)

# Compiler flags
if(MSVC)
    target_compile_options(gameplay PRIVATE /W4)
else()
    target_compile_options(gameplay PRIVATE -Wall -Wextra -Wpedantic)
endif()
