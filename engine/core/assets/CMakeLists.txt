cmake_minimum_required(VERSION 3.20)

# Asset management system
add_library(gws_assets STATIC
    asset_manager.cpp
)

target_include_directories(gws_assets PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/engine/core/logging/include
    ${CMAKE_SOURCE_DIR}/third_party/glm
    ${CMAKE_SOURCE_DIR}/third_party/spdlog/include
)

target_link_libraries(gws_assets 
    PUBLIC gws_logging tinygltf
    PRIVATE spdlog::spdlog
)

# Enable C++17 for std::filesystem
target_compile_features(gws_assets PRIVATE cxx_std_17)
