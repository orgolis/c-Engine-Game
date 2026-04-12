cmake_minimum_required(VERSION 3.20)

# Ability System Module
add_library(ability
    src/ability.cpp
    src/effect.cpp
    src/modifier.cpp
    src/cooldown_manager.cpp
    src/ability_system.cpp
    src/skill_tree.cpp
)

target_include_directories(ability PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Link dependencies
target_link_libraries(ability PUBLIC 
    spdlog::spdlog
    glm
    entt
)

# Enable C++17
target_compile_features(ability PRIVATE cxx_std_17)
