# File Cleanup & Removal Guide
> Detailed action items for removing redundant, backup, and stub files
> **Date:** April 20, 2026

---

## BEFORE YOU START

⚠️ **CRITICAL: Git Commands to Execute**

Before deleting files, ensure they're committed to Git history (they are recoverable):

```bash
cd c:\dev\ProjectSchizo - Copy\c-Engine-Game
git status                    # Verify repository clean
git log --oneline -n 5        # Check recent commits
```

---

## SECTION 1: BACKUP FILES TO DELETE

### Priority: DELETE IMMEDIATELY
These files are backups (.restored extension) with no other purpose:

```bash
# Windows PowerShell:
Remove-Item -Path "engine/renderer/src/opengl/opengl_device.cpp.restored" -Force
Remove-Item -Path "editor/src/simple_renderer.cpp.restored" -Force
Remove-Item -Path "engine/renderer/src/*.restored" -Force

# Or via Git (cleaner):
git rm engine/renderer/src/opengl/opengl_device.cpp.restored
git rm editor/src/simple_renderer.cpp.restored
git rm "engine/renderer/src/*.restored"
```

**Files to Delete:**
- ❌ `engine/renderer/src/opengl/opengl_device.cpp.restored`
- ❌ `editor/src/simple_renderer.cpp.restored`
- ❌ Any `*.restored` files in the project

**Reason:** Backup files serve no purpose; version control provides history.

---

## SECTION 2: STUB FILES TO DELETE

### Priority: DELETE AFTER REVIEW

#### 2a. Deferred Renderer Stub
**File:** `engine/renderer/src/deferred_renderer_stub.cpp`

**Action:**
1. Review file to confirm it's a stub/incomplete implementation
2. If content is valuable, merge into `deferred_renderer.cpp`
3. Otherwise, delete

```cpp
// If this file is mostly empty or just method stubs:
git rm engine/renderer/src/deferred_renderer_stub.cpp
git commit -m "remove: deferred_renderer_stub (incomplete)"
```

---

## SECTION 3: NEWER VERSION FILES (MERGE OR DELETE)

### Priority: REVIEW & MERGE, THEN DELETE

These files appear to be "new" or updated versions. Review before deciding:

#### 3a. Simple Renderer New
**Files:**
- `engine/renderer/include/simple_renderer_new.h`
- `engine/renderer/src/simple_renderer_new.cpp`

**Action:**
```bash
# Step 1: Compare with original
git diff engine/renderer/src/simple_renderer.cpp engine/renderer/src/simple_renderer_new.cpp

# Step 2: If "new" version is better, use it:
git mv engine/renderer/src/simple_renderer_new.cpp engine/renderer/src/simple_renderer.cpp
git rm engine/renderer/include/simple_renderer_new.h

# Step 3: If original is better, just delete new:
git rm engine/renderer/src/simple_renderer_new.cpp
git rm engine/renderer/include/simple_renderer_new.h

git commit -m "refactor: consolidate simple_renderer versions"
```

#### 3b. Physics Constraints New
**Files:**
- `engine/core/physics/constraints.cpp`
- `engine/core/physics/constraints_new.cpp`

**Action:**
```bash
# Step 1: Compare the two versions
git diff engine/core/physics/constraints.cpp engine/core/physics/constraints_new.cpp

# Step 2: If "new" version has fixes/improvements:
# Copy the better version over the old
git rm engine/core/physics/constraints.cpp
git mv engine/core/physics/constraints_new.cpp engine/core/physics/constraints.cpp

# Step 3: Or if they're identical/similar, keep the original:
git rm engine/core/physics/constraints_new.cpp

git commit -m "refactor: consolidate physics constraints implementation"
```

---

## SECTION 4: OUTDATED DOCUMENTATION TO ARCHIVE

### Priority: MOVE TO docs/archive/

These documentation files are outdated (Phase 1-3, before Phase 6 completion):

**Files to Archive:**
- 📄 `docs/phase-1-status.md`
- 📄 `docs/phase-2-status.md`
- 📄 `docs/phase-3-physics-week1.md`
- 📄 `docs/phase-3-week-1-physics.md`
- 📄 `docs/planning-session.md`

**Action:**
```bash
# Step 1: Create archive directory
mkdir -p docs/archive

# Step 2: Move files to archive
git mv docs/phase-1-status.md docs/archive/phase-1-status.md
git mv docs/phase-2-status.md docs/archive/phase-2-status.md
git mv docs/phase-3-physics-week1.md docs/archive/phase-3-physics-week1.md
git mv docs/phase-3-week-1-physics.md docs/archive/phase-3-week-1-physics.md
git mv docs/planning-session.md docs/archive/planning-session.md

# Step 3: Commit
git commit -m "docs: archive outdated phase-1-3 documentation"

# Step 4: Create archive index
# See SECTION 5 below
```

---

## SECTION 5: BUILD ARTIFACTS TO CLEAN

### Priority: DELETE (keep in .gitignore)

