# Camera Debug - FIXED Logging

The logging was NOT appearing because it was set to `debug()` level, but the logger only shows `info()` level and above.

**FIXED:** All logs now use `info()` level and will appear in the console.

## What to Do

1. **Run the editor:**
   ```
   c:\dev\ProjectSchizo\c-Engine-Game\build\windows-msvc-debug\bin\Debug\editor.exe
   ```

2. **Make sure console is visible** (should appear automatically in VS Code integrated terminal or external console)

3. **In the editor:**
   - Scene Hierarchy → "+ Add Entity" → "Player"
   - Or just press F5 if you have an existing scene

4. **Look for these emoji markers in console:**
   - `✅` = Success messages (what worked)
   - `❌` = Error messages (what failed)
   - `📷` = Camera positions
   - `🔄` = Player rotation/movement

## What You'll See at Startup

```
✅ PlayerCamera entity created
✅ SetParent(player_entity_) called - camera should now follow player
✅ Camera local position set to (0.00, 0.80, 0.00)
📷 SETUP - Camera world pos: (0.00, 1.80, 0.00)
📷 SETUP - Player world pos: (0.00, 1.00, 0.00)
📷 SETUP - Offset from player: (0.00, 0.80, 0.00)
✅ playback_camera_ assigned successfully
```

## What You'll See Each Frame During Playback

```
📷 UPDATE - Player: (1.00, 1.00, 5.00) | Camera local: (0.00, 0.80, 0.00) | Camera world: (1.00, 1.80, 5.00)
📷 UPDATE - Player: (2.00, 1.00, 6.00) | Camera local: (0.00, 0.80, 0.00) | Camera world: (2.00, 1.80, 6.00)
📷 UPDATE - Player: (3.00, 1.00, 7.00) | Camera local: (0.00, 0.80, 0.00) | Camera world: (3.00, 1.80, 7.00)
```

## CRITICAL: What Should Happen vs What's Broken

**IF WORKING:**
- Player world pos increases as you move
- Camera world pos increases BY THE SAME AMOUNT
- Offset stays constant at (0.00, 0.80, 0.00)

**IF BROKEN (What You Reported):**
- Player world pos increases
- Camera world pos STAYS AT (0.00, 2.00, 5.00) or similar
- Camera doesn't follow

## Copy-Paste Instructions for User

**PLEASE DO THIS AND REPORT EXACTLY WHAT YOU SEE:**

1. Run editor.exe
2. Look in console for any `❌` errors
3. If you see errors, paste them here
4. If you see `✅ PlayerCamera entity created`, then in console look for the **first UPDATE line** and paste exactly what it says (the line starting with `📷 UPDATE`)
5. Move your player forward (WASD) and paste the **next 3 UPDATE lines** from console

This will tell us EXACTLY what's wrong.

## Possible Issues We're Checking For

1. **Camera not created** → Would see `❌ Failed to create PlayerCamera entity`
2. **SetParent not called** → Would see no `✅ SetParent` message
3. **Camera world pos locked** → Would see same `Camera world` numbers each frame
4. **Camera hierarchy broken** → Transform hierarchy code has a bug
5. **Viewport not using camera** → Would see `❌ UpdateCamera: playback_camera_=false`

**The console output will tell us exactly which one it is.**
