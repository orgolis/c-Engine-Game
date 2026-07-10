cmake_minimum_required(VERSION 3.20)

# Network System Module
add_library(network
    src/deterministic_simulation.cpp
    src/input_synchronizer.cpp
    src/network_manager.cpp
    src/rollback_manager.cpp
    src/enet_transport.cpp   # Stage 7 — reliable-UDP transport (ENet)
)

target_include_directories(network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Link dependencies
target_link_libraries(network PUBLIC
    character
    spdlog::spdlog
    glm
    entt
)
# ENet stays an implementation detail (only enet_transport.cpp includes it).
# ENet uses old-style include_directories(), so add its headers explicitly.
target_link_libraries(network PRIVATE enet)
target_include_directories(network PRIVATE ${CMAKE_SOURCE_DIR}/third_party/enet/include)

# Enable C++17
target_compile_features(network PRIVATE cxx_std_17)
