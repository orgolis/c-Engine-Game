add_library(gws_reflection INTERFACE)

target_include_directories(gws_reflection
    INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(gws_reflection INTERFACE cxx_std_20)
