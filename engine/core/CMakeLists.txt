cmake_minimum_required(VERSION 3.20)

# Core systems - foundational libraries for the engine
add_subdirectory(memory)
add_subdirectory(math)
add_subdirectory(physics)  # Physics module enabled
add_subdirectory(logging)
add_subdirectory(file_io)
add_subdirectory(containers)
add_subdirectory(assets)
add_subdirectory(input)     # Input system - typed action mapping
add_subdirectory(character) # Character controller system
add_subdirectory(ability)   # Ability system - abilities, effects, modifiers, skill tree
add_subdirectory(network)   # Network system - multiplayer, deterministic simulation, rollback
add_subdirectory(gameplay)  # Game runtime foundations (abilities, combat, NPC AI, deterministic state)
