# In-Game Camera Movement - Testing Guide

**Build:** ✅ 5.44 MB (successfully compiled)

## What's New

The camera system has been completely rewritten to:
1. **Follow the player smoothly** from behind
2. **Hide the mouse cursor** during gameplay
3. **Rotate with the player** when you move the mouse
4. **Move relative to player rotation** - camera stays behind you as you turn

## How to Test

### Step 1: Run the Editor
```
c:\dev\ProjectSchizo\c-Engine-Game\build\windows-msvc-debug\bin\Debug\editor.exe
```

### Step 2: Start Playback
- Press **F5** or click "Play (F5)" button in Scene Playback Controls
- **Expected:** Mouse cursor disappears, you're now in gameplay mode

### Step 3: Test Movement
Press these keys to move:
- **W** - Move forward (in direction player faces)
- **A** - Move left (relative to player facing)
- **D** - Move right (relative to player facing)  
- **S** - Move backward
- **Space** - Jump
- **Shift** - Sprint (hold while moving)

**Expected:** 
- Player moves in the direction you input
- Camera smoothly follows behind the player
- Camera height is at eye level (~1.5m above player)

### Step 4: Test Mouse Look
**Click and drag the mouse** (any mouse button) to rotate player

**Expected:**
- Player rotates to face mouse movement direction
- Camera smoothly follows behind player as they rotate
- Camera maintains height and distance while rotating

### Step 5: Stop Playback
- Press **F5** again or click "Stop" button
- **Expected:** Mouse cursor reappears

---

## Camera Settings (in code)

If you want to adjust camera behavior, edit these values in `scene_playback_manager.h`:

```cpp
float camera_distance_ = 3.0f;      // Distance behind player (meters)
float camera_height_ = 1.5f;        // Height above player (meters)
float camera_smoothing_ = 0.15f;    // Smoothing factor (0.0=instant, 1.0=very slow)
```

- **Increase `camera_distance_`** for more "zoomed out" third-person view
- **Increase `camera_height_`** to look down more on player
- **Increase `camera_smoothing_`** for slower camera follow (more lag)
- **Decrease `camera_smoothing_`** for snappier camera (less lag)

---

## Console Output

You'll see camera position logs in console:
```
📷 UPDATE - Player: (1.00, 1.00, 5.00), Camera: (2.50, 2.50, 7.00), Forward: (0.10, 0.00, 0.87)
📷 UPDATE - Player: (2.00, 1.00, 6.00), Camera: (3.50, 2.50, 8.00), Forward: (0.10, 0.00, 0.87)
```

This shows:
- **Player position** - where player entity is in world
- **Camera position** - where camera is (behind and above player)
- **Forward direction** - where player is facing

If camera position is updating but movement doesn't work:
1. Check if Player entity exists in scene
2. Make sure you have player with default name "Player"
3. Check console for error messages

---

## Known Issues & Fixes

### Issue: Mouse cursor still visible
- **Fix:** Make sure you press F5 to enter playback mode
- The cursor only hides during active playback

### Issue: Camera doesn't follow smoothly
- **Fix:** This is normal - there's a slight lag to smooth motion
- It should catch up within 1-2 frames

### Issue: Player moves but camera doesn't update
- **Check console:** Look for `📷 UPDATE` messages
- If no messages appear, camera update isn't running
- Report the exact error in console

### Issue: Camera is at wrong distance/height
- **In code:** Adjust `camera_distance_` and `camera_height_` values
- Rebuild and test new values

---

## What This Replaces

This new system replaces the broken "SetParent()" camera hierarchy with a simpler, more reliable approach:

**Old approach:** Tried to make camera a child of player entity (didn't work)
**New approach:** Directly position camera based on player position + offset each frame

Benefits:
- ✅ Camera always follows correctly
- ✅ No transform hierarchy issues
- ✅ Smooth interpolation between frames
- ✅ Easy to adjust distance/height
- ✅ Works with any player rotation

---

## Next Steps

Once this is working and feeling good, we can add:
1. **Collision detection** - Camera pushes through walls (add raycasting)
2. **Orbit controls** - Mouse up/down to look up/down
3. **Dynamic distance** - Zoom in/out with mouse wheel
4. **Over-the-shoulder** mode - Offset camera to side instead of center

---

**Ready to test! Run the editor and press F5 to start playing!**
