# glslang wrapper target
# Note: glslang is typically included with Vulkan SDK
# This target provides a clean interface for shader compilation

# Try to find glslang package (usually part of Vulkan SDK)
find_package(glslang QUIET)

if(glslang_FOUND)
    message(STATUS "glslang found (Vulkan SDK)")
    add_library(glslang_wrapper INTERFACE)
    target_link_libraries(glslang_wrapper INTERFACE
        glslang
        SPIRV
        SPIRV-Tools
    )
elseif(MSVC AND DEFINED ENV{VULKAN_SDK})
    # MSVC only: link against the Vulkan SDK's pre-built MSVC static libs.
    # GCC/MinGW cannot link MSVC-ABI .lib files, so skip on those toolchains.
    set(VULKAN_SDK_PATH $ENV{VULKAN_SDK})
    message(STATUS "glslang: using VULKAN_SDK MSVC libs at ${VULKAN_SDK_PATH}")

    add_library(glslang_wrapper INTERFACE)
    target_include_directories(glslang_wrapper INTERFACE
        ${VULKAN_SDK_PATH}/Include
    )
    target_link_directories(glslang_wrapper INTERFACE
        ${VULKAN_SDK_PATH}/Lib
    )
    target_link_libraries(glslang_wrapper INTERFACE
        $<$<CONFIG:Debug>:glslangd>
        $<$<NOT:$<CONFIG:Debug>>:glslang>
        $<$<CONFIG:Debug>:glslang-default-resource-limitsd>
        $<$<NOT:$<CONFIG:Debug>>:glslang-default-resource-limits>
        $<$<CONFIG:Debug>:MachineIndependentd>
        $<$<NOT:$<CONFIG:Debug>>:MachineIndependent>
        $<$<CONFIG:Debug>:OSDependentd>
        $<$<NOT:$<CONFIG:Debug>>:OSDependent>
        $<$<CONFIG:Debug>:GenericCodeGend>
        $<$<NOT:$<CONFIG:Debug>>:GenericCodeGen>
        $<$<CONFIG:Debug>:SPIRVd>
        $<$<NOT:$<CONFIG:Debug>>:SPIRV>
        $<$<CONFIG:Debug>:SPIRV-Toolsd>
        $<$<NOT:$<CONFIG:Debug>>:SPIRV-Tools>
        $<$<CONFIG:Debug>:SPIRV-Tools-optd>
        $<$<NOT:$<CONFIG:Debug>>:SPIRV-Tools-opt>
    )
else()
    # GCC/MinGW or no Vulkan SDK: empty stub.
    # shader_compiler.cpp detects _MSC_VER and compiles a no-op stub automatically.
    message(STATUS "glslang: no compatible libs found — runtime GLSL compilation disabled")
    add_library(glslang_wrapper INTERFACE)
endif()
