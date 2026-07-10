# miniaudio — single-header audio playback + decoding (Stage 6 audio backend).
# The big implementation is compiled ONCE here into a static lib; consumers
# include the header for declarations only and link `miniaudio`.

add_library(miniaudio STATIC miniaudio_impl.c)
target_include_directories(miniaudio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(miniaudio PROPERTIES C_STANDARD 11)

# OS audio backends are pulled in at runtime; these are the link-time deps.
if(WIN32)
    # WASAPI/DirectSound/WinMM + COM (ole32) + GUIDs (uuid).
    target_link_libraries(miniaudio PUBLIC ole32 winmm uuid)
else()
    target_link_libraries(miniaudio PUBLIC pthread m ${CMAKE_DL_LIBS})
endif()

# It's third-party C — silence its internal warnings so they don't drown ours.
if(MSVC)
    target_compile_options(miniaudio PRIVATE /w)
else()
    target_compile_options(miniaudio PRIVATE -w)
endif()