**Files to Delete:**
- ❌ `build.log` (binary UTF-16, recreated on each build)
- ❌ `build_output.log` (binary UTF-16)
- ❌ `final_build.log` (binary UTF-16)
- ❌ `build_log.txt`, `build_log2.txt`, `build_log3.txt` (historical)
- ❌ `build_output.txt`, `build_result.txt`, `build_final.txt` (historical)
- ❌ `fullbuild.txt` (historical)

**Action:**
```bash
# These are already in .gitignore typically, but if tracked:
git rm build.log build_output.log final_build.log
git rm build_log.txt build_log2.txt build_log3.txt
git rm build_output.txt build_result.txt build_final.txt
git rm fullbuild.txt

git commit -m "build: remove build artifact logs"

# Ensure these patterns are in .gitignore:
echo "build*.log" >> .gitignore
echo "build*.txt" >> .gitignore
echo "*_build.txt" >> .gitignore
git add .gitignore
git commit -m "build: add build logs to gitignore"
```

---

## SECTION 6: SAMPLE/TEST FILES TO REVIEW

### Priority: REVIEW (may keep or archive)

These are root-level test/sample files. Decide if they should be in `tests/` directory or archived:

**Files:**
- `simple_renderer.cpp` (root)
- `test_obj_loading.cpp` (root)
- `test_obj_simple.cpp` (root)

**Action:**
```bash
# Option A: Move to tests/ if they're useful unit tests
git mv simple_renderer.cpp tests/sample_renderer.cpp
git mv test_obj_loading.cpp tests/test_gltf_loading.cpp
git mv test_obj_simple.cpp tests/test_mesh_simple.cpp

# Option B: Archive if they're old samples
mkdir -p docs/archive/samples
git mv simple_renderer.cpp docs/archive/samples/
git mv test_obj_loading.cpp docs/archive/samples/
git mv test_obj_simple.cpp docs/archive/samples/

git commit -m "tests: organize sample files into tests/ or archive"
```

---

## SECTION 7: OPTIONAL FILES TO REVIEW

### Priority: OPTIONAL (keep unless disk-constrained)

**Files:**
- 📸 `Screenshot 2026-04-11 142744.png`
- 📸 `Screenshot 2026-04-11 142807.png`

**Action:**
```bash
# Option A: Archive screenshots to docs/
mkdir -p docs/archive/screenshots
git mv "Screenshot 2026-04-11 142744.png" docs/archive/screenshots/
git mv "Screenshot 2026-04-11 142807.png" docs/archive/screenshots/

# Option B: Delete if not needed
git rm "Screenshot 2026-04-11 142744.png"
git rm "Screenshot 2026-04-11 142807.png"

git commit -m "docs: archive old screenshots or remove"
```

---

## SECTION 8: CREATE CLEANUP COMMIT SEQUENCE

### Recommended Git Commit Order

Execute these in order to keep history clean:

```bash
cd c:\dev\ProjectSchizo - Copy\c-Engine-Game

# Commit 1: Remove all backup files
git rm engine/renderer/src/opengl/opengl_device.cpp.restored
git rm editor/src/simple_renderer.cpp.restored
git commit -m "cleanup: remove backup (.restored) files"

# Commit 2: Remove stub implementations
git rm engine/renderer/src/deferred_renderer_stub.cpp
git commit -m "cleanup: remove deferred_renderer_stub"

# Commit 3: Consolidate "new" versions
git mv engine/renderer/src/simple_renderer_new.cpp engine/renderer/src/simple_renderer.cpp
git rm engine/renderer/include/simple_renderer_new.h
git mv engine/core/physics/constraints_new.cpp engine/core/physics/constraints.cpp
git commit -m "refactor: consolidate versioned implementations"

# Commit 4: Archive outdated documentation
mkdir -p docs/archive
git mv docs/phase-1-status.md docs/archive/
git mv docs/phase-2-status.md docs/archive/
git mv docs/phase-3-physics-week1.md docs/archive/
git mv docs/phase-3-week-1-physics.md docs/archive/
git mv docs/planning-session.md docs/archive/
git commit -m "docs: archive Phase 1-3 obsolete documentation"

# Commit 5: Clean build artifacts
git rm build.log build_output.log final_build.log
git rm build_log.txt build_log2.txt build_log3.txt build_output.txt build_result.txt build_final.txt fullbuild.txt
git commit -m "build: remove build artifact logs"

# Commit 6: Update .gitignore
echo "" >> .gitignore
echo "# Build artifacts" >> .gitignore
echo "build*.log" >> .gitignore
echo "build*.txt" >> .gitignore
echo "*_build.txt" >> .gitignore
git add .gitignore
git commit -m "build: update .gitignore for build artifacts"

# Verify
git log --oneline -n 10
```

---

## SECTION 9: VERIFY CLEANUP

### Sanity Checks After Cleanup

