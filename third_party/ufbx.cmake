# ufbx — single-file FBX loader (MIT). Feeds the asset cooker's FBX importer.
#
# Upstream ships NO CMakeLists.txt, so this build glue is ours. It deliberately
# lives OUTSIDE third_party/ufbx/: that directory is a git submodule, and a file
# we author inside it is untracked there, so it would be missing from every
# fresh clone — which is exactly the bug that made clean clones fail to
# configure at `add_subdirectory(third_party/ufbx)`.

set(UFBX_DIR ${CMAKE_CURRENT_LIST_DIR}/ufbx)

if(NOT EXISTS ${UFBX_DIR}/ufbx.c)
    message(FATAL_ERROR
        "third_party/ufbx is empty — the submodule was not initialised.\n"
        "Run: git submodule update --init --recursive")
endif()

add_library(ufbx STATIC ${UFBX_DIR}/ufbx.c)
target_include_directories(ufbx PUBLIC ${UFBX_DIR})

# Third-party: silence its warnings (this project builds with -Wall -Wextra
# -Wpedantic, which ufbx's large C file would trip).
if(MSVC)
    target_compile_options(ufbx PRIVATE /w)
else()
    target_compile_options(ufbx PRIVATE -w)
endif()

# ufbx uses some threading/atomics on POSIX; link pthreads where present.
find_package(Threads QUIET)
if(Threads_FOUND)
    target_link_libraries(ufbx PUBLIC Threads::Threads)
endif()
