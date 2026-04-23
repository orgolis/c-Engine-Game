# glslang wrapper target
# Note: glslang is typically included with Vulkan SDK
# This target provides a clean interface for shader compilation

# Try to find glslang package (usually part of Vulkan SDK)
find_package(glslang QUIET)

if(glslang_FOUND)
    message(STATUS "✅ glslang found (Vulkan SDK)")
    add_library(glslang_wrapper INTERFACE)
    target_link_libraries(glslang_wrapper INTERFACE
        glslang
        SPIRV
        SPIR-Tools
    )
else()
    message(STATUS "⚠️  glslang not found in system")
    message(STATUS "   glslang will need to be provided via Vulkan SDK")
    
    # Create stub target
    add_library(glslang_wrapper INTERFACE)
    
    # If Vulkan SDK is installed, these should be in the SDK
    # Set environment variable VULKAN_SDK to enable auto-detection
    if(DEFINED ENV{VULKAN_SDK})
        set(VULKAN_SDK_PATH $ENV{VULKAN_SDK})
        target_include_directories(glslang_wrapper INTERFACE
            ${VULKAN_SDK_PATH}/include
        )
        target_link_directories(glslang_wrapper INTERFACE
            ${VULKAN_SDK_PATH}/lib
        )
        message(STATUS "   Using VULKAN_SDK: ${VULKAN_SDK_PATH}")
    endif()
endif()
