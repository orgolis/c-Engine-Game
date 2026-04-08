cmake_minimum_required(VERSION 3.20)

# Core systems - foundational libraries for the engine
add_subdirectory(memory)
add_subdirectory(math)
add_subdirectory(physics)  # Physics module enabled
add_subdirectory(logging)
add_subdirectory(file_io)
add_subdirectory(containers)
add_subdirectory(assets)
