# Documentation Structure

This documentation folder is organized into a hierarchical structure for easy navigation and maintenance. Each subfolder contains related documentation for different aspects of the c-Engine-Game project.

## Folder Organization

### 📂 **EngineTechnicalRework/**
Vulkan migration planning and engine technical rework documentation. Contains phase-based planning, status reports, and technical pre-planning documents.

**Key files:**
- `REWORK_MASTER_PLAN.md` - Comprehensive master plan for engine rework
- `DEVELOPMENT_PLAN.md` - Development roadmap
- `DEPENDENCIES_AND_LIBRARIES.md` - Library and dependency documentation
- `phase-*-status.md` - Individual phase status reports

---

### 📂 **architecture/**
Technical engine systems and architectural documentation. Covers graphics abstractions, physics modules, and high-level system design.

**Key files:**
- `ENGINE_ARCHITECTURE_OVERVIEW.md` - Overall engine architecture
- `GRAPHICS-ABSTRACTION-LAYER.md` - Graphics rendering abstraction
- `physics_module.md` - Physics system documentation
- `HZB_OCCLUSION_CULLING.md` - Occlusion culling implementation
- `PHASE_*_*.md` - Phase-specific architecture documents

---

### 📂 **systems/**
Game engine systems documentation (non-Vulkan migration). Covers individual rendering and gameplay systems.

**Contains:**
- `DEFERRED-RENDERING-SYSTEM.md` - Deferred rendering pipeline
- `LIGHTING-SYSTEM.md` - Lighting system architecture
- `MATERIAL-AND-PBR-SYSTEM.md` - Material and PBR system
- `MESH-AND-GEOMETRY-SYSTEM.md` - Mesh and geometry management
- `POST-PROCESSING-SYSTEM.md` - Post-processing effects
- `SCENE-MANAGEMENT-SYSTEM.md` - Scene organization and management

---

### 📂 **references/**
Quick reference guides for developers. Concise, searchable references for common systems.

**Contains:**
- `*-QUICK-REFERENCE.md` - Quick reference for each system
- System-specific API and usage guides
- Fast lookup for common tasks and patterns

---

### 📂 **testing/**
Testing, debugging, and quality assurance documentation. Includes testing strategies and debugging guides.

**Contains:**
- `CAMERA_*.md` - Camera system testing and debugging guides
- `PHYSICS-TESTING.md` - Physics system testing
- `TESTING_GAMEPLAY.md` - Gameplay testing guidelines
- Diagnostic and debugging instructions for various systems

---

### 📂 **design/**
Game design documentation. Design specifications and game design documents.

**Contains:**
- `GAME-DESIGN.md` - Game design document and specifications

---

### 📂 **archive/**
Completed and historical work. Old phases and deprecated documentation kept for reference.

**Contains:**
- `PHASE-2-*.md` - Phase 2 completion documents
- `PHASE-6-*.md` - Phase 6 completion and editor integration
- `WEEK-5-6-*.md` - Weekly completion summaries
- `ENGINE-ROADMAP.md` - Legacy roadmap (superseded)

---

### 📂 **legacy/**
Reserved for old/obsolete documentation. Currently empty, available for future organization needs.

---

### 📂 **game-design/** (Existing)
Additional game design resources and specifications.

**Contains:**
- `project-schizo.md` - Project Schizo game design

---

### 📂 **phase-6-planning/** (Existing)
Phase 6 detailed planning and implementation documentation.

**Key files:**
- `IMPLEMENTATION-ROADMAP.md` - Implementation roadmap
- `TESTING-STRATEGY.md` - Testing approach
- `CODE-REVIEW-CHECKLIST.md` - Code review guidelines
- `SYSTEM-INTERACTIONS-DETAILED.md` - System interaction documentation

---

## Quick Navigation Guide

### 👨‍💻 For Developers
1. **Understanding the engine**: Start with `architecture/ENGINE_ARCHITECTURE_OVERVIEW.md`
2. **Learning a specific system**: Check `systems/[SYSTEM]-SYSTEM.md`
3. **Quick lookup**: Use `references/[SYSTEM]-QUICK-REFERENCE.md`
4. **Debugging**: See `testing/` folder for debugging guides

### 📋 For Project Managers
1. **Current status**: Check `EngineTechnicalRework/REWORK_MASTER_PLAN.md`
2. **Phase status**: Browse `EngineTechnicalRework/phase-*-status.md`
3. **Roadmap**: Review `phase-6-planning/IMPLEMENTATION-ROADMAP.md`

### 🎮 For Game Designers
1. **Game design**: See `design/GAME-DESIGN.md`
2. **Project details**: Review `game-design/project-schizo.md`

### 🧪 For QA/Testers
1. **Testing strategy**: `phase-6-planning/TESTING-STRATEGY.md`
2. **System-specific tests**: `testing/` folder
3. **Debugging guides**: Various `testing/` files

---

## File Statistics

- **Total Folders**: 9 (EngineTechnicalRework, architecture, systems, references, testing, design, archive, legacy, game-design, phase-6-planning)
- **Total Documentation Files**: 70+ organized markdown files
- **Organization Levels**: 2-3 levels deep

---

## Organization Principles

1. **Logical Grouping**: Related documents are grouped by system or purpose
2. **Clear Categorization**: Prefixes and folder names indicate content type
3. **Discoverability**: Quick references and guides make content easy to find
4. **Archival**: Completed phases stored separately to reduce clutter
5. **Maintainability**: Consistent naming and structure for ease of updates

---

## Maintenance Notes

- When adding new documentation, place it in the most appropriate folder
- Archive phase-specific or completed documents after project phases complete
- Update this README when adding new top-level categories
- Keep quick references synchronized with system documentation

Last Updated: 2024
