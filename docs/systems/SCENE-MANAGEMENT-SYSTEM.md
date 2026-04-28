# Scene Management System

## Overview

The Scene Management System provides a complete solution for organizing, updating, and querying game objects (entities) in a 3D world. It includes hierarchical transforms, component-based architecture, and spatial partitioning for efficient spatial queries.

## Architecture

### Core Components

```
Scene
  ├── Entity A
  │   ├── Transform (position, rotation, scale)
  │   ├── MeshRenderer (component)
  │   ├── RigidBody (component)
  │   └── Child Entity A1
  │       ├── Transform
  │       └── Light (component)
  │
  └── Entity B
      ├── Transform
      ├── Camera (component)
      └── AudioListener (component)

Spatial Partitioning (Octree)
  └── Accelerates spatial queries (AABB, Sphere)
```

### Transform Hierarchy

Entities have **local transforms** (relative to parent) and **world transforms** (absolute in world space):

```cpp
Entity root{"Root"};
Entity child{"Child"};

child.SetParent(root.shared_from_this());
child.GetTransform()->SetLocalPosition({1, 0, 0});

// World position = parent world + local offset
auto world_pos = child.GetTransform()->GetWorldPosition();
```

## Core Classes

### 1. Transform

3D transform with position, rotation, and scale:

```cpp
class Transform {
    // Local (relative to parent)
    void SetLocalPosition(const glm::vec3& position);
    void SetLocalRotation(const glm::quat& rotation);
    void SetLocalScale(const glm::vec3& scale);
    
    // World (absolute)
    glm::vec3 GetWorldPosition() const;
    glm::quat GetWorldRotation() const;
    glm::vec3 GetWorldScale() const;
    
    // Matrices
    glm::mat4 GetLocalMatrix() const;
    glm::mat4 GetWorldMatrix() const;
    
    // Direction vectors
    glm::vec3 GetForward() const;    // -Z axis
    glm::vec3 GetRight() const;      // +X axis
    glm::vec3 GetUp() const;         // +Y axis
    
    // Hierarchy
    void SetParent(Transform* parent);
    
    // Utilities
    void Translate(const glm::vec3& translation);
    void Rotate(const glm::vec3& axis, float angle);
    void LookAt(const glm::vec3& target, const glm::vec3& up);
};
```

### 2. Component

Base class for all entity behaviors:

```cpp
class Component {
    virtual void OnAttach() {}      // When added to entity
    virtual void OnDetach() {}      // When removed from entity
    virtual void OnEnable() {}      // When entity becomes active
    virtual void OnDisable() {}     // When entity becomes inactive
    virtual void OnFixedUpdate(float dt) {}  // Physics timestep
    virtual void OnUpdate(float dt) {}       // Variable timestep
    virtual void OnLateUpdate(float dt) {}   // Late phase
    
    class Entity* GetEntity() const;
};
```

Example custom component:

```cpp
class MyComponent : public Component {
public:
    void OnUpdate(float delta_time) override {
        auto entity = GetEntity();
        auto transform = entity->GetTransform();
        transform->Rotate({0, 1, 0}, 45.0f * delta_time);
    }
};
```

### 3. Entity

Game object with transform, components, and hierarchy:

```cpp
class Entity {
    // Identification
    uint32_t GetId() const;
    const std::string& GetName() const;
    const std::string& GetTag() const;
    
    // Transform
    Transform* GetTransform();
    
    // Hierarchy
    void SetParent(std::shared_ptr<Entity> parent);
    void AddChild(std::shared_ptr<Entity> child);
    const std::vector<std::shared_ptr<Entity>>& GetChildren() const;
    std::shared_ptr<Entity> FindChild(const std::string& name);
    
    // Components
    template<typename T, typename... Args>
    std::shared_ptr<T> AddComponent(Args&&... args);
    
    template<typename T>
    std::shared_ptr<T> GetComponent();
    
    template<typename T>
    std::vector<std::shared_ptr<T>> GetComponents();
    
    void RemoveComponent(std::shared_ptr<Component> component);
    
    // Lifecycle
    bool IsActive() const;
    void SetActive(bool active);
    bool IsActiveInHierarchy() const;
    
    void Destroy();
};
```

### 4. Scene

Manages all entities with update coordination and spatial queries:

