# GameWorldshaper Physics Engine - Testing Guide

## Quick Start

### Prerequisites
- Windows 10/11 with Visual Studio 2022 (Community Edition works)
- CMake 3.24+
- Git

### Step 1: Build the Engine

```powershell
cd c:\dev\ProjectSchizo\c-Engine-Game

# Configure CMake
cmake --preset windows-msvc-debug

# Build all targets (including physics_demo)
cmake --build build\windows-msvc-debug --config Debug
```

**Expected output:**
```
[100%] Built target physics_demo
[100%] Built target gws_tests
[100%] Built target game
```

### Step 2: Run Physics Demo (Recommended)

The physics demo is a **standalone console test** that doesn't require graphics. Perfect for validating the physics engine:

```powershell
cd c:\dev\ProjectSchizo\c-Engine-Game
.\build\windows-msvc-debug\bin\Debug\physics_demo.exe
```

**What you'll see:**
```
=== GameWorldshaper Physics Engine Test ===

[1/5] Creating physics world...
  ✓ Physics world created
  ✓ Gravity: -9.81 m/s²

[2/5] Creating rigid bodies...
  ✓ Ground plane created (static, mass=0)
  ✓ Sphere 1 created (mass=1.0, at height=10)
  ✓ Sphere 2 created (mass=2.0, at height=15, force applied)
  ✓ Box created (mass=3.0, at height=8)

[3/5] Running physics simulation...
  Simulating 5 seconds with 60 Hz fixed timestep

Frame | Time(s) | Sphere1 Y | Sphere2 Y | Box Y | Collisions
------|---------|-----------|-----------|-------|------------
    30 |    0.50 |      7.76 |     12.28 |  5.32 |          0
    60 |    1.00 |      5.52 |      9.21 |  3.52 |          0
    90 |    1.50 |      4.21 |      7.45 |  1.92 |          0
   120 |    2.00 |     -8.91 |      5.89 | -8.91 |          2
   150 |    2.50 |     -8.91 |      4.21 | -8.91 |          0
   ...
```

**Interpretation:**
- Y positions decreasing = objects falling ✓
- Spheres reach ground (Y ≈ -9) = collision working ✓
- Collisions > 0 = collision detection active ✓
- Objects sleeping after settling = optimization working ✓

---

## Testing Scenarios

### Scenario 1: Physics Demo (RECOMMENDED - **No Graphics Needed**)

**What it tests:**
- ✓ Gravity application
- ✓ Force application
- ✓ Collision detection
- ✓ Restitution (bouncing)
- ✓ Sleeping system
- ✓ Multiple body types

**Run:**
```powershell
.\build\windows-msvc-debug\bin\Debug\physics_demo.exe
```

**Expected outcome:**
- Objects fall smoothly
- Collisions detected with ground
- Some bouncing occurs
- Objects eventually sleep

---

### Scenario 2: Unit Tests

Run comprehensive unit tests for math and memory systems:

```powershell
.\build\windows-msvc-debug\bin\Debug\gws_tests.exe
```

**Expected output:**
```
===============================================================================
All tests passed (XX assertions in XX test cases)
```

---

### Scenario 3: Build & Verify (Validation Only)

Just verify the physics module compiles without running:

```powershell
cd c:\dev\ProjectSchizo\c-Engine-Game

# Build physics library only
cmake --build build\windows-msvc-debug --target physics --config Debug
```

**Success indicators:**
- `physics.lib` created in `build\windows-msvc-debug\lib\Debug\`
- No compilation errors
- Build completes in < 30 seconds

---

## What Each Component Does

### Physics Demo (`tests/physics_demo.cpp`)

**Setup:**
1. Creates `DefaultPhysicsWorld` with gravity
2. Creates 4 bodies:
   - **Ground plane** (static, infinite mass) - won't move
   - **Sphere 1** (dynamic, mass=1kg) - falls from 10m
   - **Sphere 2** (mass=2kg) - falls from 15m with leftward force
   - **Box** (mass=3kg) - falls from 8m

**Simulation:**
- Runs 5 seconds of physics at 60 Hz (300 frames)
- Applies gravity (-9.81 m/s²)
- Detects collisions continuously
- Reports statistics every 0.5 seconds

**Output validation:**
```
Objects start at Y > 0 
  ↓
