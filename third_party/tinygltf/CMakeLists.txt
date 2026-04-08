# tinygltf - header-only glTF 2.0 loader library
# This is an interface library configuration for tinygltf

add_library(tinygltf INTERFACE)
target_include_directories(tinygltf INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# tinygltf requires C++11 minimum
target_compile_features(tinygltf INTERFACE cxx_std_11)

# Define the TINYGLTF header-only flag
target_compile_definitions(tinygltf INTERFACE 
    TINYGLTF_IMPLEMENTATION
)
