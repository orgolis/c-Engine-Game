# Mesh & Geometry System

## Overview

The Mesh and Geometry systems provide comprehensive tools for managing 3D geometry in the GameWorldshaper engine. The Mesh system handles GPU resources (vertices, indices, rendering), while the Geometry system provides advanced utilities for manipulation, optimization, and analysis of 3D data.

## Architecture

```
┌─────────────────────────────────┐
│  Game/Editor Mesh Management    │
└──────────────┬──────────────────┘
               │
    ┌──────────▼──────────┐
    │    Mesh System      │
    │  (GPU-managed)      │
    ├─────────────────────┤
    │ - Mesh class        │
    │ - Vertex formats    │
    │ - Primitive gen     │
    │ - Bounds calc       │
    └──────────┬──────────┘
               │
    ┌──────────▼──────────────────┐
    │  Geometry System             │
    │ (CPU-side utilities)         │
    ├──────────────────────────────┤
    │ - Mesh queries               │
    │ - Normal/tangent calc        │
    │ - Optimization               │
    │ - Transformation             │
    │ - LOD generation             │
    │ - Procedural generation      │
    └──────────────────────────────┘
```

## Mesh System

### Vertex Formats

Multiple vertex structures support different use cases:

```cpp
// Basic vertex (8 bytes)
struct Vertex {
    glm::vec3 position;    // 12 bytes
    glm::vec4 color;       // 16 bytes
    // Total: 28 bytes
};

// Position + Normal (24 bytes)
struct VertexPN {
    glm::vec3 position;    // 12 bytes
    glm::vec3 normal;      // 12 bytes
    // Total: 24 bytes
};

// Position + Normal + UV (28 bytes)
struct VertexPNUV {
    glm::vec3 position;    // 12 bytes
    glm::vec3 normal;      // 12 bytes
    glm::vec2 uv;          // 8 bytes
    // Total: 32 bytes (aligned)
};

// Full vertex with normals, tangents, UV (64 bytes) - for normal mapping
struct VertexPNTUV {
    glm::vec3 position;    // 12 bytes
    glm::vec3 normal;      // 12 bytes
    glm::vec4 tangent;     // 16 bytes (w = handedness)
    glm::vec2 uv;          // 8 bytes
    // Total: 48 bytes
};
```

### Mesh Usage

```cpp
#include "graphics.h"

using namespace schizo::graphics;

// Create renderer
auto device = RenderDevice::Create();
device->Initialize();
auto renderer = Renderer::Create();
renderer->Initialize();

// Create primitive mesh
auto cube = Mesh::CreateCube(device.get());

// Create custom mesh
std::vector<Vertex> vertices = {
    { glm::vec3(-1,-1,-1), glm::vec4(1,0,0,1) },
    { glm::vec3( 1,-1,-1), glm::vec4(0,1,0,1) },
    { glm::vec3( 1, 1,-1), glm::vec4(0,0,1,1) },
};
std::vector<uint32_t> indices = { 0, 1, 2 };

auto mesh = std::make_unique<Mesh>();
mesh->SetData(vertices, indices);

// Get bounds
mesh->CalculateBounds();
auto center = mesh->GetBoundsCenter();
auto radius = mesh->GetBoundsRadius();

// Render
renderer->BeginFrame(camera);
mesh->Draw(device.get());
renderer->EndFrame();
```

### Built-in Primitives

#### Triangle
```cpp
auto triangle = Mesh::CreateTriangle(device.get());
```

#### Cube
```cpp
auto cube = Mesh::CreateCube(device.get());
```

#### Grid/Plane
```cpp
// Creates width x height grid
auto grid = Mesh::CreateGrid(device.get(), 10, 10);
```

#### Sphere
```cpp
// UV sphere - segments around equator, rings from pole to pole
auto sphere = Mesh::CreateSphere(device.get(), 1.0f, 32, 16);
```

