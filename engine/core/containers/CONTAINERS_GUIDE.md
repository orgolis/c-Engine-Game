# Container Wrappers & Asset Handles

## Overview

The container system provides type-safe, efficient wrappers around standard C++ containers with additional features for game engine development:

- **DynamicArray<T>** — Growable array with convenience methods
- **HashMap<Key, Value>** — Type-safe hash table with safe access patterns
- **RingBuffer<T>** — Fixed-size circular buffer for event queues
- **ObjectPool<T>** — Object pool for efficient allocation
- **Handle<T>** — Type-safe asset references (file_io module)
- **Allocator Support** — Custom allocator templates for memory management

## Design Philosophy

All containers follow these principles:

1. **Zero Overhead** — Thin wrappers around STL with no hidden performance costs
2. **Type Safety** — Templates prevent accidental type confusion
3. **Safe by Default** — Exception throwing and range checking available
4. **STL Compatible** — Full iterator support and standard algorithms
5. **Allocator Aware** — Support for custom memory allocation strategies

## Core Components

### DynamicArray<T>

Wrapper around `std::vector<T>` with convenient methods and consistent naming.

```cpp
using namespace gws::containers;

DynamicArray<int> numbers;
numbers.Push(10);
numbers.Push(20);
numbers.Push(30);

// Access
int first = numbers.Front();
int last = numbers.Back();
int at_index = numbers[1];          // Unchecked
int safe = numbers.At(1);           // Throws if out of range

// Query
size_t size = numbers.Size();
size_t capacity = numbers.Capacity();
bool empty = numbers.IsEmpty();

// Modify
numbers.Emplace(5);                 // Construct in-place
numbers.Pop();
int value = numbers.PopValue();     // Pop and return value
numbers.Clear();
numbers.Reserve(1000);
numbers.Resize(10, 0);              // Resize with default value
numbers.Shrink();                   // Shrink capacity to fit

// Erase
auto it = numbers.Erase(numbers.begin());
auto it2 = numbers.Erase(numbers.begin(), numbers.end());

// Iteration
for (int num : numbers) { }
auto* data = numbers.Data();        // Get raw pointer
auto& vec = numbers.GetData();      // Get underlying std::vector
```

#### Type Aliases for Standard Containers

```cpp
using Vector<T>                              = std::vector<T>;
using Deque<T>                               = std::deque<T>;
using AllocVector<T, Allocator>              = std::vector<T, Allocator>;
using AllocDeque<T, Allocator>               = std::deque<T, Allocator>;
```

### HashMap<Key, Value>

Type-safe hash table with convenient access patterns.

```cpp
using namespace gws::containers;

HashMap<std::string, int> scores;

// Insert
scores.Insert("Alice", 100);
scores.Insert("Bob", 85);
scores.Set("Alice", 110);           // Always sets (insert or update)

// Query
bool has_alice = scores.Contains("Alice");
int* score_ptr = scores.Find("Alice");     // Returns pointer or nullptr
int score;
if (scores.Get("Alice", score)) {          // Safe get with bool return
    // Use score
}

// Access (creates entry if missing)
int bob_score = scores["Bob"];

// Size/Capacity
size_t count = scores.Size();
bool empty = scores.IsEmpty();
size_t buckets = scores.BucketCount();

// Erase
bool removed = scores.Erase("Bob");         // Return: was key present?
auto it = scores.Erase(it);                 // Return: next iterator

// Optimize
scores.Rehash(large_bucket_count);

// Iteration
for (auto& [key, value] : scores) {         // Structured binding
    std::cout << key << " = " << value << "\n";
}

// Access underlying map
auto& map = scores.GetData();
```

#### Key Design Features

**Safe .Get() Pattern**
```cpp
Value result;
if (map.Get(key, result)) {
    // Key exists, result is valid
} else {
    // Key not found
}
```

**Pointer .Find() Pattern**
```cpp
if (auto ptr = map.Find(key)) {
    // Key exists, use *ptr
    ptr->member = value;
} else {
    // Key not found
}
```

**Difference from std::unordered_map**
```cpp
// Standard map creates entry on access:
int val = map["missing_key"];       // Now map contains "missing_key"

// Our HashMap requires explicit insert/set:
int val = map["missing_key"];       // Also creates entry
map.Set(key, value);                // Explicit set
auto [it, inserted] = map.Insert(key, value);  // Returns pair
```

### RingBuffer<T>

Fixed-size circular buffer for event queues and frame data.

```cpp
using namespace gws::containers;

RingBuffer<Event> events(100);  // Fixed capacity of 100

// Push (overwrites old data when full)
events.Push(event1);
events.Push(event2);

// Query
size_t size = events.Size();
size_t capacity = events.Capacity();
bool full = events.IsFull();
bool empty = events.IsEmpty();

// Access front
Event& front = events.Front();

// Pop from front (removes and returns)
Event old = events.Pop();

// Clear all
events.Clear();
```

**Use Cases:**
- Event history (keeps last N events)
- Frame timing data
- Input buffer
- Frame-by-frame diagnostics

### ObjectPool<T>

Pre-allocated object pool for efficient allocation/deallocation.

```cpp
using namespace gws::containers;

ObjectPool<Entity> pool(256);  // Pre-allocate 256 entities

// Acquire (reuses from pool or creates new)
auto entity = pool.Acquire();
entity->Initialize("Player");

// Use entity...

// Release back to pool
pool.Release(std::move(entity));

// Query
size_t available = pool.AvailableCount();

// Clear all
pool.Clear();
```

**Benefits:**
- Avoids repeated allocation/deallocation
- Predictable performance
- Useful for N entities/particles/bullets per frame
- Thread-unsafe (single-threaded use)

