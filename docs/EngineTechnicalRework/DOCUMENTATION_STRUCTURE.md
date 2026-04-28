# Documentation Structure & Organization

**Last Updated**: April 27, 2026  
**Status**: ✅ Properly Organized

---

## Overview

All documentation has been reorganized into a clear, hierarchical structure:

```
docs/
├── EngineTechnicalRework/      ← PLANNING & EXECUTION TRACKING
│   ├── EXECUTION_CHECKLIST.md          ✅ PRIMARY MASTER REFERENCE
│   ├── DEVELOPMENT_PLAN.md
│   ├── REWORK_MASTER_PLAN.md
│   ├── CLEANUP_GUIDE.md
│   └── DEPENDENCIES_AND_LIBRARIES.md
│
├── architecture/               ← TECHNICAL SYSTEM DESIGN
│   ├── GRAPHICS-ABSTRACTION-LAYER.md
│   ├── PHASE_4_DEFERRED_PIPELINE.md
│   ├── PHASE_5_CULLING.md
│   ├── PHASE_6_EDITOR_AND_HZB_CULLING.md
│   ├── PHASE_7_GPU_PROFILER.md
│   └── physics_module.md
│
├── archive/                    ← HISTORICAL REFERENCES
│   ├── PHASE_3_*.md
│   ├── PHASE_6_WEEK_*.md
│   └── [other completed work]
│
└── [Legacy game system docs]
    ├── LIGHTING-SYSTEM.md
    ├── MESH-AND-GEOMETRY-SYSTEM.md
    └── etc.
```

---

## Folder Purposes

### 📋 docs/EngineTechnicalRework/ - PLANNING & EXECUTION

**Purpose**: Central hub for current development planning and tracking

**What Goes Here**:
- ✅ EXECUTION_CHECKLIST.md - Current phase/week tracking (USE THIS!)
- ✅ DEVELOPMENT_PLAN.md - General engine development roadmap
- ✅ REWORK_MASTER_PLAN.md - Vulkan migration strategy overview
- ✅ CLEANUP_GUIDE.md - Onboarding and setup
- ✅ DEPENDENCIES_AND_LIBRARIES.md - Build dependencies

**Who Uses It**:
- Project managers (tracking progress)
- Developers (knowing what to work on next)
- CI/CD systems (automated phase checking)

**Key Document**: **EXECUTION_CHECKLIST.md** ← READ THIS FIRST

---

### 🏗️ docs/architecture/ - TECHNICAL SYSTEM DESIGN

**Purpose**: Design documentation for all major engine subsystems

**What Goes Here**:
- Phase-specific architecture (how each system works)
- Technical deep-dives (profiler, culling, rendering pipeline)
- Design decisions and rationale
- Performance characteristics
- Integration patterns

**Current Architecture Docs**:
- GRAPHICS-ABSTRACTION-LAYER.md - API abstraction layer
- PHASE_4_DEFERRED_PIPELINE.md - Deferred rendering
- PHASE_5_CULLING.md - Frustum culling
- PHASE_6_EDITOR_AND_HZB_CULLING.md - Editor + HZB
- PHASE_7_GPU_PROFILER.md - GPU profiling
- physics_module.md - Physics system

**Who Uses It**:
- Architects (designing new features)
- Developers (understanding existing systems)
- Code reviewers (verifying design compliance)

---

### 📦 docs/archive/ - HISTORICAL REFERENCES

**Purpose**: Store completed work for historical reference

**What Goes Here**:
- Week-by-week summaries (PHASE_6_WEEK_21.md, etc.)
- Completed phase documentation
- Old planning documents
- Obsolete technical notes

**Why Archive**:
- Keeps current folders clean
- Searchable history of what was done
- Prevents confusion with current work
- Easy to reference "how we solved X before"

---

## How to Use This Structure

### 🎯 For Day-to-Day Development

1. **Check what to work on next**:
   → Open `docs/EngineTechnicalRework/EXECUTION_CHECKLIST.md`

2. **Understand the system you're modifying**:
   → Read relevant docs in `docs/architecture/`

3. **Look up historical context**:
   → Search `docs/archive/` for completed phase work

### 🎯 For Code Review

1. **Verify phase compliance**:
   → Check against EXECUTION_CHECKLIST.md
   → Confirm design matches architecture/ docs

2. **Assess technical correctness**:
   → Review relevant architecture document
   → Verify implementation matches design

### 🎯 For Onboarding New Developers

1. **Understand project scope**:
   → Read REWORK_MASTER_PLAN.md
   → Review EXECUTION_CHECKLIST.md for current state

2. **Get current phase context**:
   → Read corresponding architecture doc
   → Review relevant code comments

---

## File Organization Rules

✅ **DO**:
- Keep planning docs in `EngineTechnicalRework/`
- Keep technical architecture in `architecture/`
- Archive completed week summaries to `archive/`
- Link between docs (reference EXECUTION_CHECKLIST from other docs)

❌ **DON'T**:
- Mix architecture docs with planning docs
- Keep redundant copies of the same content
- Archive current-phase documentation
- Create new docs without clear purpose

---

## Current Phase (Phase 7 Week 25)

**Tracking Document**: EXECUTION_CHECKLIST.md  
**Architecture Docs**: PHASE_7_GPU_PROFILER.md + PHASE_6_EDITOR_AND_HZB_CULLING.md  
**Archived Docs**: PHASE_6_WEEK_21-24.md (moved to archive/)  

**What's in Development**:
- GPU Profiler queries (Week 25)
- Transform gizmo rendering (Week 26 prep)
- Performance optimization (Week 27 prep)

---

## Next Update

When Phase 7 Week 25 completes:
1. Move PHASE_7_WEEK_25_GPU_PROFILER.md → archive/ (if was interim doc)
2. Keep PHASE_7_GPU_PROFILER.md in architecture/ (permanent reference)
3. Update EXECUTION_CHECKLIST.md with Week 26 status

---

## Documentation Health Checklist

- ✅ EXECUTION_CHECKLIST.md exists and is current
- ✅ Architecture docs in proper folder
- ✅ No redundant copies of same documentation
- ✅ Old phase docs archived
- ✅ Clear folder purposes
- ✅ This guide (you're reading it!)

**Status**: ✅ Documentation properly organized and accessible
