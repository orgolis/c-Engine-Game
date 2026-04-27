# Tests for GameWorldshaper Engine

# Test executable
add_executable(gws_tests
    main.cpp
    math/test_vectors.cpp
    math/test_matrices.cpp
    math/test_quaternions.cpp
    math/test_transforms.cpp
    memory/test_general_allocator.cpp
    memory/test_stack_allocator.cpp
    memory/test_pool_allocator.cpp
    test_character_integration.cpp
    test_ability_integration.cpp
    test_network_integration.cpp
    deferred_rendering_test.cpp
    imgui_integration_test.cpp
    # test_physics_integration.cpp  # TODO: Fix circular dependency with scene module
    # test_animation_integration.cpp  # TODO: Fix circular dependency with renderer
)

# C++20 standard
target_compile_features(gws_tests PRIVATE cxx_std_20)

# Link against engine (which includes all dependencies)
target_link_libraries(gws_tests PRIVATE
    engine
    character
    ability
    network
    renderer
    imgui
    Catch2::Catch2WithMain
)

# Include directories - expose all core headers
# The engine folder must be in the path to find "core/math.h"
target_include_directories(gws_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
)

# Register tests with CTest
include(CTest)
include(Catch)
catch_discover_tests(gws_tests)

# Physics demo executable (simple console test) - DISABLED since physics module is disabled
# add_executable(physics_demo
#     physics_demo.cpp
# )
# 
# target_compile_features(physics_demo PRIVATE cxx_std_20)
# 
# target_link_libraries(physics_demo PRIVATE
#     physics
#     math
# )
# 
# target_include_directories(physics_demo PRIVATE
#     ${CMAKE_SOURCE_DIR}/engine
# )

# ============================================================================
# Vulkan Window Test - Phase 1 Milestone
# ============================================================================
# Tests basic Vulkan initialization and window rendering
# Build: cmake --build . --target vulkan_window_test
# Run: ./bin/vulkan_window_test

add_executable(vulkan_window_test
    vulkan_window_test.cpp
)

target_compile_features(vulkan_window_test PRIVATE cxx_std_20)

target_link_libraries(vulkan_window_test PRIVATE
    window
    Vulkan::Vulkan
    glfw
)

target_include_directories(vulkan_window_test PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
)

# ============================================================================
# Deferred Smoke Test - Phase 4 Week 3 live-GPU integration
# ============================================================================
# Constructs G-Buffer + LightingPass + RenderGraph against a real
# VulkanDevice and submits a recorded command buffer to the graphics queue.
add_executable(deferred_smoke_test
    deferred_smoke_test.cpp
)

target_compile_features(deferred_smoke_test PRIVATE cxx_std_20)

target_link_libraries(deferred_smoke_test PRIVATE
    renderer
    Vulkan::Vulkan
)

target_include_directories(deferred_smoke_test PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
)

# ============================================================================
# Deferred Scene Test - Phase 5 Week 1 draw-list / Material / Mesh / glTF
# ============================================================================
add_executable(deferred_scene_test
    deferred_scene_test.cpp
)

target_compile_features(deferred_scene_test PRIVATE cxx_std_20)

target_link_libraries(deferred_scene_test PRIVATE
    renderer
    Vulkan::Vulkan
)

target_include_directories(deferred_scene_test PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
)

# ============================================================================
# Engine Window Test - Phase 4 Week 4 surface + swapchain
# ============================================================================
# Validates VulkanDevice::attach_surface + create_window_swapchain against
# a GLFW-created VkSurfaceKHR.
add_executable(engine_window_test
    engine_window_test.cpp
)

target_compile_features(engine_window_test PRIVATE cxx_std_20)

target_link_libraries(engine_window_test PRIVATE
    renderer
    Vulkan::Vulkan
    glfw
)

target_include_directories(engine_window_test PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
)

# ============================================================================
# Culling Test - Phase 5 Week 3 frustum culling
# ============================================================================
# Validates frustum extraction, AABB visibility tests, and draw-list filtering.
add_executable(culling_test
    culling_test.cpp
)

target_compile_features(culling_test PRIVATE cxx_std_20)

target_link_libraries(culling_test PRIVATE
    renderer
    Vulkan::Vulkan
)

target_include_directories(culling_test PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
)
