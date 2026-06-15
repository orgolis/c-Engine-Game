add_library(gws_ecs INTERFACE)

target_include_directories(gws_ecs
    INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# EnTT registry + reflected POD components + job-parallel iteration.
# gws_serialization: world snapshot save/load (ecs/snapshot.h, Stage 0.5 step 5).
target_link_libraries(gws_ecs INTERFACE
    entt
    glm
    gws_reflection
    gws_serialization
    gws_jobs
)

target_compile_features(gws_ecs INTERFACE cxx_std_20)
