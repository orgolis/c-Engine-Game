# Phase 6 Gameplay Testing Guide

## Build Status
- **Editor.exe**: 5.7 MB (windows-msvc-debug/bin/Debug/editor.exe)
- **Compilation**: ✅ No errors
- **Physics System**: ✅ Refactored and compiled

## Quick Start

### 1. Start the Editor
```bash
c:\dev\ProjectSchizo\c-Engine-Game\build\windows-msvc-debug\bin\Debug\editor.exe
```

### 2. Create a Test Scene
- Editor auto-creates empty "Untitled" scene on startup
- Right-click in **Scene Hierarchy** panel → "+ Add Entity" → "Player"
- This creates a red capsule entity named "Player" at position (0, 1, 0)

### 3. Test Playback
- Press **F5** to start scene playback
  - Viewport should switch to player camera view
  - Player entity should be under playback control

### 4. Test Gameplay

| Input | Expected Behavior | Status |
|-------|-------------------|--------|
| **W** | Move forward | To test |
| **S** | Move backward | To test |
| **A** | Strafe left | To test |
| **D** | Strafe right | To test |
| **Space** | Jump (only when on ground) | To test |
| **Shift + WASD** | Sprint (12 m/s vs 7 m/s) | To test |

### 5. Verify Physics

Check in real-time:
1. **Gravity**: Player should fall when in air
2. **Ground Detection**: Player should stop falling at Y=0
3. **Jump**: Space should only work when `is_on_ground=true`
4. **Horizontal Movement**: WASD movement should work while falling

### 6. Verify Camera

- Camera should follow player in first-person view
- Camera offset: 0.8m above player (eye level)
- Viewport should show player camera perspective

## Physics Implementation Details

### Velocity System
```cpp
player_velocity_ = glm::vec3(x, y, z)
  x = horizontal movement from input (±7 m/s or ±12 m/s sprint)
  y = vertical velocity from gravity
  z = horizontal movement from input (±7 m/s or ±12 m/s sprint)
```

### Update Order (Each Frame)
1. **ApplyGravity()** → `velocity.y -= 9.8 * dt`
2. **UpdateCharacterMovement()** → `velocity.x/z = input * speed`
3. **ApplyVelocityToPosition()** → `position += velocity * dt`
4. **UpdateCamera()** → Camera follows player

### Ground Detection
```cpp
if (position.y <= 0.0f) {
    position.y = 0.0f;
    velocity.y = 0.0f;
    is_on_ground = true;
}
```

## Testing Notes

### Common Commands
- **F5** = Start/Stop playback
- **Esc** = Exit playback (if implemented)
- **WASD** = Movement
- **Space** = Jump
- **Shift** = Sprint modifier

### Expected Console Output
When running with debug logging:
```
[editor] Player entity found: Player
[editor] Created new camera child: PlayerCamera
[editor] Player move: forward=1.00, lateral=0.00, sprint=false, on_ground=true
[editor] Gravity applied: velocity.y = -9.80
[editor] Position updated: (0.00, 5.00, 0.00), on_ground=false
```

### Scene Hierarchy
After creation with Player:
```
Scene: Untitled
├── Player (red capsule)
│   └── PlayerCamera (follows player at eye height)
```

## Debug Tips

If physics don't work:
1. **Check console logs** for "Player entity found"
2. **Verify entity naming** - must be exactly "Player"
3. **Check player position** - should start at Y=1.0
4. **Verify UpdateCharacterMovement** - check WASD input is being read
5. **Check ApplyVelocityToPosition** - verify position updates each frame

If camera doesn't follow:
1. **Check playback camera** - should be named "PlayerCamera"
2. **Verify parent-child relationship** - camera should be child of player
3. **Check local position** - should be (0, 0.8, 0)
4. **Verify transform hierarchy** - parent updates should propagate

## Troubleshooting

### Player doesn't move
- [ ] Check entity is named exactly "Player"
- [ ] Check WASD keys are registered in ImGui
- [ ] Check player_velocity_ is being updated
- [ ] Check ApplyVelocityToPosition is called after input

### Player falls through ground
- [ ] Check GROUND_LEVEL constant = 0.0f
- [ ] Check ground collision detection in ApplyVelocityToPosition
- [ ] Verify position.y <= 0.0f check

### Camera doesn't follow
- [ ] Check playback_camera_ is set in SetupPlaybackCamera()
- [ ] Check camera is child of player entity
- [ ] Check camera local position is (0, 0.8, 0)
- [ ] Verify transform hierarchy updates

### Viewport stays in editor view
- [ ] Check IsPlaying() returns true during playback
- [ ] Check GetPlaybackCamera() returns valid entity
- [ ] Check viewport camera integration in main.cpp ~1415

## Next Steps

After testing basic gameplay:
1. Add player model/mesh replacement
2. Implement camera rotation (mouse look)
3. Add footstep sounds
4. Add particle effects for jumping
5. Implement stamina/sprint drain
6. Add animations for movement/jumping