Fall due to gravity (Y decreases each frame)
  ↓
Collide with ground around Y ≈ -9
  ↓
Bounce slightly (restitution)
  ↓
Settle and sleep (Y stabilized, Sleeping=YES)
```

---

## Troubleshooting

### Problem: `physics_demo.exe not found`

**Solution:** Clean rebuild required
```powershell
cd c:\dev\ProjectSchizo\c-Engine-Game
rm -r build\windows-msvc-debug
cmake --preset windows-msvc-debug
cmake --build build\windows-msvc-debug --config Debug
```

---

### Problem: `Cannot open include file: 'glm/glm.hpp'`

**Solution:** This is a CMake configuration issue, not a code issue. Try:
```powershell
cmake --preset windows-msvc-debug --fresh
cmake --build build\windows-msvc-debug --config Debug
```

---

### Problem: Physics objects don't fall / no gravity observed

**Solution:** Check physics_world->SetGravity() was called
- Verify gravity value is non-zero: `y = -9.81f`
- Check bodies aren't marked as static (mass = 0)

---

## Next Steps for Visual Testing

Once physics_demo works, next goals:

**Easy** (console-based):
1. Create physics constraint test (physics_demo_constraints.cpp)
2. Add raycasting test (physics_demo_raycast.cpp)
3. Test sleeping/wake system behavior

**Medium** (requires renderer):
1. Integrate physics into game/ application  
2. Render falling spheres as actual geometry
3. Interactive mouse-forces demo

**Advanced** (production features):
1. Physics-based ragdoll system
2. Cloth simulation
3. Particle physics

---

## Expected Build Times

| Operation | Time | Notes |
|-----------|------|-------|
| Fresh CMake config | 5-10s | Includes third-party libs |
| Physics module rebuild | 2-5s | 4 .cpp files |
| Full engine build | 30-60s | All modules |
| Physics demo link | 1-2s | Minimal dependencies |

---

## File Structure Reference

```
c-Engine-Game/
├── build/
│   └── windows-msvc-debug/
│       ├── bin/Debug/
│       │   ├── physics_demo.exe  ← Run this!
│       │   └── gws_tests.exe
│       └── lib/Debug/
│           └── physics.lib       ← Physics library
├── engine/
│   ├── core/physics/            ← Core physics implementation
│   │   ├── collision.h/cpp
│   │   ├── rigidbody.h/cpp
│   │   ├── physics_world.h/cpp
│   │   └── constraints.h/cpp
│   └── renderer/                ← Scene integration
│       ├── physics_component.h/cpp
│       └── physics_system.h/cpp
└── tests/
    ├── physics_demo.cpp         ← Physics demo (START HERE!)
    ├── main.cpp                 ← Unit tests
    └── CMakeLists.txt           ← Build configuration
```

---

## Success Criteria

Your physics engine is working correctly when:

✅ **physics_demo.exe runs without crashes**
✅ **Objects fall from their starting heights**
✅ **Collision messages appear in output**
✅ **Objects settle to rest (Y position stabilizes)**
✅ **Sleeping system activates (Sleeping=YES in final state)**
✅ **No access violation errors**

---

## Quick Commands Reference

```powershell
# Configure + Build
cmake --preset windows-msvc-debug
cmake --build build\windows-msvc-debug --config Debug

# Run physics demo
.\build\windows-msvc-debug\bin\Debug\physics_demo.exe

# Run unit tests
.\build\windows-msvc-debug\bin\Debug\gws_tests.exe

# Rebuild just physics
cmake --build build\windows-msvc-debug --target physics --config Debug

# Clean rebuild (if stuck)
rm -r build\windows-msvc-debug
cmake --preset windows-msvc-debug
cmake --build build\windows-msvc-debug --config Debug
```

---

**Ready to test?** Run physics_demo.exe and watch the physics simulation in the console! 🚀
