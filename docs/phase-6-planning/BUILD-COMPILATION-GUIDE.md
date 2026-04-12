# Build, Compilation & CMake Guide — Phase 6 Setup

**Purpose:** Step-by-step guide for setting up builds and handling compilation issues

---

## INITIAL SETUP: Adding Phase 6 Systems to Build

### Step 1: Create Directory Structure

```
c-Engine-Game/
├── engine/
│   ├── core/
│   │   ├── character/              ← NEW
│   │   │   ├── CMakeLists.txt
│   │   │   ├── include/
│   │   │   │   ├── character_controller.h
│   │   │   │   ├── character_state.h
│   │   │   │   ├── input_buffer.h
│   │   │   │   └── character_stats.h
│   │   │   └── src/
│   │   │       ├── character_controller.cpp
│   │   │       ├── character_state.cpp
│   │   │       └── input_buffer.cpp
│   │   ├── ability/                ← NEW
│   │   │   ├── CMakeLists.txt
│   │   │   ├── include/
│   │   │   │   ├── ability_system.h
│   │   │   │   ├── ability.h
│   │   │   │   ├── effect.h
│   │   │   │   ├── modifier_stack.h
│   │   │   │   └── cooldown_manager.h
│   │   │   └── src/
│   │   ├── network/                ← NEW (Week 9)
│   │   │   ├── CMakeLists.txt
│   │   │   ├── include/
│   │   │   └── src/
│   │   └── ai/                     ← NEW (Week 11)
│   │       ├── CMakeLists.txt
│   │       ├── include/
│   │       └── src/
```

### Step 2: Create Character Module CMakeLists.txt

```cmake
# engine/core/character/CMakeLists.txt

project(engine_character)

set(HEADERS
    include/character_controller.h
    include/character_state.h
    include/input_buffer.h
    include/character_stats.h
)

set(SOURCES
    src/character_controller.cpp
    src/character_state.cpp
    src/input_buffer.cpp
)

add_library(${PROJECT_NAME} STATIC ${HEADERS} ${SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PRIVATE src)

# Dependencies
target_link_libraries(${PROJECT_NAME} 
    PUBLIC 
        engine_core_math
        engine_core_memory
        entt
        glm
        spdlog
    PRIVATE
        engine_core_physics        # For raycasting
        engine_animation          # For animator access
)

# Enable C++20
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)

# Warnings
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()
```

### Step 3: Update Main engine/CMakeLists.txt

```cmake
# engine/CMakeLists.txt

# Add before existing add_subdirectory() calls
add_subdirectory(core/math)
add_subdirectory(core/memory)
add_subdirectory(core/character)   # ← ADD THIS
add_subdirectory(core/ability)     # ← ADD THIS (Week 5)
# add_subdirectory(core/network)   # ← Uncomment Week 9
# add_subdirectory(core/ai)        # ← Uncomment Week 11

# Update engine_SRC to include all character/ability files
set(ENGINE_SRCS
    ${ENGINE_SRCS}
    # Other files...
)

# Engine library links
add_library(engine SHARED ${ENGINE_SRCS})
target_link_libraries(engine 
    PUBLIC
        engine_core_math
        engine_core_memory
        engine_character        # ← ADD
        engine_ability         # ← ADD (Week 5)
        # engine_network       # ← Uncomment Week 9
        # engine_ai            # ← Uncomment Week 11
)
```

---

## COMMON BUILD ERRORS & FIXES

### Error 1: "fatal error C1083: Cannot open include file: 'character_controller.h'"

**Cause:** Include directory not in CMakeLists.txt

**Fix:**
```cmake
# In engine/core/character/CMakeLists.txt
target_include_directories(${PROJECT_NAME} PUBLIC include)
                                         ^^^^^^ — Must be PUBLIC
```

---

### Error 2: "error LNK2019: unresolved external symbol ... InputBuffer::BufferInput"

**Cause:** Function defined in .h but not in .cpp

**Fix:**

```cpp
// character_controller.h
class InputBuffer {
public:
    void BufferInput(const InputAction& action);  // Declaration
};

// character_controller.cpp
void InputBuffer::BufferInput(const InputAction& action) {
    // Implementation
}
```

**OR** header-only template (if needed):

```cpp
// character_controller.h
template <size_t N>
class InputBuffer {
public:
    void BufferInput(const InputAction& action) {  // Inline
        buffer_[write_pos_] = action;
        write_pos_ = (write_pos_ + 1) % N;
    }
};
```

---

### Error 3: "circular dependency detected: character → physics → animation"

**Cause:** Headers including each other recursively

**Fix:** Use forward declarations and virtual interfaces

