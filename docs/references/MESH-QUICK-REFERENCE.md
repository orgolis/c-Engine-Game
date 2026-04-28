# Mesh & Geometry Quick Reference

## Mesh Creation

### From Built-in Primitives
```cpp
#include "graphics.h"
using namespace schizo::graphics;

auto device = RenderDevice::Create();
device->Initialize();

// Triangle
auto triangle = Mesh::CreateTriangle(device.get());

// Cube
auto cube = Mesh::CreateCube(device.get());

// Grid
auto grid = Mesh::CreateGrid(device.get(), 10, 10);

// UV Sphere (32 segments, 16 rings)
auto sphere = Mesh::CreateSphere(device.get(), 1.0f, 32, 16);

// Torus (major_radius, minor_radius, segments, rings)
auto torus = Mesh::CreateTorus(device.get(), 2.0f, 0.5f, 32, 16);

// Cylinder
auto cylinder = Mesh::CreateCylinder(device.get(), 1.0f, 2.0f, 32, true);

// Capsule (cylinder with hemispherical caps)
auto capsule = Mesh::CreateCapsule(device.get(), 0.5f, 2.0f, 16, 8);

// Cone
auto cone = Mesh::CreateCone(device.get(), 1.0f, 2.0f, 32, true);

// Pyramid
auto pyramid = Mesh::CreatePyramid(device.get(), 2.0f, 3.0f);

// Icosphere (better than UV sphere for LOD)
auto icosphere = Mesh::CreateIcosphere(device.get(), 1.0f, 3);
```

### From Custom Data
```cpp
// Create vertex data
std::vector<Vertex> vertices = {
    { glm::vec3(-1,-1,0), glm::vec4(1,0,0,1) },
    { glm::vec3( 1,-1,0), glm::vec4(0,1,0,1) },
    { glm::vec3( 0, 1,0), glm::vec4(0,0,1,1) },
};
std::vector<uint32_t> indices = { 0, 1, 2 };

// Load into mesh
auto mesh = std::make_unique<Mesh>();
mesh->SetData(vertices, indices);

// For advanced vertex format
std::vector<VertexPNUV> verts;
mesh->SetDataPNUV(verts, indices);

// Calculate bounds
mesh->CalculateBounds();
```

## Mesh Properties

```cpp
uint32_t vertex_count = mesh->GetVertexCount();
uint32_t index_count = mesh->GetIndexCount();

glm::vec3 center = mesh->GetBoundsCenter();
glm::vec3 extents = mesh->GetBoundsExtents();
float radius = mesh->GetBoundsRadius();
```

## Mesh Rendering

```cpp
renderer->BeginFrame(camera);

// Bind material
material->Bind(device.get());

// Draw mesh
mesh->Draw(device.get(), 0x0004);  // GL_TRIANGLES = 0x0004

material->Unbind(device.get());
renderer->EndFrame();
```

## Geometry Queries

### Bounding Volumes
```cpp
using namespace schizo::geometry;

// AABB
BoundingBox bbox = CalculateMeshBounds(positions);
glm::vec3 min = bbox.min;
glm::vec3 max = bbox.max;
glm::vec3 center = bbox.GetCenter();
glm::vec3 extents = bbox.GetExtents();
float radius = bbox.GetRadius();

// Bounding Sphere
Sphere sphere = CalculateMeshBoundingSphere(positions);
glm::vec3 sphere_center = sphere.center;
float sphere_radius = sphere.radius;

// Check intersection
if (bbox.Intersects(other_bbox)) { }
if (sphere.Intersects(other_sphere)) { }
if (bbox.Contains(point)) { }
```

### Mesh Analysis
```cpp
float surface_area = CalculateMeshSurfaceArea(positions, indices);
float volume = CalculateMeshVolume(positions, indices);
glm::vec3 center_of_mass = CalculateMeshCenterOfMass(positions, indices);

bool is_closed = IsMeshClosed(positions, indices);
bool is_manifold = IsMeshManifold(indices);

MeshQuality quality = CalculateMeshQuality(positions, indices);
// quality.min_edge_length, quality.max_edge_length, quality.avg_triangle_area
```

## Normal Calculations

### Vertex Normals
```cpp
// Simple average (fast)
auto normals = CalculateNormals(positions, indices);

// Area-weighted average (better)
auto smooth_normals = CalculateSmoothedNormals(positions, indices);

// Angle-weighted (best)
auto quality_normals = CalculateAngleWeightedNormals(positions, indices);
```

### Face Normals and Tangent Space
```cpp
// Per-face normals
auto face_normals = CalculateFaceNormals(positions, indices);

// Tangent space for normal mapping
std::vector<glm::vec4> tangents;
std::vector<glm::vec3> bitangents;
CalculateTangents(tangents, bitangents,
                 positions, normals, texcoords, indices);
//  tangents[i].w = handedness (-1.0 or 1.0)
```

## Mesh Optimization

### Vertex Welding
```cpp
WeldInfo weld = WeldVertices(positions, indices, 0.001f);
std::vector<uint32_t> optimized = weld.new_indices;
uint32_t removed = weld.removed_count;
```