```cpp
class Scene {
    // Creation
    static std::shared_ptr<Scene> Create(const SceneConfig& config);
    
    // Entity management
    std::shared_ptr<Entity> CreateEntity(const std::string& name);
    void AddEntity(std::shared_ptr<Entity> entity);
    void RemoveEntity(std::shared_ptr<Entity> entity);
    
    std::shared_ptr<Entity> GetEntityById(uint32_t id);
    std::shared_ptr<Entity> GetEntityByName(const std::string& name);
    std::vector<std::shared_ptr<Entity>> GetEntitiesByTag(const std::string& tag);
    
    // Spatial queries
    std::vector<std::shared_ptr<Entity>> QueryAABB(const glm::vec3& min, const glm::vec3& max);
    std::vector<std::shared_ptr<Entity>> QuerySphere(const glm::vec3& center, float radius);
    
    // Lifecycle
    void Initialize();
    void Update(float delta_time);
    void Shutdown();
    void Clear();
    
    // Statistics
    Stats GetStats() const;
};
```

### 5. Octree

Spatial partitioning for efficient spatial queries:

```cpp
class Octree {
    void Insert(std::shared_ptr<Entity> entity);
    void Remove(std::shared_ptr<Entity> entity);
    
    std::vector<std::shared_ptr<Entity>> QueryAABB(const glm::vec3& min, const glm::vec3& max);
    std::vector<std::shared_ptr<Entity>> QuerySphere(const glm::vec3& center, float radius);
    
    Stats GetStats() const;
};
```

## Usage Examples

### Creating a Scene

```cpp
using namespace schizo::scene;

SceneConfig config;
config.name = "MainScene";
config.world_bounds_min = {-1000, -100, -1000};
config.world_bounds_max = {1000, 500, 1000};
config.gravity = {0, -9.81f, 0};

auto scene = Scene::Create(config);
scene->Initialize();
```

### Creating Entities and Hierarchy

```cpp
// Create root entity
auto player = scene->CreateEntity("Player");
player->SetTag("player");
player->GetTransform()->SetLocalPosition({0, 1, 0});

// Create child (weapon attached to player)
auto weapon = scene->CreateEntity("Weapon");
weapon->SetParent(player);
weapon->GetTransform()->SetLocalPosition({1, 0, 0});  // 1 unit to the right

// Find entity
auto found_player = scene->GetEntityByName("Player");
```

### Adding Components

```cpp
// Add custom component
class MovementComponent : public Component {
public:
    void OnUpdate(float dt) override {
        auto entity = GetEntity();
        entity->GetTransform()->Translate({1, 0, 0} * dt);
    }
};

auto movement = player->AddComponent<MovementComponent>();
auto movement2 = player->GetComponent<MovementComponent>();
```

### Updating Scene

```cpp
// In main game loop
float delta_time = 0.016f;  // 60 FPS

// Preferred way: scene handles all updates
scene->Update(delta_time);  // Updates all entities and components

// Or manually (not typical)
for (const auto& entity : scene->GetEntities()) {
    entity->Update(delta_time);
}
```

### Spatial Queries

```cpp
// Find entities in box
auto aabb_results = scene->QueryAABB({-50, 0, -50}, {50, 10, 50});

// Find entities in sphere
auto sphere_results = scene->QuerySphere({0, 5, 0}, 25.0f);

// Use with octree for culling
for (const auto& entity : sphere_results) {
    // Keep only active entities
    if (entity->IsActiveInHierarchy()) {
        // Process entity
    }
}
```

### Entity Lifecycle

```cpp
// Disable entity (all children also disabled)
player->SetActive(false);

// Re-enable
player->SetActive(true);

// Destroy (removed at end of frame)
player->Destroy();

// Check status
if (player->IsActiveInHierarchy()) {
    // Entity is active and all parents are active
}
```

### Scene Statistics

```cpp
auto stats = scene->GetStats();

std::cout << "Entities: " << stats.entity_count << std::endl;
std::cout << "Active: " << stats.active_entity_count << std::endl;
std::cout << "Components: " << stats.component_count << std::endl;
std::cout << "Octree nodes: " << stats.octree_nodes << std::endl;
std::cout << "Octree depth: " << stats.octree_depth << std::endl;
```

## Performance Optimization

### Spatial Partitioning

