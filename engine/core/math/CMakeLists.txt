cmake_minimum_required(VERSION 3.20)

add_library(math STATIC
    math.cpp
)

target_include_directories(math
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Enable C++20
target_compile_features(math PUBLIC cxx_std_20)

# Set as interface library for header-only parts
# Note: Most vector operations are inline, only matrix/quaternion ops are compiled