#### Torus
```cpp
auto torus = Mesh::CreateTorus(device.get(), 2.0f, 0.5f, 32, 16);
// major_radius = 2.0f, minor_radius = 0.5f
```

#### Cylinder
```cpp
auto cylinder = Mesh::CreateCylinder(device.get(), 1.0f, 2.0f, 32, true);
// radius = 1.0f, height = 2.0f, segments = 32, include_caps = true
```

#### Capsule
```cpp
auto capsule = Mesh::CreateCapsule(device.get(), 0.5f, 2.0f, 16, 8);
// Cylinder with hemispherical caps
```

#### Cone
```cpp
auto cone = Mesh::CreateCone(device.get(), 1.0f, 2.0f, 32, true);
// radius = 1.0f, height = 2.0f, include base cap
```

#### Pyramid
```cpp
auto pyramid = Mesh::CreatePyramid(device.get(), 2.0f, 3.0f);
// base_size = 2.0f, height = 3.0f
```

#### Icosphere
```cpp
auto icosphere = Mesh::CreateIcosphere(device.get(), 1.0f, 3);
// radius = 1.0f, subdivisions = 3
// More uniform than UV sphere, better for LOD
```

## Geometry System

The Geometry system provides CPU-side utilities for 3D data manipulation and analysis.

### Bounding Volume Queries

```cpp
using namespace schizo::geometry;

// Bounding box
BoundingBox bbox = CalculateMeshBounds(positions);
glm::vec3 center = bbox.GetCenter();
glm::vec3 extents = bbox.GetExtents();
float radius = bbox.GetRadius();

// Bounding sphere (Welzl's algorithm)
Sphere sphere = CalculateMeshBoundingSphere(positions);

// Check containment
bool contains = bbox.Contains(point);
bool intersects = bbox.Intersects(other_box);

// Sphere operations
bool sphere_contains = sphere.Contains(point);
bool spheres_intersect = sphere.Intersects(other_sphere);
```

### Mesh Queries

```cpp
// Surface area
float area = CalculateMeshSurfaceArea(positions, indices);

// Volume (for closed meshes)
float volume = CalculateMeshVolume(positions, indices);

// Center of mass
glm::vec3 com = CalculateMeshCenterOfMass(positions, indices);

// Check if mesh is closed
if (IsMeshClosed(positions, indices)) {
    // Can calculate volume safely
}

// Check if mesh is manifold
if (IsMeshManifold(indices)) {
    // Every edge shared by exactly 2 triangles
}
```

### Normal and Tangent Calculation

```cpp
// Flat normals (per-face)
std::vector<glm::vec3> face_normals = CalculateFaceNormals(positions, indices);

// Vertex normals (simple average)
std::vector<glm::vec3> normals = CalculateNormals(positions, indices);

// Smoothed normals (area-weighted)
std::vector<glm::vec3> smooth_normals = CalculateSmoothedNormals(positions, indices);

// Angle-weighted normals (best quality)
std::vector<glm::vec3> angle_weighted = CalculateAngleWeightedNormals(positions, indices);

// Tangent space (for normal mapping)
std::vector<glm::vec4> tangents;
std::vector<glm::vec3> bitangents;
CalculateTangents(tangents, bitangents, positions, normals, texcoords, indices);
// tangents[i].w = handedness (-1 or 1)
```

### Mesh Optimization

```cpp
// Weld identical vertices
WeldInfo weld = WeldVertices(positions, indices, 0.0001f);
std::vector<uint32_t> optimized_indices = weld.new_indices;

// Remove degenerate triangles
std::vector<uint32_t> clean_indices = RemoveDegenerateTriangles(positions, indices);

// Simplify mesh
std::vector<uint32_t> simplified = SimplifyMesh(positions, indices, 0.5f);
// 0.5 = 50% reduction target

// Generate LOD levels
std::vector<MeshLOD> lods = GenerateLODLevels(positions, indices, 4);
// Creates 100%, 75%, 50%, 25% LODs
for (const auto& lod : lods) {
    printf("LOD %d: %u triangles\n", lod_index, lod.indices.size() / 3);
}
```