```cpp
// character_controller.h
#pragma once

// Forward declarations (no include needed)
class PhysicsBody;
class AnimationComponent;

class CharacterController {
    PhysicsBody* physics_body_;        // ✓ OK with forward decl
    AnimationComponent* animator_;     // ✓ OK with forward decl
    
    void Update(float dt);  // Can use them in function bodies
};

// character_controller.cpp
#include "character_controller.h"
#include "physics_body.h"      // ← Include in .cpp only
#include "animation_component.h"

void CharacterController::Update(float dt) {
    auto velocity = physics_body_->GetVelocity();  // ✓ Now OK
}
```

---

### Error 4: "error: 'glm::vec3' does not name a type"

**Cause:** Missing #include <glm/glm.hpp>

**Fix:**
```cpp
// In character_controller.h
#pragma once

#include <glm/glm.hpp>      // ← Add this
#include <entt/entt.hpp>

class CharacterController {
    glm::vec3 position_;    // ✓ Now OK
};
```

---

### Error 5: "LNK1169: one or more multiply defined symbols found"

**Cause:** Same function defined in two .cpp files or in header without `inline`

**Fix:**

```cpp
// ❌ BAD: Defined in header and .cpp
// character_controller.h
void UpdateAnimation() {
    // Implementation
}

// ✓ GOOD: Only in .cpp or marked inline
// character_controller.h
void UpdateAnimation();  // Declaration only

// character_controller.cpp
void CharacterController::UpdateAnimation() {
    // Implementation
}
```

**OR** for header-only:
```cpp
// character_controller.h
inline void UpdateAnimation() {  // ← Add inline
    // Implementation
}
```

---

### Error 6: "entt/entity/registry.hpp:1: No such file or directory"

**Cause:** EnTT not in include paths

**Fix:**
```cmake
# In CMakeLists.txt
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/entt/single_include
)

# Verify EnTT exists:
```

**Verify:**
```powershell
# In PowerShell terminal
Get-ChildItem c:\dev\ProjectSchizo\c-Engine-Game\third_party\entt\
```

---

### Error 7: "LINK : fatal error LNK1104: cannot open file '...\engine_character.lib'"

**Cause:** Library target not built yet

**Fix:**
```cmake
# In top-level CMakeLists.txt, ensure order:
add_subdirectory(engine/core/math)      # 1. Math first
add_subdirectory(engine/core/memory)    # 2. Memory second
add_subdirectory(engine/core/character) # 3. Character third
add_subdirectory(engine)                # 4. Main engine last
                                           (needs libs above)
```

---

### Error 8: "IntelliSense: No member named 'GetVelocity' in 'PhysicsBody'"

**Cause:** Include path correct but IntelliSense confused

**Fix:**
```cpp
// Restart C++ IntelliSense
// VS Code: Ctrl+Shift+P → "C++: Rescan Solutions"
// or
// Visual Studio: Project → Rescan Solutions
```

---

## BUILD COMMANDS

### Clean Build From Scratch

```powershell
# Navigate to project
cd c:\dev\ProjectSchizo\c-Engine-Game

# Remove old build (DANGEROUS - removes everything)
Remove-Item -Path build -Recurse -Force

# Create fresh build
mkdir build
cd build

# Configure
cmake -G "Visual Studio 17 2022" ..

# Build Debug
cmake --build . --config Debug --parallel 8

# or Release
cmake --build . --config Release --parallel 8
```

### Incremental Build (Normal Development)

```powershell
cd c:\dev\ProjectSchizo\c-Engine-Game\build

# Just build changes
cmake --build . --config Debug

# With more detail
cmake --build . --config Debug --verbose
```

### Build Single Module

```powershell
# Build just character module
cmake --build . --target engine_character --config Debug

# or
cmake --build . --target engine_ability --config Debug
```

---

## CMAKE PRESETS (Recommended)

Create or update `CMakePresets.json`:

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "default",
      "generator": "Visual Studio 17 2022",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug"
      },
      "binaryDir": "${sourceDir}/build/debug"
    },
    {
      "name": "release",
      "generator": "Visual Studio 17 2022",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release"
      },
      "binaryDir": "${sourceDir}/build/release"
    }
  ],
  "buildPresets": [
    {
      "name": "debug-build",
      "configurePreset": "default",
      "jobs": 8
    },
    {
      "name": "release-build",
      "configurePreset": "release",
      "jobs": 8
    }
  ]
}
```

Use from CLI:
```powershell
cmake --preset default
cmake --build --preset debug-build
```

Or from VS Code:
```
Ctrl+Shift+P → "CMake: Select Configure Preset" → "default"
Ctrl+Shift+P → "CMake: Build"
```

---

## DEPENDENCY GRAPH FOR LINKING

```
Character Module:
├── Needs: engine_core_math (for vec3, etc)
├── Needs: engine_core_memory (for allocations)
├── Needs: engine_animation (for Animator interface)
├── Needs: engine_physics (for raycasting)
└── Needs: entt, glm, spdlog (third party)

Ability Module:
├── Needs: engine_core_math
├── Needs: engine_character (for CharacterStats)
├── Needs: entt
└── Needs: spdlog

