add_library(gws_profiler INTERFACE)

target_include_directories(gws_profiler
    INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(gws_profiler INTERFACE cxx_std_20)