### Mesh Transformation

```cpp
// Apply matrix transformation
glm::mat4 transform = glm::translate(glm::mat4(1), glm::vec3(1, 2, 3));
TransformPositions(positions, transform);

// Transform with normal recalculation
TransformPositionsAndNormals(positions, normals, transform);

// Scale uniformly
ScaleMesh(positions, 2.0f);  // 2x larger

// Center at origin
CenterMesh(positions);

// Normalize to unit bounds
NormalizeMesh(positions);  // Scales to fit in [-1, 1]^3

// Invert winding (flip normals)
InvertWinding(indices);
```

### Quality Metrics

```cpp
MeshQuality quality = CalculateMeshQuality(positions, indices);

printf("Edge length: %.3f - %.3f\n", 
       quality.min_edge_length, quality.max_edge_length);
printf("Avg triangle area: %.3f\n", quality.avg_triangle_area);
printf("Aspect ratio: %.3f (1.0 = equilateral)\n", quality.aspect_ratio);
```

### Procedural Generation

#### Terrain from Heightmap
```cpp
std::vector<glm::vec3> terrain_positions;
std::vector<uint32_t> terrain_indices;

std::vector<float> heightmap(100 * 100);
// ... fill heightmap ...

GenerateTerrainMesh(terrain_positions, terrain_indices,
                    100, 100,          // width, height
                    heightmap,
                    1.0f);             // vertical scale
```

#### Noise-based Terrain
```cpp
GenerateNoiseTerrain(positions, indices,
                     200, 200,         // grid size
                     5.0f,             // scale
                     0.1f,             // frequency
                     1.0f);            // amplitude
```

#### Tube Along Curve
```cpp
std::vector<glm::vec3> curve = {
    glm::vec3(0, 0, 0),
    glm::vec3(5, 2, 0),
    glm::vec3(10, 0, 0),
};

std::vector<glm::vec3> tube_pos;
std::vector<uint32_t> tube_idx;

GenerateTube(tube_pos, tube_idx,
            curve,
            0.5f,              // radius
            16);               // segments around tube
```

## Usage Examples

### Example 1: Mesh with Normal Mapping

```cpp
// Create mesh with normal mapping support
std::vector<VertexPNTUV> vertices = {
    { pos0, normal0, tangent0, uv0 },
    { pos1, normal1, tangent1, uv1 },
    { pos2, normal2, tangent2, uv2 },
};
std::vector<uint32_t> indices = { 0, 1, 2 };

auto mesh = std::make_unique<Mesh>();
mesh->SetDataPNTUV(vertices, indices);
mesh->CalculateBounds();

// Use with material
auto material = Material::CreatePBR(device.get(), 
                                   albedo, normal, 
                                   metallic, roughness, ao);

renderer->BeginFrame(camera);
material->Bind(device.get());
mesh->Draw(device.get());
material->Unbind(device.get());
renderer->EndFrame();
```

### Example 2: LOD System

```cpp
// Generate LOD levels
auto lods = GenerateLODLevels(positions, indices, 4);

// Store in array for runtime selection
struct MeshAsset {
    std::vector<std::unique_ptr<Mesh>> lod_meshes;
    
    void LoadLODs(RenderDevice* device, const std::vector<MeshLOD>& lod_data) {
        for (const auto& lod : lod_data) {
            auto mesh = std::make_unique<Mesh>();
            mesh->SetData(lod.positions, lod.indices);
            mesh->CalculateBounds();
            lod_meshes.push_back(std::move(mesh));
        }
    }
    
    Mesh* GetLOD(float distance, float max_distance) {
        float ratio = distance / max_distance;
        int lod_index = static_cast<int>(ratio * lod_meshes.size());
        lod_index = std::min(lod_index, (int)lod_meshes.size() - 1);
        return lod_meshes[lod_index].get();
    }
};
```

