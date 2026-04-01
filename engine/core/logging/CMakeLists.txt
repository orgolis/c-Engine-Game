# Ensure spdlog is available
if(NOT TARGET spdlog::spdlog)
    message(FATAL_ERROR "spdlog target not found. Make sure spdlog is added as a subdirectory in the root CMakeLists.txt")
endif()

add_library(gws_logging STATIC)

target_sources(gws_logging PRIVATE
    logger.cpp
)

target_include_directories(gws_logging PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Link against spdlog (includes its public headers)
target_link_libraries(gws_logging PUBLIC
    spdlog::spdlog
)

# C++20 standard
target_compile_features(gws_logging PUBLIC cxx_std_20)
