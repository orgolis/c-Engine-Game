add_library(gws_file_io STATIC)

target_sources(gws_file_io PRIVATE
    file_io.cpp
)

target_include_directories(gws_file_io PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# C++20 standard
target_compile_features(gws_file_io PUBLIC cxx_std_20)

# Suppress warnings for this module
if(MSVC)
    target_compile_options(gws_file_io PRIVATE /W4)
else()
    target_compile_options(gws_file_io PRIVATE -Wall -Wextra)
endif()