### Example 3: Mesh Optimization Pipeline

```cpp
void OptimizeMesh(std::vector<glm::vec3>& positions,
                  std::vector<uint32_t>& indices) {
    // Step 1: Remove degenerate triangles
    indices = RemoveDegenerateTriangles(positions, indices);
    
    // Step 2: Weld identical vertices
    auto weld_info = WeldVertices(positions, indices, 0.001f);
    indices = weld_info.new_indices;
    
    // Step 3: Check quality
    auto quality = CalculateMeshQuality(positions, indices);
    spdlog::info("Mesh quality - Edge: {:.3f}-{:.3f}, Aspect: {:.3f}",
                 quality.min_edge_length, quality.max_edge_length,
                 quality.aspect_ratio);
    
    // Step 4: Report
    spdlog::info("Optimized: {} vertices, {} triangles",
                 positions.size(), indices.size() / 3);
}
```

### Example 4: Procedural Terrain

```cpp
void CreateProceduralTerrain(RenderDevice* device,
                            uint32_t grid_size) {
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
    
    // Generate noise-based terrain
    GenerateNoiseTerrain(positions, indices,
                        grid_size, grid_size,
                        10.0f,     // horizontal scale
                        0.05f,     // frequency
                        5.0f);     // height variation
    
    // Calculate normals
    auto normals = CalculateSmoothedNormals(positions, indices);
    
    // Create mesh
    std::vector<VertexPN> vertices;
    for (size_t i = 0; i < positions.size(); ++i) {
        vertices.emplace_back(positions[i], normals[i]);
    }
    
    auto terrain_mesh = std::make_unique<Mesh>();
    terrain_mesh->SetDataPN(vertices, indices);
    terrain_mesh->CalculateBounds();
    
    // Render terrain
    auto terrain_material = Material::CreateSolidColor(device, glm::vec4(0.2f, 0.6f, 0.2f, 1));
    
    // ... render terrain_mesh with terrain_material ...
}
```

## Performance Considerations

### Mesh Creation
- Prefer using built-in primitives (pre-optimized)
- Use vertex welding for custom meshes
- Calculate bounds for frustum culling

### Normal Calculation
- Fast: `CalculateNormals()` - simple average
- Better: `CalculateSmoothedNormals()` - area-weighted
- Best: `CalculateAngleWeightedNormals()` - geometric quality

### LOD Selection
- Use distance to camera for LOD selection
- Precompute LOD levels offline
- Consider view frustum size for dynamic LOD

### Memory Usage
- Vertex format size matters (8-48 bytes per vertex)
- Index list: 4 bytes per index
- Store shared vertices, not duplicated

### GPU Upload
- Upload happens automatically when needed
- For dynamic meshes, update vertex data selectively
- Use UpdateBuffer for frequent changes

## Best Practices

1. **Use appropriate vertex format**
   - `Vertex`: Debugging, simple visualization
   - `VertexPN`: Lighting calculations
   - `VertexPNUV`: Textured geometry
   - `VertexPNTUV`: Normal-mapped surfaces

2. **Always calculate bounds**
   - Enables frustum culling
   - Needed for shadow map fitting
   - Required for physics collision

3. **Validate mesh topology**
   - Check `IsMeshClosed()` before volume calculation
   - Check `IsMeshManifold()` for physics
   - Remove degenerate triangles

4. **Use LOD for large scenes**
   - Generate at asset load time
   - Store in asset bundles
   - Switch based on distance + screen coverage

5. **Optimize in pipeline order**
   - Weld vertices first
   - Remove degenerates second
   - Calculate normals last
   - Then simplify/create LODs

## Future Enhancements

1. GPU-based mesh processing
2. Streaming mesh data
3. Adaptive tessellation
4. Real-time mesh generation
5. Physics-aware simplification
6. Texture atlas generation
7. Mesh baking utilities