Network Module (Week 9):
├── Needs: engine_core_math
├── Needs: engine_character (syncs character state)
├── Needs: engine_physics (deterministic simulation)
├── Needs: asio (external, need to add: vcpkg install asio)
└── Needs: spdlog

AI Module (Week 11):
├── Needs: engine_core_math
├── Needs: engine_character (uses character controller)
├── Needs: engine_scene (for pathfinding queries)
└── Needs: spdlog
```

---

## COMPILER WARNINGS TO WATCH

### Treat These as Errors (Add to CMakeLists.txt)

```cmake
if(MSVC)
    target_compile_options(engine_character PRIVATE
        /W4              # Warning level 4
        /WX              # Warnings as errors
        /permissive-     # Strict C++ conformance
    )
else()
    target_compile_options(engine_character PRIVATE
        -Wall -Wextra -Wpedantic
        -Werror          # Warnings as errors
        -Wconversion     # Implicit conversions
    )
endif()
```

### Common Warnings

| Warning | Fix |
|---------|-----|
| `unused variable 'x'` | Remove or use `[[maybe_unused]]` |
| `narrowing conversion from 'float' to 'int'` | Use explicit cast: `static_cast<int>(value)` |
| `missing return statement` | All code paths must return |
| `unreachable code` | Check logic flow |

---

## PCH (Pre-Compiled Headers) For Speed

Add to `engine/core/character/CMakeLists.txt`:

```cmake
# Create PCH
target_precompile_headers(engine_character PRIVATE
    <vector>
    <string>
    <glm/glm.hpp>
    <entt/entt.hpp>
    <spdlog/spdlog.h>
)

# Reduces compile time significantly
```

---

## DEBUGGING BUILD ISSUES

### Generate Verbose Log

```powershell
cd c:\dev\ProjectSchizo\c-Engine-Game\build

# Full output
cmake --build . --config Debug --verbose 2>&1 | Tee-Object -FilePath build_log_verbose.txt

# Save for analysis
cat build_log_verbose.txt | head -100  # First 100 errors
```

### Check What CMake Found

```powershell
# Run cmake with debug output
cmake .. -DCMAKE_MESSAGE_LOG_LEVEL=VERBOSE
```

### Validate Dependencies Exist

```powershell
# Check library paths
ls c:\dev\ProjectSchizo\c-Engine-Game\build\Debug\lib\

# Check include paths
ls c:\dev\ProjectSchizo\c-Engine-Game\third_party\entt\
```

---

## CONTINUOUS INTEGRATION (For Team)

### GitHub Actions Example (if using GitHub)

Create `.github/workflows/build.yml`:

```yaml
name: Build Phase 6

on: [push, pull_request]

jobs:
  build:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v2
      
      - name: Configure
        run: |
          mkdir build
          cd build
          cmake -G "Visual Studio 17 2022" ..
      
      - name: Build
        run: |
          cd build
          cmake --build . --config Debug --parallel 8
      
      - name: Run Tests
        run: |
          cd build
          ctest --output-on-failure
      
      - name: Upload Logs
        if: failure()
        uses: actions/upload-artifact@v2
        with:
          name: build-logs
          path: build/CMakeFiles/CMakeOutput.log
```

---

## LINKING ORDER MATTERS

If getting linker errors, try this order in your main CMakeLists.txt:

```cmake
# ✓ CORRECT ORDER
add_subdirectory(engine/core/memory)        # First
add_subdirectory(engine/core/math)          # Second
add_subdirectory(engine/renderer)           # Third (no deps on char/ability)
add_subdirectory(engine/scene)              # Fourth
add_subdirectory(engine/animation)          # Fifth
add_subdirectory(engine/physics)            # Sixth
add_subdirectory(engine/window)             # Seventh
add_subdirectory(engine/core/character)     # Eighth (depends on above)
add_subdirectory(engine/core/ability)       # Ninth (depends on char)
add_subdirectory(engine/core/network)       # Tenth (Week 9, depends on all)
add_subdirectory(engine/core/ai)            # Eleventh (Week 11, depends on all)
add_subdirectory(engine)                    # Last (ties it together)
add_subdirectory(game)
add_subdirectory(editor)
add_subdirectory(tests)
```

---

## TESTING BUILD

Add this to `tests/CMakeLists.txt`:

```cmake
# Compile tests for each module
add_executable(test_character
    character/test_input_buffer.cpp
    character/test_character_controller.cpp
    character/test_states.cpp
)
target_link_libraries(test_character 
    engine_character 
    catch2
    spdlog
)

add_executable(test_ability
    ability/test_ability_system.cpp
    ability/test_modifier_stack.cpp
)
target_link_libraries(test_ability 
    engine_ability 
    engine_character
    catch2
)

# Run all tests
add_custom_target(run_tests
    COMMAND ctest --output-on-failure
    DEPENDS test_character test_ability
)
```

Build and run:
```powershell
cmake --build . --target run_tests --config Debug
```

---

