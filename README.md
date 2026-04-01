# Game-Worldshaper

A deterministic game engine for multiplayer voxel-based world simulation with rollback netcode support.

## 📊 Project Status

| Phase | Component | Status |
|-------|-----------|--------|
| **Phase 1** | Core Math, Memory, I/O, Containers | ✅ **COMPLETE** — 27/27 tests passing |
| Phase 2 | Graphics & Rendering | ⏳ Coming next |
| Phase 3-10 | Animation, Physics, Editor, Networking | 📋 Planned |

## ✅ Phase 1: Foundation (Complete)

**Core Features Implemented:**
- **Math Library:** Vec2/Vec3/Vec4, Mat3/Mat4, Quaternions, Transforms with full operator support
- **Physics Utilities:** Verlet particles, constraints, vector fields, PDE solvers
- **Memory System:** Stack, Pool, and General allocators with tracking
- **File I/O:** Virtual filesystem, async loading, type-safe asset handles
- **Containers:** DynamicArray, HashMap, RingBuffer, ObjectPool with custom allocator support
- **Build System:** CMake cross-platform (Windows MSVC, Linux GCC)
- **Testing:** Catch2 integration with 27 unit tests, all passing
- **CI/CD:** GitHub Actions for automated builds and tests

## 🚀 Quick Start

### Prerequisites
- CMake 3.20+
- MSVC 2022 (Windows) or GCC 11+ (Linux)
- C++20 support

### Build

```bash
# Windows with MSVC
cmake --preset windows-msvc-debug -S . -B build/windows-debug
cmake --build build/windows-debug --config Debug

# Linux with GCC
cmake --preset linux-gcc-debug -S . -B build/linux-debug
cmake --build build/linux-debug
```

### Run Tests

```bash
cd build/windows-debug
ctest --output-on-failure
```

## 📚 Documentation

- [Phase 1 Status & Architecture](docs/phase-1-status.md)
- [Engine Roadmap](docs/ENGINE-ROADMAP.md)
- [Game Design Document](docs/game-design/project-schizo.md)

## 🏗️ Project Structure

```
engine/
├── core/
│   ├── math/           # Math library (vectors, matrices, quaternions)
│   ├── memory/         # Allocator implementations
│   ├── physics/        # Physics utilities and solvers
│   ├── logging/        # Logging system (spdlog)
│   ├── file_io/        # Virtual filesystem and asset management
│   └── containers/     # Custom containers with allocator support
editor/                # Engine editor (Phase 6)
game/                  # Game client implementation
tests/                 # Comprehensive test suite
```

## ✨ Key Achievements in Phase 1

- ✅ **Deterministic Floating-Point:** All math uses 32-bit floats for rollback compatibility
- ✅ **Zero-Cost Abstractions:** Optional physics module has no overhead if unused
- ✅ **Type Safety:** Custom containers and handles prevent common bugs
- ✅ **Cross-Platform:** Single codebase builds on Windows and Linux
- ✅ **Test-Driven:** 27 comprehensive unit tests ensure code quality
- ✅ **CI/CD Ready:** GitHub Actions automatically validates all changes