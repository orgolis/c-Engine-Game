# New Dependencies & Library Integration Guide
> Complete manifest of Vulkan ecosystem libraries and installation instructions
> **Date:** April 20, 2026

---

## TABLE OF CONTENTS

1. [Overview](#overview)
2. [Core Vulkan Dependencies](#core-vulkan-dependencies)
3. [Platform-Specific Installation](#platform-specific-installation)
4. [CMake Integration](#cmake-integration)
5. [Dependency Verification](#dependency-verification)
6. [Optional Dependencies](#optional-dependencies)
7. [Build Configuration Reference](#build-configuration-reference)

---

## OVERVIEW

### Dependency Matrix

| Library | Purpose | License | Size | Installation |
|---------|---------|---------|------|--------------|
| **Vulkan SDK** | Graphics API | Custom | 600MB | Official installer |
| **volk** | Vulkan function loader | MIT | 50KB | Header-only git submodule |
| **vk-bootstrap** | Device initialization helper | MIT | 200KB | Header-only git submodule |
| **glslang** | GLSL → SPIR-V compiler | Apache 2.0 | 50MB | Included in Vulkan SDK |
| **shaderc** | Google shader compiler (optional) | Apache 2.0 | 100MB | Optional install |
| **VMA** | Vulkan Memory Allocator | MIT | 100KB | Header-only, bundled |
| **NVIDIA NRD** | Ray tracing denoiser (optional) | Apache 2.0 | 200MB | Optional for RTX |

### Current Dependencies to KEEP

| Library | Status | Reason |
|---------|--------|--------|
| **EnTT** | ✅ Keep | ECS independent of graphics API |
| **GLM** | ✅ Keep | Math library, graphics-independent |
| **spdlog** | ✅ Keep | Logging, graphics-independent |
| **Catch2** | ✅ Keep | Testing framework, graphics-independent |
| **tinygltf** | ✅ Keep | Asset loading, graphics-independent |
| **ImGui** | ✅ Keep | UI framework (needs Vulkan backend) |
| **GLFW** | ✅ Keep | Window management (adapt for Vulkan surface) |

### Current Dependencies to REMOVE/REPLACE

| Library | Status | Reason |
|---------|--------|--------|
| **GLAD** | ❌ Remove | OpenGL loader, replaced by Vulkan |
| **GLFW** | ⚠️ Adapt | Windowing still useful, graphics init replaced |

---

## CORE VULKAN DEPENDENCIES

### 1. Vulkan SDK (REQUIRED)

**What:** Official Vulkan SDK from Khronos Group, includes:
- Vulkan headers (`vulkan.h`, `vulkan_core.h`)
- Vulkan loader (`vulkan-1.dll` on Windows, `libvulkan.so` on Linux)
- Vulkan Validation Layers
- glslang shader compiler
- spirv-tools
- Sample code & documentation

**Versions Supported:**
- Windows: Vulkan SDK 1.3.280+ (2024)
- Linux: Vulkan SDK 1.3.280+
- macOS: Vulkan SDK (beta, via MoltenVK)

**Installation:**

#### Windows (MSVC)

```
1. Download: https://www.lunarg.com/vulkan-sdk/
2. Download: VulkanSDK-1.3.280.0-Installer.exe (or latest)
3. Run installer:
   ✓ Destination: C:\VulkanSDK\1.3.280
   ✓ Check: Vulkan Runtime
   ✓ Check: glslang Compiler
   ✓ Check: SPIR-V tools
   ✓ Check: Vulkan Runtime Development Files
4. Environment variable: VULKAN_SDK=C:\VulkanSDK\1.3.280
   (Installer sets this automatically)
5. Add to PATH: %VULKAN_SDK%\bin
6. Verify:
   vulkaninfo.exe
   glslangValidator.exe --version
```

**PowerShell Setup:**
```powershell
# Test Vulkan installation
$env:VULKAN_SDK = "C:\VulkanSDK\1.3.280"
$env:Path += ";$env:VULKAN_SDK\bin"

# Verify
vulkaninfo.exe | head -20
glslangValidator.exe --version
```

#### Linux (GCC/Clang)

```bash
# Ubuntu 22.04 / Debian 12
sudo apt-get update
sudo apt-get install -y vulkan-tools libvulkan-dev glslang-tools spirv-tools

# Alternative: Compile from source
# https://github.com/KhronosGroup/Vulkan-LoaderAndValidationLayers
cd /opt
wget https://github.com/KhronosGroup/Vulkan-Headers/archive/main.zip
unzip main.zip
cd Vulkan-Headers-main && mkdir build && cd build
cmake .. && make && sudo make install

# Set environment
export VULKAN_SDK=/usr/local
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# Verify
vulkaninfo | head -20
glslangValidator --version
```

---

### 2. volk (OPTIONAL but RECOMMENDED)

**What:** Vulkan function loader (modern replacement for official loader)

**Advantages:**
- Faster load time
- Smaller binary
- No runtime .dll/.so dependency
- Direct symbol loading via `vkGetInstanceProcAddr()`

**Download:** https://github.com/zeux/volk

**Integration:**

```bash
# Add as git submodule
cd c:\dev\ProjectSchizo - Copy\c-Engine-Game
git submodule add https://github.com/zeux/volk.git third_party/volk

# Or download directly
# https://github.com/zeux/volk/releases
# Extract to: third_party/volk/
```

**CMakeLists.txt Integration:**
```cmake
# Option A: Use volk (recommended)
set(VOLK_STATIC_DEFINES ON)
add_subdirectory(third_party/volk)
target_link_libraries(renderer volk)
target_compile_definitions(renderer PRIVATE VK_NO_PROTOTYPES)

# Option B: Use official Vulkan loader (default)
# find_package(Vulkan REQUIRED)
# target_link_libraries(renderer Vulkan::Vulkan)
```

---

### 3. vk-bootstrap (RECOMMENDED)

**What:** Helper library for Vulkan device initialization, significantly reduces boilerplate

**Advantages:**
- Automatic GPU selection (discrete > integrated)
- Queue family detection
- Surface support checking
- Error handling with descriptive messages

**Download:** https://github.com/charles-lunarg/vk-bootstrap

**Integration:**

```bash
# Add as submodule
git submodule add https://github.com/charles-lunarg/vk-bootstrap.git third_party/vk-bootstrap
```

**CMakeLists.txt Integration:**
```cmake
add_subdirectory(third_party/vk-bootstrap)
target_link_libraries(renderer vk-bootstrap)
```

**Usage Example:**
```cpp
// engine/renderer/gpu/vulkan/vulkan_device.cpp
#include <VkBootstrap.h>

vkb::InstanceBuilder instance_builder;
auto inst_ret = instance_builder
    .set_app_name("GameWorldshaper")
    .set_engine_name("GWS")
    .request_validation_layers(true)
    .build();

if (!inst_ret) {
    GWS_LOG_ERROR("Failed to create Vulkan instance: {}", inst_ret.error().message());
}
```

---

### 4. glslang (REQUIRED, included in Vulkan SDK)

**What:** Official GLSL → SPIR-V compiler

**Already Included:** Vulkan SDK installs glslang at:
- Windows: `%VULKAN_SDK%\bin\glslangValidator.exe`
- Linux: `/usr/bin/glslangValidator`

**Build-Time Integration:**

```cmake
# CMakeLists.txt
find_package(glslang REQUIRED CONFIG)
find_package(SPIRV REQUIRED)

# For offline shader compilation
add_executable(shader_compiler engine/renderer/tools/shader_compiler.cpp)
target_link_libraries(shader_compiler PRIVATE glslang SPIRV spirv-reflect)
```

**Runtime Shader Compilation (optional):**

```cpp
// engine/renderer/gpu/shader_compiler.cpp
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

std::vector<uint32_t> CompileGLSLToSpirv(const std::string& glsl_code) {
    glslang::InitializeProcess();
    
    EShLanguage stage = EShLangFragment;
    glslang::TShader shader(stage);
    shader.setStrings(&glsl_code);
    shader.parse(...);
    
    glslang::TProgram program;
    program.addShader(&shader);
    program.link(...);
    
    std::vector<uint32_t> spirv;
    glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);
    
    glslang::FinalizeProcess();
    return spirv;
}
```

---

### 5. VMA - Vulkan Memory Allocator (REQUIRED, Header-only)

**What:** AMD's memory allocator for Vulkan, vastly simplifies GPU memory management

**Download:** https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator

**Integration:**

```bash
# Add as submodule
git submodule add https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git \
    third_party/VulkanMemoryAllocator

# Or download just the header
wget https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/master/include/vk_mem_alloc.h
mv vk_mem_alloc.h third_party/VMA/include/
```

**Usage Example:**
```cpp
// engine/renderer/gpu/vulkan/vulkan_memory.h
#include <vk_mem_alloc.h>

class VulkanMemoryAllocator {
public:
    void Initialize(VkDevice device, VkPhysicalDevice physical_device) {
        VmaAllocatorCreateInfo allocator_info{};
        allocator_info.device = device;
        allocator_info.physicalDevice = physical_device;
        allocator_info.instance = instance;
        vmaCreateAllocator(&allocator_info, &allocator_);
    }
    
    VkBuffer AllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage) {
        VkBufferCreateInfo buffer_info{};
        buffer_info.size = size;
        buffer_info.usage = usage;
        
        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        
        VkBuffer buffer;
        vmaCreateBuffer(allocator_, &buffer_info, &alloc_info, &buffer, nullptr, nullptr);
        return buffer;
    }

private:
    VmaAllocator allocator_;
};
```

---

## PLATFORM-SPECIFIC INSTALLATION

### Windows (MSVC 2022, C++20)

#### Step 1: Install Vulkan SDK
```bash
# 1. Download installer from https://www.lunarg.com/vulkan-sdk/
# 2. Run: VulkanSDK-1.3.280.0-Installer.exe
# 3. Accept default installation path: C:\VulkanSDK\1.3.280
# 4. Select all components
# 5. System will set VULKAN_SDK environment variable automatically
```

#### Step 2: Verify Installation
```powershell
# PowerShell
$env:VULKAN_SDK
# Output: C:\VulkanSDK\1.3.280

vulkaninfo | head -30
# Output: Vulkan version 1.3.280, available devices, etc.

glslangValidator --version
# Output: glslang version 14.0.0, SPIR-V 1.6, etc.
```

#### Step 3: Clone Submodules
```bash
cd c:\dev\ProjectSchizo - Copy\c-Engine-Game

# Add helper libraries
git submodule add https://github.com/zeux/volk.git third_party/volk
git submodule add https://github.com/charles-lunarg/vk-bootstrap.git third_party/vk-bootstrap

# Pull them
git submodule update --init --recursive
```

#### Step 4: Configure CMake
```bash
cd c:\dev\ProjectSchizo - Copy\c-Engine-Game
mkdir build
cd build

cmake .. `
    -G "Visual Studio 17 2022" `
    -DCMAKE_BUILD_TYPE=Debug `
    -DCMAKE_PREFIX_PATH="C:\VulkanSDK\1.3.280" `
    -DGWS_GRAPHICS_API="vulkan"
```

#### Step 5: Build
```bash
cmake --build . --config Debug -- /j 8
# /j 8 = parallel build with 8 threads
```

---

### Linux (GCC 11+, Ubuntu 22.04 / Debian 12)

#### Step 1: Install System Packages
```bash
sudo apt-get update
sudo apt-get install -y \
    vulkan-tools \
    libvulkan-dev \
    glslang-tools \
    spirv-tools \
    libspirv-cross-dev \
    libshaderc-dev \
    cmake \
    build-essential \
    git

# Verify
vulkaninfo | head -10
glslangValidator --version
```

#### Step 2: Clone Submodules
```bash
cd /path/to/ProjectSchizo/c-Engine-Game
git submodule add https://github.com/zeux/volk.git third_party/volk
git submodule add https://github.com/charles-lunarg/vk-bootstrap.git third_party/vk-bootstrap
git submodule update --init --recursive
```

#### Step 3: Configure CMake
```bash
mkdir build && cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=g++ \
    -DGWS_GRAPHICS_API=vulkan \
    -DCMAKE_PREFIX_PATH=/usr/local
```

#### Step 4: Build
```bash
cmake --build . -j $(nproc)
# $(nproc) = number of CPU cores
```

---

### macOS (M1/M2/M3, with MoltenVK - OPTIONAL)

**Note:** Vulkan on macOS requires MoltenVK (commercial/open-source layer over Metal).

```bash
# Install via Homebrew
brew install vulkan-headers vulkan-loader molten-vk glslang

# Alternative: Download MoltenVK manually
# https://github.com/KhronosGroup/MoltenVK/releases

# Then follow Linux build steps above
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DGWS_GRAPHICS_API=vulkan
cmake --build . -j $(sysctl -n hw.ncpu)
```

---

## CMAKE INTEGRATION

### New CMakeLists.txt Configuration

Replace the graphics API section in `engine/renderer/CMakeLists.txt`:

```cmake
# ============================================================================
# Graphics API Selection (Vulkan or OpenGL)
# ============================================================================

option(GWS_GRAPHICS_API "Target graphics API: vulkan or opengl" "vulkan")

if(GWS_GRAPHICS_API STREQUAL "vulkan")
    message(STATUS "Building with Vulkan renderer")
    
    # Find Vulkan SDK
    find_package(Vulkan REQUIRED)
    
    # volk (optional, modern loader)
    option(USE_VOLK "Use volk instead of official loader" ON)
    if(USE_VOLK)
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/volk)
        list(APPEND RENDERER_LIBS volk)
        target_compile_definitions(renderer PRIVATE VK_NO_PROTOTYPES)
    else()
        list(APPEND RENDERER_LIBS Vulkan::Vulkan)
    endif()
    
    # vk-bootstrap
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/vk-bootstrap)
    list(APPEND RENDERER_LIBS vk-bootstrap)
    
    # Find glslang
    find_package(glslang REQUIRED CONFIG)
    list(APPEND RENDERER_LIBS glslang SPIRV)
    
    # Enable Vulkan-specific features
    target_compile_definitions(renderer PRIVATE 
        GWS_USE_VULKAN=1
        VK_ENABLE_BETA_EXTENSIONS=1
    )
    
    # Source files
    target_sources(renderer PRIVATE
        gpu/vulkan/vulkan_device.cpp
        gpu/vulkan/vulkan_swapchain.cpp
        gpu/vulkan/vulkan_command_buffer.cpp
        gpu/vulkan/vulkan_descriptor.cpp
        gpu/vulkan/vulkan_buffer.cpp
        gpu/vulkan/vulkan_image.cpp
        gpu/vulkan/vulkan_shader.cpp
        gpu/vulkan/vulkan_pipeline.cpp
        gpu/vulkan/vulkan_render_pass.cpp
        gpu/vulkan/vulkan_barrier.cpp
    )
    
    target_include_directories(renderer PRIVATE
        ${Vulkan_INCLUDE_DIRS}
        gpu/
    )

elseif(GWS_GRAPHICS_API STREQUAL "opengl")
    message(STATUS "Building with OpenGL renderer (legacy)")
    
    # OpenGL (deprecated path)
    find_package(OpenGL REQUIRED)
    find_package(glad REQUIRED CONFIG)
    
    list(APPEND RENDERER_LIBS OpenGL::GL glad::glad)
    target_compile_definitions(renderer PRIVATE GWS_USE_OPENGL=1)
    
    target_sources(renderer PRIVATE
        gpu/opengl/opengl_device.cpp
        gpu/opengl/opengl_shader.cpp
        gpu/opengl/opengl_texture.cpp
    )

else()
    message(FATAL_ERROR "Invalid GWS_GRAPHICS_API: ${GWS_GRAPHICS_API}")
endif()

# Link libraries
target_link_libraries(renderer PRIVATE ${RENDERER_LIBS})
```

---

## DEPENDENCY VERIFICATION

### Build-Time Verification Script

Create `scripts/verify_dependencies.cmake`:

```cmake
# Verify Vulkan installation
function(verify_vulkan_installation)
    if(NOT VULKAN_SDK)
        message(WARNING "VULKAN_SDK environment variable not set")
        if(WIN32)
            message("  Set via: set VULKAN_SDK=C:\\VulkanSDK\\1.3.280")
        else()
            message("  Set via: export VULKAN_SDK=/usr/local/vulkan")
        endif()
    else()
        message(STATUS "VULKAN_SDK: $ENV{VULKAN_SDK}")
    endif()
    
    # Check for glslang
    find_program(GLSLANG_VALIDATOR glslangValidator)
    if(GLSLANG_VALIDATOR)
        message(STATUS "glslangValidator found: ${GLSLANG_VALIDATOR}")
    else()
        message(FATAL_ERROR "glslangValidator not found. Install Vulkan SDK.")
    endif()
    
    # Check for vulkaninfo
    find_program(VULKANINFO vulkaninfo)
    if(VULKANINFO)
        message(STATUS "vulkaninfo found: ${VULKANINFO}")
    else()
        message(WARNING "vulkaninfo not found (non-critical)")
    endif()
endfunction()

verify_vulkan_installation()
```

### Runtime Verification

Add to `engine/renderer/gpu/vulkan_device.cpp`:

```cpp
void VulkanDevice::LogDeviceInfo() {
    GWS_LOG_INFO("Vulkan Device Information:");
    GWS_LOG_INFO("  API Version: {}.{}.{}",
        VK_VERSION_MAJOR(vk_version_),
        VK_VERSION_MINOR(vk_version_),
        VK_VERSION_PATCH(vk_version_));
    GWS_LOG_INFO("  Driver Version: {}", vk_driver_version_);
    GWS_LOG_INFO("  Vendor ID: {}", gpu_properties_.vendorID);
    GWS_LOG_INFO("  Device: {}", gpu_properties_.deviceName);
    GWS_LOG_INFO("  Device Type: {}", 
        gpu_properties_.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? "Discrete GPU" :
        gpu_properties_.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? "Integrated GPU" :
        "Other");
    GWS_LOG_INFO("  Supported Extensions: {}", supported_extensions_.size());
    
    for (const auto& ext : supported_extensions_) {
        GWS_LOG_DEBUG("    - {}", ext.extensionName);
    }
}
```

---

## OPTIONAL DEPENDENCIES

### A. shaderc (Google's Alternative Shader Compiler - OPTIONAL)

**When to Use:** If glslang proves insufficient or you need additional features

**Installation:**
```bash
# Windows
choco install shaderc  # Requires Chocolatey

# Linux
sudo apt-get install libshaderc-dev libshaderc1

# Source build
git clone https://github.com/google/shaderc.git
cd shaderc && mkdir build && cd build
cmake .. && make && sudo make install
```

**CMakeLists.txt:**
```cmake
option(USE_SHADERC "Use Google shaderc instead of glslang" OFF)
if(USE_SHADERC)
    find_package(shaderc REQUIRED)
    list(APPEND RENDERER_LIBS shaderc)
    target_compile_definitions(renderer PRIVATE GWS_USE_SHADERC=1)
endif()
```

---

### B. NVIDIA NRD (Ray Tracing Denoiser - OPTIONAL for RTX)

**When to Use:** If implementing ray-traced lighting/reflections

**Installation:**
```bash
git clone https://github.com/NVIDIAGameWorks/RayTracingDenoiser.git third_party/NRD
cd third_party/NRD && mkdir build && cd build
cmake .. && make
```

**CMakeLists.txt:**
```cmake
option(GWS_ENABLE_RAY_TRACING "Enable ray tracing (requires NVIDIA GPU)" OFF)
if(GWS_ENABLE_RAY_TRACING)
    add_subdirectory(third_party/NRD)
    list(APPEND RENDERER_LIBS nrd)
    target_compile_definitions(renderer PRIVATE GWS_USE_RAY_TRACING=1)
endif()
```

---

### C. AMD GPU Debug Tools (OPTIONAL)

**Installation:**
```bash
# Download from: https://gpuopen.com/tools/
# - Radeon GPU Profiler (RGP)
# - Radeon GPU Detective (RGD)
# - Radeon Developer Panel
```

---

## BUILD CONFIGURATION REFERENCE

### Quick-Start Build Commands

#### Windows (MSVC)
```bash
cd c:\dev\ProjectSchizo - Copy\c-Engine-Game
mkdir build && cd build

# Debug with Vulkan
cmake .. -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Debug -DGWS_GRAPHICS_API=vulkan
cmake --build . --config Debug

# Release with Vulkan
cmake .. -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release -DGWS_GRAPHICS_API=vulkan
cmake --build . --config Release
```

#### Linux
```bash
cd /path/to/c-Engine-Game
mkdir build && cd build

# Debug
cmake .. -DCMAKE_BUILD_TYPE=Debug -DGWS_GRAPHICS_API=vulkan
cmake --build . -j $(nproc)

# Release
cmake .. -DCMAKE_BUILD_TYPE=Release -DGWS_GRAPHICS_API=vulkan
cmake --build . -j $(nproc)
```

---

### Environment Variables

| Variable | Value | Purpose |
|----------|-------|---------|
| `VULKAN_SDK` | Path to SDK | Vulkan headers & libraries location |
| `VK_LAYER_PATH` | SDK/Bin | Validation layers location |
| `VK_ICD_FILENAMES` | icd JSON path | Vulkan ICD (optional) |

---

## DEPENDENCY SUMMARY TABLE

| Phase | Dependency | Type | Required | Installation |
|-------|-----------|------|----------|--------------|
| **Foundation** | Vulkan SDK | System | ✅ Yes | Official installer |
| **Foundation** | CMake 3.20+ | System | ✅ Yes | `apt-get` / `brew` |
| **Phase 1** | volk | Submodule | ⚠️ Optional | Git submodule |
| **Phase 1** | vk-bootstrap | Submodule | ⚠️ Optional | Git submodule |
| **Phase 2** | glslang | System | ✅ Yes | Included in SDK |
| **Phase 3** | VMA | Submodule | ✅ Yes | Header-only |
| **Phase 5** | NVIDIA NRD | Submodule | ⚠️ Optional | For RTX |
| **All** | spdlog | Existing | ✅ Keep | Already installed |
| **All** | EnTT | Existing | ✅ Keep | Already installed |
| **All** | ImGui | Existing | ✅ Keep | Already installed |

---

## TROUBLESHOOTING

### Issue: "vulkaninfo: command not found"
**Solution:**
```bash
# Windows: Add to PATH
set PATH=%PATH%;C:\VulkanSDK\1.3.280\bin

# Linux: Install properly
sudo apt-get install vulkan-tools

# Or rebuild from source
```

### Issue: "Cannot find Vulkan"
**Solution:**
```cmake
# In CMakeLists.txt, set prefix path explicitly:
set(CMAKE_PREFIX_PATH "C:\\VulkanSDK\\1.3.280" ${CMAKE_PREFIX_PATH})
find_package(Vulkan REQUIRED)
```

### Issue: "glslang not found"
**Solution:**
```bash
# Windows: Verify installation
dir "C:\VulkanSDK\1.3.280\bin\glslangValidator.exe"

# Linux: Install again
sudo apt-get install --reinstall glslang-tools

# Or add to PATH
export PATH=/usr/local/bin:$PATH
```

### Issue: "vk_mem_alloc.h: No such file"
**Solution:**
```bash
# Ensure VMA submodule is initialized
git submodule update --init --recursive

# Verify path
ls -la third_party/VulkanMemoryAllocator/include/
```

---

**Document Version:** 1.0  
**Last Updated:** April 20, 2026  
**Status:** Ready for Implementation