### Cleanup and Simplification
```cpp
// Remove zero-area triangles
auto clean = RemoveDegenerateTriangles(positions, indices);

// Reduce polygon count
auto simple = SimplifyMesh(positions, indices, 0.5f);  // 50% target

// Generate LOD levels (100%, 75%, 50%, 25%)
auto lods = GenerateLODLevels(positions, indices, 4);
for (size_t i = 0; i < lods.size(); ++i) {
    printf("LOD %d: %u tris\n", i, lods[i].indices.size() / 3);
}
```

## Mesh Transformation

### Matrix Transform
```cpp
glm::mat4 transform = glm::translate(glm::mat4(1), glm::vec3(1, 0, 0));
TransformPositions(positions, transform);

TransformPositionsAndNormals(positions, normals, transform);
```

### Geometry Manipulation
```cpp
// Scale
ScaleMesh(positions, 2.0f);

// Center at origin
CenterMesh(positions);

// Normalize to unit bounds [-1,1]
NormalizeMesh(positions);

// Flip winding (reverse normals)
InvertWinding(indices);
```

## Procedural Generation

### Terrain from Heightmap
```cpp
std::vector<float> heightmap(width * height);
// ... populate heightmap ...

std::vector<glm::vec3> positions;
std::vector<uint32_t> indices;

GenerateTerrainMesh(positions, indices,
                   width, height,
                   heightmap,
                   10.0f);  // height scale
```

### Noise Terrain
```cpp
GenerateNoiseTerrain(positions, indices,
                    200, 200,          // grid size
                    10.0f,             // horizontal scale
                    0.1f,              // frequency (lower = larger features)
                    5.0f);             // amplitude (height variation)
```

### Tube/Path
```cpp
std::vector<glm::vec3> curve = {
    glm::vec3(0, 0, 0),
    glm::vec3(5, 2, 0),
    glm::vec3(10, 0, 0),
};

GenerateTube(positions, indices,
            curve,
            0.5f,      // radius
            16);       // segments
```

## Vertex Format Cheat Sheet

| Format | Size | Use Case |
|--------|------|----------|
| `Vertex` | 28 bytes | Debug, simple color rendering |
| `VertexPN` | 24 bytes | Lighting, no textures |
| `VertexPNC` | 28 bytes | Lighting + per-vertex color |
| `VertexPNUV` | 32 bytes | Textured models |
| `VertexPNTUV` | 48 bytes | Textured + normal mapping |

## Common Patterns

### Create and Render Cube
```cpp
auto cube = Mesh::CreateCube(device.get());
auto material = Material::CreateSolidColor(device.get(), glm::vec4(1, 0, 0, 1));

renderer->BeginFrame(camera);
material->Bind(device.get());
cube->Draw(device.get());
material->Unbind(device.get());
renderer->EndFrame();
```

### Generate LOD and Select
```cpp
struct MeshAsset {
    std::vector<MeshLOD> lods;
    
    uint32_t SelectLOD(float distance, float max_distance) {
        float t = distance / max_distance;
        return static_cast<uint32_t>(t * (lods.size() - 1));
    }
};

// Usage
uint32_t lod_index = asset.SelectLOD(distance_to_camera, 1000.0f);
```

### Compute Tangent Space
```cpp
auto positions = /* ... */;
auto normals = CalculateSmoothedNormals(positions, indices);

std::vector<glm::vec4> tangents;
std::vector<glm::vec3> bitangents;
CalculateTangents(tangents, bitangents,
                 positions, normals, texcoords, indices);

// Now use in shader
// normal_map_sample = normalize(normal_map * 2 - 1)
// normal_ws = normalize(TBN * normal_map_sample)
// where TBN = [tangent, bitangent, normal]
```

## Error Handling

```cpp
// Validate mesh
if (mesh.GetVertexCount() == 0 || mesh.GetIndexCount() == 0) {
    spdlog::error("Empty mesh");
    return;
}

// Check bounds validity
if (mesh.GetBoundsRadius() == 0.0f) {
    spdlog::warn("Degenerate mesh (zero size)");
}

// Verify topology
if (!IsMeshManifold(indices)) {
    spdlog::warn("Non-manifold geometry detected");
}

if (!IsMeshClosed(positions, indices)) {
    spdlog::warn("Unclosed mesh cannot compute volume");
} else {
    float vol = CalculateMeshVolume(positions, indices);
}
```

## Tips & Tricks

1. **Always calculate bounds after creating mesh**
   ```cpp
   mesh->CalculateBounds();
   ```

2. **Use icosphere for LOD (more uniform than UV sphere)**
   ```cpp
   auto sphere = Mesh::CreateIcosphere(device.get(), radius, subdivisions);
   ```

3. **Chain geometry operations**
   ```cpp
   auto indices = RemoveDegenerateTriangles(positions, indices);
   auto weld = WeldVertices(positions, indices, threshold);
   indices = weld.new_indices;
   auto simple = SimplifyMesh(positions, indices, target);
   ```

4. **Precompute normals at asset load time**
   ```cpp
   auto normals = CalculateSmoothedNormals(positions, indices);
   // Store with asset, don't recalculate at runtime
   ```

5. **Check mesh quality before shipping**
   ```cpp
   auto quality = CalculateMeshQuality(positions, indices);
   if (quality.aspect_ratio > 2.0f) {
       spdlog::warn("Poor aspect ratio: {}", quality.aspect_ratio);
   }
   ```
