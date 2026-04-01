add_library(gws_containers INTERFACE)

target_include_directories(gws_containers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Header-only library
target_compile_features(gws_containers INTERFACE cxx_std_20)
