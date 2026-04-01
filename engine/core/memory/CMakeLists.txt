add_library(memory INTERFACE)

target_include_directories(memory
    INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(memory INTERFACE cxx_std_20)
