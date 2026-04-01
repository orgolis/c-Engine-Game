// ====================
// Container Wrappers Examples
// ====================

#pragma once

#include <iostream>
#include <string>

namespace gws::containers::examples {

// ====================
// Example 1: DynamicArray Basic Usage
// ====================
void DynamicArrayBasics() {
    using namespace gws::containers;
    
    DynamicArray<int> numbers;
    
    // Add elements
    numbers.Push(10);
    numbers.Push(20);
    numbers.Push(30);
    
    // Access elements
    std::cout << "First: " << numbers.Front() << "\n";
    std::cout << "Last: " << numbers.Back() << "\n";
    std::cout << "Size: " << numbers.Size() << "\n";
    
    // Iteration
    for (int num : numbers) {
        std::cout << num << " ";
    }
    std::cout << "\n";
    
    // Pop
    numbers.Pop();
    std::cout << "After pop: " << numbers.Size() << "\n";
    
    // Clear
    numbers.Clear();
    std::cout << "After clear: " << numbers.IsEmpty() << "\n";
}

// ====================
// Example 2: DynamicArray Advanced Operations
// ====================
void DynamicArrayAdvanced() {
    using namespace gws::containers;
    
    DynamicArray<std::string> items;
    
    // Emplace directly
    items.Emplace("apple");
    items.Emplace("banana");
    items.Emplace("cherry");
    
    // Reserve capacity
    items.Reserve(100);
    std::cout << "Capacity: " << items.Capacity() << ", Size: " << items.Size() << "\n";
    
    // Resize with default value
    items.Resize(5, std::string("empty"));
    std::cout << "After resize: " << items.Size() << "\n";
    
    // Safe at() access
    try {
        std::string& item = items.At(1);
        std::cout << "Item at 1: " << item << "\n";
    } catch (const std::out_of_range& e) {
        std::cout << "Out of range: " << e.what() << "\n";
    }
    
    // PopValue
    if (items.Size() > 0) {
        auto value = items.PopValue();
        std::cout << "Popped: " << value << "\n";
    }
    
    // Shrink to fit
    items.Shrink();
}

// ====================
// Example 3: DynamicArray with Custom Types
// ====================
struct Point {
    float x, y, z;
    Point(float x = 0, float y = 0, float z = 0) : x(x), y(y), z(z) {}
};

void DynamicArrayCustomTypes() {
    using namespace gws::containers;
    
    DynamicArray<Point> vertices;
    
    // Emplace with constructor arguments
    vertices.Emplace(1.0f, 2.0f, 3.0f);
    vertices.Emplace(4.0f, 5.0f, 6.0f);
    vertices.Emplace(7.0f, 8.0f, 9.0f);
    
    // Direct access via pointer
    Point* data = vertices.Data();
    std::cout << "First vertex: (" << data[0].x << ", " << data[0].y << ", " << data[0].z << ")\n";
    
    // Iteration
    for (const auto& point : vertices) {
        std::cout << "Point: " << point.x << ", " << point.y << ", " << point.z << "\n";
    }
    
    // Get underlying vector for advanced operations
    std::vector<Point>& vec = vertices.GetData();
    std::cout << "Underlying vector size: " << vec.size() << "\n";
}

// ====================
// Example 4: HashMap Basic Usage
// ====================
void HashMapBasics() {
    using namespace gws::containers;
    
    HashMap<std::string, int> scores;
    
    // Insert
    scores.Insert("Alice", 100);
    scores.Insert("Bob", 85);
    scores.Insert("Charlie", 92);
    
    // Check if contains
    if (scores.Contains("Alice")) {
        std::cout << "Alice is in the map\n";
    }
    
    // Set (returns reference to value)
    scores.Set("Alice", 110);
    
    // Get
    int* alice_score = scores.Find("Alice");
    if (alice_score) {
        std::cout << "Alice's score: " << *alice_score << "\n";
    }
    
    // Access via []
    std::cout << "Bob's score: " << scores["Bob"] << "\n";
    
    // Size
    std::cout << "Number of entries: " << scores.Size() << "\n";
    
    // Erase
    bool erased = scores.Erase("Charlie");
    std::cout << "Erased Charlie: " << (erased ? "yes" : "no") << "\n";
}

// ====================
// Example 5: HashMap Safe Access Patterns
// ====================
void HashMapSafeAccess() {
    using namespace gws::containers;
    
    HashMap<std::string, double> prices;
    
    prices.Insert("apple", 1.99);
    prices.Insert("banana", 0.59);
    prices.Insert("orange", 0.99);
    
    // Safe Get pattern
    double price;
    std::string item = "apple";
    if (prices.Get(item, price)) {
        std::cout << item << " costs $" << price << "\n";
    } else {
        std::cout << item << " not found\n";
    }
    
    // Safe Find pattern
    if (auto price_ptr = prices.Find("banana")) {
        std::cout << "Found banana at $" << *price_ptr << "\n";
    }
    
    // Iteration
    std::cout << "All prices:\n";
    for (const auto& [name, price] : prices) {
        std::cout << "  " << name << ": $" << price << "\n";
    }
}

// ====================
// Example 6: HashMap with Custom Types
// ====================
struct Asset {
    std::string path;
    size_t size_bytes;
    bool is_loaded;
};

void HashMapCustomTypes() {
    using namespace gws::containers;
    
    HashMap<std::string, Asset> assets;
    
    // Emplace with constructor
    assets.Emplace("player_idle", Asset{"models/player_idle.mesh", 2048, true});
    assets.Emplace("player_walk", Asset{"models/player_walk.anim", 4096, false});
    
    // Modify asset
    if (auto asset_ptr = assets.Find("player_idle")) {
        asset_ptr->is_loaded = true;
        std::cout << "Updated player_idle status\n";
    }
    
    // Iteration with structured bindings
    for (auto& [name, asset] : assets) {
        std::cout << name << ": " << asset.path 
                  << " (" << asset.size_bytes << " bytes)"
                  << " loaded=" << (asset.is_loaded ? "yes" : "no") << "\n";
    }
}

// ====================
// Example 7: Typed Asset Handles
// ====================

// Asset types (would normally be in different headers)
struct Mesh {
    std::string name;
    size_t vertex_count;
    size_t index_count;
};

struct Texture {
    std::string name;
    int width, height;
    int channels;
};

struct Shader {
    std::string name;
    std::string vertex_code;
    std::string fragment_code;
};

void TypeSafeAssetHandles() {
    // Using Handle<T> from file_io module
    using namespace gws::file_io;
    
    // Note: In real code, these would come from AssetManager
    // Here we're illustrating the type-safe concept
    
    Handle<Mesh> mesh_handle(1);
    Handle<Texture> texture_handle(2);
    Handle<Shader> shader_handle(3);
    
    // These are different types - no accidental mixing
    // mesh_handle = texture_handle;  // ERROR: Type mismatch!
    
    std::cout << "Mesh handle valid: " << (mesh_handle.IsValid() ? "yes" : "no") << "\n";
    std::cout << "Texture handle valid: " << (texture_handle.IsValid() ? "yes" : "no") << "\n";
    std::cout << "Shader handle valid: " << (shader_handle.IsValid() ? "yes" : "no") << "\n";
}

// ====================
// Example 8: Combined DynamicArray + HashMap for Scene Data
// ====================
void SceneDataManagement() {
    using namespace gws::containers;
    
    HashMap<std::string, DynamicArray<Point>> geometry;
    
    // Create multiple meshes
    DynamicArray<Point> cube_vertices;
    cube_vertices.Emplace(-1, -1, -1);
    cube_vertices.Emplace( 1, -1, -1);
    cube_vertices.Emplace( 1,  1, -1);
    cube_vertices.Emplace(-1,  1, -1);
    
    DynamicArray<Point> sphere_vertices;
    sphere_vertices.Emplace(0, 1, 0);
    sphere_vertices.Emplace(1, 0, 0);
    sphere_vertices.Emplace(0, -1, 0);
    
    // Store in map
    geometry.Set("cube", cube_vertices);
    geometry.Set("sphere", sphere_vertices);
    
    // Retrieve and process
    if (auto vertices = geometry.Find("cube")) {
        std::cout << "Cube has " << vertices->Size() << " vertices\n";
    }
    
    // Iterate all meshes
    for (auto& [mesh_name, vertices] : geometry) {
        std::cout << mesh_name << ": " << vertices.Size() << " vertices\n";
    }
}

// ====================
// Example 9: RingBuffer for Event Queue
// ====================
void RingBufferEventQueue() {
    using namespace gws::containers;
    
    struct Event {
        std::string type;
        float timestamp;
    };
    
    // Fixed-size event queue (last 10 events)
    RingBuffer<Event> event_history(10);
    
    // Add events
    event_history.Push({"KeyDown", 0.0f});
    event_history.Push({"MouseMove", 0.1f});
    event_history.Push({"KeyUp", 0.2f});
    
    std::cout << "Event queue size: " << event_history.Size() << "\n";
    std::cout << "Is full: " << (event_history.IsFull() ? "yes" : "no") << "\n";
    
    // Access front event
    const Event& latest = event_history.Front();
    std::cout << "Latest event: " << latest.type << " at " << latest.timestamp << "\n";
}

// ====================
// Example 10: ObjectPool for Entity Allocation
// ====================
void ObjectPoolEntityAllocation() {
    using namespace gws::containers;
    
    struct Entity {
        std::string name;
        float x, y, z;
        Entity() : name("Default"), x(0), y(0), z(0) {}
    };
    
    ObjectPool<Entity> entity_pool(5);
    
    // Acquire entities
    auto entity1 = entity_pool.Acquire();
    entity1->name = "Player";
    auto entity2 = entity_pool.Acquire();
    entity2->name = "Enemy";
    
    std::cout << "Available in pool: " << entity_pool.AvailableCount() << "\n";
    
    // Release back to pool
    entity_pool.Release(std::move(entity1));
    std::cout << "After release: " << entity_pool.AvailableCount() << "\n";
    
    // Acquire reuses released objects
    auto entity3 = entity_pool.Acquire();
    std::cout << "Reused entity name: " << entity3->name << "\n";
}

}  // namespace gws::containers::examples
