cmake_minimum_required(VERSION 3.20)

# Network System Module
add_library(network
    src/deterministic_simulation.cpp
    src/input_synchronizer.cpp
    src/network_manager.cpp
    src/rollback_manager.cpp
)

target_include_directories(network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Link dependencies
target_link_libraries(network PUBLIC 
    character
    spdlog::spdlog
    glm
    entt
)

# Enable C++17
target_compile_features(network PRIVATE cxx_std_17)
