add_library(gws_serialization INTERFACE)

target_include_directories(gws_serialization
    INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Reflection-driven: needs the core reflection headers.
target_link_libraries(gws_serialization INTERFACE gws_reflection)

target_compile_features(gws_serialization INTERFACE cxx_std_20)
