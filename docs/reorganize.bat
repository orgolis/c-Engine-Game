@echo off
REM Create target directories
mkdir archive 2>nul
mkdir systems 2>nul
mkdir references 2>nul
mkdir testing 2>nul
mkdir design 2>nul
mkdir legacy 2>nul

REM Move to archive/ (completed/old phases)
move "PHASE-2-PLAN.md" "archive\" 2>nul
move "PHASE-2-SETUP-COMPLETE.md" "archive\" 2>nul
move "PHASE-6-COMPLETE-SUMMARY.md" "archive\" 2>nul
move "PHASE-6-EDITOR-INTEGRATION.md" "archive\" 2>nul
move "PHASE_6_SUMMARY.md" "archive\" 2>nul
move "WEEK-5-6-COMPLETION.md" "archive\" 2>nul
move "ENGINE-ROADMAP.md" "archive\" 2>nul

REM Move to systems/ (game engine systems)
move "DEFERRED-RENDERING-SYSTEM.md" "systems\" 2>nul
move "LIGHTING-SYSTEM.md" "systems\" 2>nul
move "MATERIAL-AND-PBR-SYSTEM.md" "systems\" 2>nul
move "MESH-AND-GEOMETRY-SYSTEM.md" "systems\" 2>nul
move "POST-PROCESSING-SYSTEM.md" "systems\" 2>nul
move "SCENE-MANAGEMENT-SYSTEM.md" "systems\" 2>nul

REM Move to references/ (quick reference guides)
move "DEFERRED-RENDERING-QUICK-REFERENCE.md" "references\" 2>nul
move "GRAPHICS-QUICK-REFERENCE.md" "references\" 2>nul
move "LIGHTING-QUICK-REFERENCE.md" "references\" 2>nul
move "MATERIAL-QUICK-REFERENCE.md" "references\" 2>nul
move "MESH-QUICK-REFERENCE.md" "references\" 2>nul
move "POST-PROCESSING-QUICK-REFERENCE.md" "references\" 2>nul
move "SCENE-MANAGEMENT-QUICK-REFERENCE.md" "references\" 2>nul
move "WEEK-5-6-QUICK-REFERENCE.md" "references\" 2>nul

REM Move to testing/ (testing & debugging)
move "CAMERA_DEBUG_INSTRUCTIONS.md" "testing\" 2>nul
move "CAMERA_DIAGNOSTIC_GUIDE.md" "testing\" 2>nul
move "CAMERA_FOLLOWING_DEBUG.md" "testing\" 2>nul
move "CAMERA_ROTATION_GUIDE.md" "testing\" 2>nul
move "IN_GAME_CAMERA_TEST.md" "testing\" 2>nul
move "PHYSICS-TESTING.md" "testing\" 2>nul
move "QUICK_START_CAMERA_TEST.md" "testing\" 2>nul
move "TESTING_GAMEPLAY.md" "testing\" 2>nul

REM Move to design/
move "GAME-DESIGN.md" "design\" 2>nul

echo All files reorganized successfully!
pause