### Handle<T> — Type-Safe Asset References

```cpp
using namespace gws::file_io;

// From asset manager
Handle<Mesh> mesh_handle = asset_manager.LoadAsset<Mesh>("mesh.obj", loader);
Handle<Texture> texture_handle = asset_manager.LoadAsset<Texture>("texture.png", loader);

// Query validity
if (mesh_handle.IsValid()) {
    // Handle points to real asset
}

// Retrieve asset
if (auto mesh = asset_manager.GetAsset(mesh_handle)) {
    // Use mesh
}

// Comparison
if (mesh_handle == other_mesh_handle) { }
if (mesh_handle != other_mesh_handle) { }

// Get internal ID
uint32_t id = mesh_handle.GetId();

// Key benefit: Type safety
// mesh_handle = texture_handle;     // COMPILE ERROR!
// auto mesh = asset_manager.GetAsset(texture_handle);  // Type mismatch prevention
```

**Why Handle<T> is Better**

1. **Type Checked at Compile Time**
   ```cpp
   // Safe: Types match
   Handle<Mesh> h1 = mesh_handle;
   auto mesh = asset_manager.GetAsset(h1);  // Returns Mesh*
   
   // Unsafe: Types don't match
   Handle<Texture> h2 = mesh_handle;        // ERROR: Type mismatch
   ```

2. **Avoids String Keys or void* Pointers**
   ```cpp
   // Bad: Using strings
   auto asset = asset_manager.Get("mesh_1");  // Returns void*? What type?
   
   // Good: Using typed handles
   auto mesh = asset_manager.GetAsset(mesh_handle);  // Returns Mesh*
   ```

3. **Enables Compile-Time Asset Type Checking**
   ```cpp
   void RenderMesh(Handle<Mesh> mesh) {
       // Guaranteed to be a Mesh, not a Texture or Shader
   }
   
   RenderMesh(mesh_handle);         // OK
   RenderMesh(texture_handle);      // ERROR at compile time
   ```

## Memory Management

### Custom Allocators

```cpp
using namespace gws::containers;
using namespace gws::memory;

GeneralAllocator my_alloc;

// Use with custom allocator
AllocVector<int> vec(&my_alloc);
vec.push_back(1);  // Uses my_alloc

// Or just use standard STL
Vector<int> vec2;  // Uses std::allocator
```

### Stack Allocator Integration

For per-frame allocations:

```cpp
StackAllocator frame_allocator(1024 * 1024);  // 1MB per frame

AllocVector<Entity> frame_entities(&frame_allocator);
// ... work with entities ...
frame_allocator.Clear();  // Clear all allocations at frame end
```

## Performance Characteristics

| Container | Insert | Lookup | Erase | Memory |
|-----------|--------|--------|-------|--------|
| DynamicArray | O(1) amortized | O(n) | O(n) | 1.5x capacity |
| HashMap | O(1) average | O(1) average | O(1) average | ~Load factor × elements |
| RingBuffer | O(1) | O(1) | N/A (fixed) | Exact capacity |
| ObjectPool | O(1) | O(1) | O(1) | Allocated size |

## Design Patterns

### Pattern 1: Safe HashMap Access

```cpp
// Bad: Can create unintended entries
if (map["key"].value > 10) { }  // Creates "key" if missing

// Good: Explicit checking
if (auto ptr = map.Find("key")) {
    if (ptr->value > 10) { }    // No side effects
}

// Also good: Get pattern
Value val;
if (map.Get("key", val)) {
    if (val.value > 10) { }     // No side effects
}
```

### Pattern 2: DynamicArray as Vertex/Index Buffer

```cpp
DynamicArray<Vertex> vertices;
vertices.Push(Vertex{... position ...});
vertices.Push(Vertex{... position ...});

// For GPU upload
const Vertex* vertex_data = vertices.Data();
size_t vertex_count = vertices.Size();
glBufferData(GL_ARRAY_BUFFER, vertex_count * sizeof(Vertex), vertex_data, GL_STATIC_DRAW);
```

### Pattern 3: HashMap for Entity Component Lookup

```cpp
HashMap<EntityID, TransformComponent> transforms;
HashMap<EntityID, RenderComponent> renderers;

// Get component for entity
if (auto transform = transforms.Find(entity_id)) {
    transform->position += velocity * delta_time;
}
```

### Pattern 4: RingBuffer for Debug History

```cpp
RingBuffer<DebugFrame> frame_history(60);  // Last 60 frames

void UpdateDebugFrame() {
    frame_history.Push(DebugFrame{
        fps: 1.0f / delta_time,
        draw_calls: draw_call_count,
        // ...
    });
}

void RenderDebugUI() {
    for (const auto& frame : frame_history) {
        // Render graph
    }
}
```

## Phase Roadmap

### Phase 1 (Current) ✓
- DynamicArray wrapper implementation
- HashMap wrapper with safe access patterns
- Handle<T> type-safe asset references
- RingBuffer for fixed-size buffers
- ObjectPool for entity allocation
- Custom allocator support

### Phase 2
- HashMap specialization for ID lookups
- Graph data structure (DAG for scene hierarchy)
- String interning (StringID concept)

### Phase 3+
- Spatial index structures (octree, quadtree)
- Sorted containers (SortedArray, BTree)
- Lock-free queue for multi-threaded systems

## Summary

The container system provides type-safe, zero-overhead abstractions that:

1. **Improve Code Clarity** — `Push(value)` vs `push_back(value)`
2. **Prevent Errors** — Type-checked handles prevent asset confusion
3. **Support Custom Memory** — Allocator-aware templates
4. **Maintain Performance** — Thin wrappers, no virtual calls
5. **Enable Game Patterns** — Pools, rings, and handles for common use cases
