# Scene Management Quick Reference

## Scene Creation

```cpp
using namespace schizo::scene;

SceneConfig config;
config.name = "MainScene";
config.use_spatial_partitioning = true;
config.gravity = {0, -9.81f, 0};

auto scene = Scene::Create(config);
scene->Initialize();
```

## Entity Creation

```cpp
// Create entity
auto player = scene->CreateEntity("Player");

// Set position
player->GetTransform()->SetLocalPosition({0, 1, 0});

// Set tags for grouping
player->SetTag("player");

// Get entity back
auto found = scene->GetEntityByName("Player");
```

## Entity Hierarchy

```cpp
// Create parent and child
auto parent = scene->CreateEntity("Parent");
auto child = scene->CreateEntity("Child");

// Attach child to parent
child->SetParent(parent);

// Access hierarchy
const auto& children = parent->GetChildren();
auto p = child->GetParent();
auto root_child = parent->FindChild("Child");

// Detach
child->Detach();
```

## Transform Operations

```cpp
auto transform = entity->GetTransform();

// Local (relative to parent)
transform->SetLocalPosition({0, 1, 0});
transform->SetLocalRotation(glm::quat{1, 0, 0, 0});
transform->SetLocalScale({1, 1, 1});

// World (absolute)
auto world_pos = transform->GetWorldPosition();
auto world_rot = transform->GetWorldRotation();
auto world_scale = transform->GetWorldScale();

// Matrices
auto local_mat = transform->GetLocalMatrix();
auto world_mat = transform->GetWorldMatrix();

// Direction vectors
auto forward = transform->GetForward();   // -Z
auto right = transform->GetRight();       // +X
auto up = transform->GetUp();             // +Y

// Utilities
transform->Translate({1, 0, 0});           // Local translation
transform->TranslateWorld({1, 0, 0});      // World translation
transform->Rotate({0, 1, 0}, 3.14f/2);    // Rotate 90° around Y
transform->LookAt({0, 0, 0});              // Look at point
```

## Components

```cpp
// Custom component
class MyComponent : public Component {
public:
    void OnAttach() override { /* Initialized */ }
    void OnUpdate(float dt) override { /* Logic */ }
};

// Add component
auto comp = entity->AddComponent<MyComponent>();

// Get component
auto my_comp = entity->GetComponent<MyComponent>();

// Get all of type
auto comps = entity->GetComponents<MyComponent>();

// Remove component
entity->RemoveComponent(comp);
```

## Entity Lifecycle

```cpp
// Check activity
bool is_active = entity->IsActive();
bool active_in_hierarchy = entity->IsActiveInHierarchy();

// Control activity
entity->SetActive(true);   // Enable (children inherit)
entity->SetActive(false);  // Disable (children inherit)

// Destroy (end of frame)
entity->Destroy();

// Check state
if (entity->IsDestroyed()) {
    // Will be removed
}
```

## Querying Entities

```cpp
// By ID
auto entity = scene->GetEntityById(123);

// By name
auto entity = scene->GetEntityByName("Player");

// By tag
auto enemies = scene->GetEntitiesByTag("enemy");

// All entities
const auto& entities = scene->GetEntities();

// Count
uint32_t count = scene->GetEntityCount();
```

## Spatial Queries

```cpp
// AABB query
auto in_box = scene->QueryAABB({-50, 0, -50}, {50, 10, 50});

// Sphere query
auto in_sphere = scene->QuerySphere({0, 0, 0}, 25.0f);

// Frustum query
auto visible = scene->QueryFrustum(view_proj_matrix);
```

## Scene Lifecycle

```cpp
// Initialize (once at start)
scene->Initialize();

// Update (every frame)
scene->Update(delta_time);

// Reload (reset scene)
scene->Reload();

// Clear all entities
scene->Clear();

// Shutdown (cleanup)
scene->Shutdown();
```

## Scene Statistics

```cpp
auto stats = scene->GetStats();

std::cout << "Entities: " << stats.entity_count << std::endl;
std::cout << "Active: " << stats.active_entity_count << std::endl;
std::cout << "Components: " << stats.component_count << std::endl;
std::cout << "Octree nodes: " << stats.octree_nodes << std::endl;
std::cout << "Octree depth: " << stats.octree_depth << std::endl;
```

## Debugging

```cpp
// Print hierarchy
scene->PrintHierarchy();

// Entity info
std::cout << "Name: " << entity->GetName() << std::endl;
std::cout << "ID: " << entity->GetId() << std::endl;
std::cout << "Position: " << entity->GetTransform()->GetWorldPosition() << std::endl;

// Component info
auto comps = entity->GetComponents();
std::cout << "Components: " << comps.size() << std::endl;
```

## Component Lifecycle Events

```cpp
class MyComponent : public Component {
    void OnAttach() override {
        // Called when added to entity
    }
    
    void OnDetach() override {
        // Called when removed from entity
    }
    
    void OnEnable() override {
        // Called when entity becomes active
    }
    
    void OnDisable() override {
        // Called when entity becomes inactive
    }
    
    void OnFixedUpdate(float dt) override {
        // Called at fixed timestep (physics)
    }
    
    void OnUpdate(float dt) override {
        // Called at variable timestep (main logic)
    }
    
    void OnLateUpdate(float dt) override {
        // Called after Update
    }
};
```

## Common Patterns

### Player-Weapon Hierarchy
```cpp
auto player = scene->CreateEntity("Player");
auto weapon = scene->CreateEntity("Sword");
weapon->SetParent(player);

// Weapon follows player automatically
weapon->GetTransform()->SetLocalPosition({0.5f, 0, 0});
```

### Tag-Based Grouping
```cpp
// Create enemies
for (int i = 0; i < 10; ++i) {
    auto enemy = scene->CreateEntity("Enemy_" + std::to_string(i));
    enemy->SetTag("enemy");
}

// Update all enemies
auto enemies = scene->GetEntitiesByTag("enemy");
for (const auto& enemy : enemies) {
    // Update logic
}
```

### Spatial Culling
```cpp
// Query visible objects
auto camera_pos = camera->GetTransform()->GetWorldPosition();
auto visible = scene->QuerySphere(camera_pos, 100.0f);

for (const auto& obj : visible) {
    if (obj->IsActiveInHierarchy()) {
        // Render object
    }
}
```

### Active/Inactive Toggle
```cpp
// Show UI
ui_panel->SetActive(true);

// Hide all enemies
auto enemies = scene->GetEntitiesByTag("enemy");
for (const auto& enemy : enemies) {
    enemy->SetActive(false);  // Also disables children
}
```

## Configuration Options

```cpp
struct SceneConfig {
    std::string name = "DefaultScene";
    
    // Spatial partitioning
    bool use_spatial_partitioning = true;
    glm::vec3 world_bounds_min = {-1000, -100, -1000};
    glm::vec3 world_bounds_max = {1000, 500, 1000};
    uint32_t octree_max_depth = 8;
    uint32_t octree_objects_per_node = 8;
    
    // Physics
    glm::vec3 gravity = {0, -9.81f, 0};
    
    // Timing
    float fixed_timestep = 0.02f;   // 50 FPS
    float max_frame_time = 0.1f;    // Cap frame time
};
```

## Header Locations

- `engine/scene/include/transform.h`
- `engine/scene/include/entity.h`
- `engine/scene/include/scene.h`
- `engine/scene/include/octree.h`

## Namespace
All scene classes are in `schizo::scene`