The octree automatically partitions entities for efficient spatial queries:

```cpp
// Configure octree
SceneConfig config;
config.use_spatial_partitioning = true;
config.octree_max_depth = 8;           // Max 2^8 = 256 subdivisions
config.octree_objects_per_node = 8;    // Split when > 8 objects
```

### Per-Object Culling

Use spatial queries to cull objects:

```cpp
// Query camera frustum
auto visible = scene->QueryFrustum(view_proj_matrix);

// Or use bounding sphere
auto player_pos = player->GetTransform()->GetWorldPosition();
auto nearby = scene->QuerySphere(player_pos, 100.0f);

for (const auto& entity : nearby) {
    // Only render nearby entities
}
```

### Lazy Updates

Mark entities as needing update only when necessary:

```cpp
// Most entities don't move - don't update every frame
if (entity_moved) {
    entity->GetTransform()->SetWorldPosition(new_pos);
}
```

## Update Phases

Scene updates happen in strict phases each frame:

1. **Fixed Update** — Physics simulation (fixed timestep, repeats if needed)
2. **Update** — Variable timestep (normal game logic)
3. **Late Update** — Post-processing, camera positioning
4. **Spatial Partitioning** — Rebuild octree for spatial queries
5. **Destruction** — Remove entities marked for destruction

```cpp
// Component lifecycle per frame
component->OnFixedUpdate(0.02f);   // May be called multiple times
component->OnUpdate(0.016f);        // Variable timestep
component->OnLateUpdate(0.016f);    // After all updates
```

## Best Practices

1. **Use Tags for Categories** — Group entities logically
   ```cpp
   entity->SetTag("enemy");
   auto enemies = scene->GetEntitiesByTag("enemy");
   ```

2. **Parent Expensive Queries** — Cache results when possible
   ```cpp
   auto nearby_objects = scene->QuerySphere(center, radius);
   // Reuse for multiple frames if objects aren't moving
   ```

3. **Organize Hierarchy** — Use parent-child relationships
   ```cpp
   // Weapon attached to player
   weapon->SetParent(player);
   // Rotating player also rotates weapon
   ```

4. **Active/Inactive for Optimization** — Disable when off-screen
   ```cpp
   entity->SetActive(false);  // Skips all updates
   ```

5. **Use Find/Query Methods** — Avoid manual loops
   ```cpp
   // Good: O(1) lookup
   auto entity = scene->GetEntityById(id);
   
   // OK: O(n) linear search
   auto entity = scene->GetEntityByName("Player");
   
   // Bad: Manual iteration
   // for (const auto& e : scene->GetEntities()) { ... }
   ```

6. **Component Composition** — Use multiple components
   ```cpp
   player->AddComponent<MeshRenderer>();
   player->AddComponent<CollisionBox>();
   player->AddComponent<PlayerController>();
   ```

## Memory Considerations

- **Entity ID**: 4 bytes (uint32_t)
- **Transform**: ~100 bytes (with caching)
- **Component**: 8-16 bytes (pointer overhead) + data
- **Octree Node**: ~50-100 bytes each

For a scene with 10,000 entities:
- Base overhead: ~1-2 MB
- Per-component: varies (10-100 bytes average)
- Octree: ~10-20 MB

## Debugging

### Print Scene Hierarchy

```cpp
scene->PrintHierarchy();

// Output:
// [1] Player (Active: 1)
//   [2] Weapon (Active: 1)
//   [3] Camera (Active: 1)
// [4] Enemy_1 (Active: 1)
//   [5] Healthbar (Active: 1)
```

### Get Entity Information

```cpp
auto entity = scene->GetEntityByName("Player");

std::cout << "Name: " << entity->GetName() << std::endl;
std::cout << "ID: " << entity->GetId() << std::endl;
std::cout << "Tag: " << entity->GetTag() << std::endl;
std::cout << "Position: " << entity->GetTransform()->GetWorldPosition() << std::endl;
std::cout << "Children: " << entity->GetChildren().size() << std::endl;
std::cout << "Components: " << entity->GetComponents().size() << std::endl;
```

## Future Enhancements

- Lazy component initialization
- Component dependency system
- Signal/event system
- Serialization (save/load scenes)
- Prefab system
- Pooled entity allocation
- Physics integration hooks
- Advanced frustum culling
