cmake_minimum_required(VERSION 3.20)

# Lobby codes and the relay (lobby_protocol.h): the server and the game's
# side, on ENet's portable sockets only -- so tools/lobby_server is a small
# program that builds for a Linux cloud machine as it does here.
add_library(gws_lobby STATIC
    src/lobby_server.cpp
    src/lobby_client.cpp
    src/lobby_direct.cpp     # codes without a server: STUN, the DHT, the local network, hole punching
)
target_include_directories(gws_lobby PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(gws_lobby PRIVATE ${CMAKE_SOURCE_DIR}/third_party/enet/include)
target_link_libraries(gws_lobby PRIVATE enet)
target_compile_features(gws_lobby PRIVATE cxx_std_17)

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
# ENet stays an implementation detail (enet_transport.cpp and gws_lobby include it).
# ENet uses old-style include_directories(), so add its headers explicitly.
target_link_libraries(network PRIVATE enet)
target_link_libraries(network PUBLIC gws_lobby)
target_include_directories(network PRIVATE ${CMAKE_SOURCE_DIR}/third_party/enet/include)

# Enable C++17
target_compile_features(network PRIVATE cxx_std_17)
