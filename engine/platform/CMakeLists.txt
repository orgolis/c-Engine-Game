add_library(gws_platform STATIC)

# Portable: resolves both OSes' rules from the environment, no OS headers.
target_sources(gws_platform PRIVATE src/user_dirs.cpp)

target_include_directories(gws_platform
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    target_sources(gws_platform PRIVATE src/file_dialog_windows.cpp)
    target_link_libraries(gws_platform PRIVATE shell32 ole32)
elseif(UNIX)
    target_sources(gws_platform PRIVATE src/file_dialog_linux.cpp)
else()
    message(FATAL_ERROR "gws_platform: unsupported platform")
endif()

target_compile_features(gws_platform PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(gws_platform PRIVATE /W4)
else()
    target_compile_options(gws_platform PRIVATE -Wall -Wextra -Wpedantic)
endif()
