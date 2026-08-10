add_library(gws_jobs STATIC)

target_sources(gws_jobs PRIVATE
    job_system.cpp
    task_runner.cpp
)

target_include_directories(gws_jobs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Work-stealing thread pool needs the system threading library, and reads
# memory/alloc_worker.h to set each worker's per-worker frame-allocator slot.
find_package(Threads REQUIRED)
target_link_libraries(gws_jobs PUBLIC Threads::Threads memory)

target_compile_features(gws_jobs PUBLIC cxx_std_20)