```bash
# 1. Count .cpp files (should be ~85, down from ~90)
git ls-files *.cpp | wc -l
git ls-files engine/**/*.cpp | wc -l

# 2. Count .h files (should be ~125, down from ~130)
git ls-files *.h | wc -l
git ls-files engine/**/*.h | wc -l

# 3. Search for any remaining .restored files
git ls-files | grep "\.restored"   # Should return nothing

# 4. Verify important files still exist
git ls-files engine/renderer/src/shader.cpp
git ls-files engine/renderer/src/deferred_renderer.cpp
git ls-files engine/core/character/character_controller.cpp
git ls-files engine/core/ability/ability_system.cpp

# 5. Run tests to ensure nothing broke
cd build
cmake --build . --target tests
./bin/tests
# Should see: 27 tests passing
```

---

## SECTION 10: FILES TO KEEP

### ✅ Critical Files (Do NOT Delete)

**Engine Core:**
- ✅ `engine/core/ability/`
- ✅ `engine/core/character/`
- ✅ `engine/core/network/`
- ✅ `engine/core/physics/`
- ✅ `engine/core/animation/` (if exists)
- ✅ `engine/core/input/`
- ✅ `engine/core/logging/`
- ✅ `engine/core/math/`
- ✅ `engine/core/memory/`
- ✅ `engine/core/containers/`
- ✅ `engine/core/file_io/`
- ✅ `engine/core/assets/`

**Renderer:**
- ✅ `engine/renderer/include/` (all files except removed stubs)
- ✅ `engine/renderer/src/shader.cpp`
- ✅ `engine/renderer/src/deferred_renderer.cpp` (MAIN, not stub)
- ✅ `engine/renderer/src/texture.cpp`
- ✅ `engine/renderer/src/mesh.cpp`
- ✅ `engine/renderer/src/animation*.cpp`
- ✅ `engine/renderer/src/lighting.cpp`
- ✅ `engine/renderer/src/post_processing.cpp`

**Scene & Window:**
- ✅ `engine/scene/`
- ✅ `engine/window/`

**Build:**
- ✅ `CMakeLists.txt`
- ✅ `CMakePresets.json`
- ✅ `.gitignore`

**Documentation:**
- ✅ `docs/EngineTechnicalRework/` (comprehensive design)
- ✅ `docs/architecture/` (current architecture)
- ✅ `docs/phase-6-planning/` (recent, relevant)
- ✅ `docs/REWORK_MASTER_PLAN.md` (NEW - master plan)
- ✅ `docs/CLEANUP_GUIDE.md` (NEW - this file)
- ✅ `README.md`

**Tests:**
- ✅ `tests/` (all test files)

**Third-party:**
- ✅ `third_party/` (all except GLAD, GLFW partially refactored)

---

## SECTION 11: POST-CLEANUP TASK CHECKLIST

After cleanup is complete, execute these tasks:

- [ ] All commits pushed to remote
- [ ] CI/CD pipeline runs successfully (tests pass)
- [ ] Build system works: `cmake -B build && cmake --build build`
- [ ] All 27 unit tests pass
- [ ] No compiler warnings introduced
- [ ] README updated with current status
- [ ] Team notified of cleanup (if multi-developer)
- [ ] Backup branch created (git tag the cleanup checkpoint)

```bash
# Create a tag for the cleanup milestone
git tag -a cleanup-phase-0 -m "Cleanup complete: backup files removed, docs archived"
git push origin cleanup-phase-0

# For reference in future history
git log --oneline --graph --decorate | head -20
```

---

## CLEANUP COMPLETION CHECKLIST

Use this to track progress:

```
BACKUP FILES:
  ☐ opengl_device.cpp.restored
  ☐ simple_renderer.cpp.restored
  ☐ All other *.restored files

STUB FILES:
  ☐ deferred_renderer_stub.cpp

VERSIONED FILES (merge/consolidate):
  ☐ simple_renderer_new.cpp/.h
  ☐ constraints_new.cpp

DOCUMENTATION (archive):
  ☐ phase-1-status.md → archive/
  ☐ phase-2-status.md → archive/
  ☐ phase-3-physics-week1.md → archive/
  ☐ phase-3-week-1-physics.md → archive/
  ☐ planning-session.md → archive/

BUILD ARTIFACTS:
  ☐ build.log
  ☐ build_output.log
  ☐ final_build.log
  ☐ build_log.txt, build_log2.txt, build_log3.txt
  ☐ build_output.txt, build_result.txt, build_final.txt
  ☐ fullbuild.txt
  ☐ .gitignore updated

VERIFICATION:
  ☐ All tests still pass (27/27)
  ☐ Build succeeds without warnings
  ☐ Git history clean
  ☐ No .restored files remain
  ☐ docs/archive/ created with old docs
  ☐ Cleanup tag created
  ☐ Team notified (if applicable)
```

---

## ESTIMATED TIME

- **Backup files:** 2 minutes
- **Stub files:** 5 minutes
- **Versioned files review:** 10-15 minutes
- **Documentation archiving:** 5 minutes
- **Build artifacts:** 2 minutes
- **Testing & verification:** 10 minutes
- **Git tagging & cleanup:** 5 minutes

**Total: ~40-50 minutes**

---

**Document Version:** 1.0  
**Last Updated:** April 20, 2026  
**Status:** Ready for Execution

