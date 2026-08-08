# gws_diagnostics — crash handler + persistent/ring-buffer logging.
if(NOT TARGET spdlog::spdlog)
    message(FATAL_ERROR "spdlog target not found (add it in the root CMakeLists).")
endif()

add_library(gws_diagnostics STATIC)

target_sources(gws_diagnostics PRIVATE
    crash_handler.cpp
)

target_include_directories(gws_diagnostics PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(gws_diagnostics PUBLIC
    spdlog::spdlog
)

# Windows: DbgHelp for StackWalk64 / SymFromAddr / MiniDumpWriteDump.
if(WIN32)
    target_link_libraries(gws_diagnostics PUBLIC dbghelp)
endif()

target_compile_features(gws_diagnostics PUBLIC cxx_std_20)
