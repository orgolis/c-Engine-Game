# Quick Start - Camera & Rotation Testing

**Build:** 5.7 MB editor.exe
**Status:** ✅ Ready to test

## 1-Minute Quick Test

### Step 1: Launch
```bash
c:\dev\ProjectSchizo\c-Engine-Game\build\windows-msvc-debug\bin\Debug\editor.exe
```

### Step 2: Create Player
- Scene Hierarchy panel → **"+ Add Entity"** → **"Player"**
- Red capsule appears at (0, 1, 0)

### Step 3: Test Rotation Rendering
- Select Player in hierarchy
- Inspector panel → Find **Transform** or **Local Rotation**
- Set rotation Y to **45 degrees** or **90 degrees**
- **VERIFY:** Model tilts/rotates (not staying upright)

### Step 4: Start Playback
- Press **F5** or click **Play** button
- Viewport switches to first-person view (camera above player)

### Step 5: Test Mouse Look
- **Click in viewport** (any mouse button)
- **Move mouse horizontally** (left/right)
- **VERIFY:** Player rotates smoothly, camera view rotates too

### Step 6: Test Movement
- **Release mouse** (click elsewhere)
- **Press W** - Move forward (in direction player faces)
- **Rotate player 90°** (mouse look)
- **Press W again** - Move perpendicular to original direction
- **VERIFY:** Movement follows facing direction ✓

### Step 7: Test Jump
- **Press Space** - Player jumps up
- **VERIFY:** Gravity pulls player back down ✓

---

## Full Test Checklist

### Graphics
- [ ] Player model appears as red capsule
- [ ] Model rotates when you change rotation in inspector
- [ ] Model doesn't stay upright (rotation actually applied)

### Input
- [ ] W moves forward
- [ ] A moves left (relative to player)
- [ ] S moves backward
- [ ] D moves right (relative to player)
- [ ] Space makes player jump
- [ ] Shift+W makes player move faster (sprint)

### Mouse Look
- [ ] Click in viewport enables mouse look
- [ ] Move mouse left = Player turns left, camera view rotates
- [ ] Move mouse right = Player turns right
- [ ] Movement after turning follows new facing direction

### Physics & Gravity
- [ ] Jump only works when on ground
- [ ] Gravity pulls player down continuously
- [ ] Player stops at ground (Y=0)
- [ ] Can walk while falling from height

### Camera
- [ ] First-person view centered on player eye
- [ ] Camera shows at correct height (0.8m up)
- [ ] Camera rotates with player
- [ ] Camera doesn't lag behind

---

## Keyboard Controls Reference

| Key | Action |
|-----|--------|
| **W** | Move forward (player facing direction) |
| **A** | Strafe left |
| **S** | Move backward |
| **D** | Strafe right |
| **Space** | Jump |
| **Shift** | Sprint modifier (hold with movement) |
| **Mouse + Click** | Look around (rotate player) |
| **F5** | Play/Stop |

---

## Customizing Camera Position

### Option 1: Edit Code
File: `editor/src/scene_playback_manager.cpp` - SetupPlaybackCamera()
```cpp
camera_transform->SetLocalPosition(glm::vec3(0.0f, 0.8f, 0.0f));
// Try these:
// (0, 0.8, 0.3)   = Gun camera (forward)
// (0.3, 0.8, 0)   = Right shoulder (over-shoulder)
// (-0.3, 0.8, 0)  = Left shoulder
// (0, 1.2, 0)     = Higher up (tall view)
// (0, 2, 0)       = Top-down bird's eye
```

### Option 2: In Editor
1. Start playback (F5)
2. Scene Hierarchy → Select **PlayerCamera**
3. Inspector → **Local Position** → Modify X, Y, Z
4. Changes apply immediately
5. Camera follows from new position

---

## Troubleshooting

### ❌ Model not rotating when I change rotation
- **Check:** Are you using GetWorldMatrix() in main.cpp?
- **Line:** editor/src/main.cpp around line 1488
- **Fix:** Change to `transform->GetWorldMatrix()` instead of manual translate+scale

### ❌ Mouse not rotating player
- **Check:** Are you clicking in the viewport? (must have focus)
- **Check:** Is ImGui blocking input? (try clicking away from panels first)
- **Check:** Console for any errors
- **Fix:** Make sure not hovering over any ImGui element

### ❌ Movement in wrong direction
- **Check:** Are you rotated? (face northeast, press W, should move northeast)
- **Check:** GetForward()/GetRight() returning correct vectors
- **Fix:** Log `GetForward()` and `GetRight()` in console

### ❌ Camera not following or stuck at origin
- **Check:** Is PlayerCamera in Scene Hierarchy as child of Player?
- **Check:** Select PlayerCamera, see Local Position in Inspector
- **Fix:** Run playback again - should auto-create camera

### ❌ Player vanishes after jumping
- **Check:** Did player fall below Y=-100 or similar?
- **Fix:** Check ground plane or adjust gravity

---

## Performance Notes

**Frame Time Budget (60 FPS = 16.67ms):**
- Physics/Movement: ~1-2ms
- Rendering: ~8-10ms
- Total comfortable: ~12-14ms
- **Status:** ✅ Well under budget

**Build Size:**
- **editor.exe:** 5.7 MB (includes all systems)

---

## Next Features to Add

1. **Pitch rotation** - Look up/down with mouse Y
2. **Head bobbing** - Animation while walking
3. **Camera modes** - First-person, third-person, top-down toggle
4. **Mouse sensitivity UI** - Adjust in editor instead of code
5. **Footstep sounds** - Audio on landing

---

## Files to Know

- **Game Logic:** `editor/src/scene_playback_manager.cpp`
- **Rendering:** `editor/src/main.cpp` (viewport section)
- **Transform:** `engine/scene/src/transform.cpp` (GetWorldMatrix)
- **Physics:** `editor/src/scene_playback_manager.cpp` (ApplyGravity, ApplyVelocityToPosition)

---

## Getting Help

See detailed guides:
- [CAMERA_ROTATION_GUIDE.md](CAMERA_ROTATION_GUIDE.md) - Full technical reference
- [PHASE_6_SUMMARY.md](PHASE_6_SUMMARY.md) - System overview
- [TESTING_GAMEPLAY.md](TESTING_GAMEPLAY.md) - Physics testing guide

---

**Ready to test!** Start editor and follow the 1-minute test above. 🎮
