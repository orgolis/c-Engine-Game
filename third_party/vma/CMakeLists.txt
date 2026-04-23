# Vulkan Memory Allocator (VMA) - Header-only library
add_library(vma INTERFACE)

# VMA requires Vulkan
find_package(Vulkan REQUIRED)

target_link_libraries(vma INTERFACE
    Vulkan::Vulkan
)

target_include_directories(vma INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# VMA uses 64-bit handles and dynamic dispatch
target_compile_definitions(vma INTERFACE
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1
    VMA_STATIC_VULKAN_FUNCTIONS=0
)

message(STATUS "✅ Vulkan Memory Allocator (VMA) configured as header-only library")
