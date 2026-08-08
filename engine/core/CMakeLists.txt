cmake_minimum_required(VERSION 3.20)

# Core systems - foundational libraries for the engine
add_subdirectory(memory)
add_subdirectory(compression) # LZ4 + Zstd front-end (Stage 2.2)
add_subdirectory(profiler)    # Core CPU profiler - scoped zones, per-thread (Stage 0.6)
add_subdirectory(jobs)        # Job system - work-stealing thread pool (Stage 0.3)
add_subdirectory(reflection)    # Core RTTI++ reflection (Stage 0.4)
add_subdirectory(serialization) # Reflection-driven binary archive (Stage 0.5)
add_subdirectory(ecs)           # ECS: EnTT World + POD components (Stage 1)
add_subdirectory(asset_pipeline)# Cooked-asset bundle spine (Stage 2)
add_subdirectory(math)
add_subdirectory(physics)  # Physics module enabled
add_subdirectory(logging)
add_subdirectory(diagnostics) # Crash handler + persistent/ring-buffer logging
add_subdirectory(file_io)
add_subdirectory(containers)
add_subdirectory(assets)
add_subdirectory(input)     # Input system - typed action mapping
add_subdirectory(character) # Character controller system
add_subdirectory(ability)   # Ability system - abilities, effects, modifiers, skill tree
add_subdirectory(network)   # Network system - multiplayer, deterministic simulation, rollback
add_subdirectory(audio)     # Audio: miniaudio device + custom voice mixer (Stage 6)
add_subdirectory(gameplay)  # Game runtime foundations (abilities, combat, NPC AI, deterministic state)
add_subdirectory(ai)        # AI: triangle navmesh (A* + funnel) + behavior trees (Stage 9)
add_subdirectory(ui)        # Runtime game-UI framework (widgets/layout/input) — Game-UI pillar
add_subdirectory(world)     # World streaming: grid + cell load/unload + floating origin
add_subdirectory(vfx)       # VFX: CPU particle system + camera-facing billboards
add_subdirectory(combat)    # Combat: frame-data hitbox windows + dodge/parry/poise
