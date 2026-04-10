cmake_minimum_required(VERSION 3.20)

# Input system - typed action mapping for gameplay

add_library(input STATIC)

# Header files
target_sources(input
    PRIVATE
        input_manager.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            input_action.h
            input_manager.h
)

# Include directories
target_include_directories(input
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Dependencies
target_link_libraries(input
    PUBLIC
        glfw
        glm
        spdlog::spdlog
)

# Compiler flags
if(MSVC)
    target_compile_options(input PRIVATE /W4)
else()
    target_compile_options(input PRIVATE -Wall -Wextra -Wpedantic)
endif()
