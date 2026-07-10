cmake_minimum_required(VERSION 3.20)

# gws_audio — Stage 6 audio: device-agnostic Mixer + miniaudio-backed engine.
# Headers are included as "audio/<x>.h" (the include root is engine/core, like
# the other gws_* core libs). miniaudio is a PRIVATE impl detail (only
# audio_engine.cpp includes it) so it never leaks into consumers.
add_library(gws_audio STATIC
    mixer.cpp
    audio_engine.cpp)

target_include_directories(gws_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gws_audio
    PUBLIC  glm
    PRIVATE miniaudio spdlog::spdlog)
target_compile_features(gws_audio PUBLIC cxx_std_17)
