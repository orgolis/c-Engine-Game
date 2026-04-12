cmake_minimum_required(VERSION 3.20)

# Character Controller Module
add_library(character
    src/input_buffer.cpp
    src/character_controller.cpp
    src/character_state.cpp
    src/ground_detector.cpp
    src/character_stats.cpp
    src/locomotion_system.cpp
)

target_include_directories(character PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Link dependencies
target_link_libraries(character PUBLIC 
    spdlog::spdlog
    glm
    entt
)

# Enable C++17
target_compile_features(character PRIVATE cxx_std_17)
