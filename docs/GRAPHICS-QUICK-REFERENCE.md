# Graphics Layer Quick Reference

## Initialization

```cpp
// Create and initialize graphics
auto device = schizo::graphics::RenderDevice::Create();
if (!device->Initialize()) {
    LOG_ERROR("Graphics device initialization failed");
    return false;
}

auto renderer = schizo::graphics::Renderer::Create();
if (!renderer->Initialize()) {
    LOG_ERROR("Renderer initialization failed");
    return false;
}
```

## Creating Textures

```cpp
// Create empty texture
auto texture = schizo::graphics::Texture2D::Create(
    device.get(), 
    1024, 1024, 
    schizo::graphics::TextureFormat::RGBA8
);

// Load from file
auto texture_from_file = schizo::graphics::Texture2D::LoadFromFile(
    device.get(), 
    "assets/textures/diffuse.png"
);

// Create cubemap
const void* faces[6] = { /* 6 face data */ };
auto cubemap = schizo::graphics::TextureCube::Create(
    device.get(),
    512,
    schizo::graphics::TextureFormat::RGBA8,
    faces
);

// Set texture settings
schizo::graphics::TextureSettings settings;
settings.min_filter = schizo::graphics::TextureFilter::LinearMipmapLinear;
settings.mag_filter = schizo::graphics::TextureFilter::Linear;
settings.wrap_u = schizo::graphics::TextureWrap::Repeat;
settings.wrap_v = schizo::graphics::TextureWrap::Repeat;
settings.generate_mipmaps = true;
```

## Creating Materials

```cpp
// Solid color material
auto red_material = schizo::graphics::Material::CreateSolidColor(
    device.get(),
    glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)
);

// Textured material
auto textured_material = schizo::graphics::Material::CreateTextured(
    device.get(),
    diffuse_texture
);

// Custom shader material
auto custom_shader = schizo::graphics::ShaderProgram::Create(/* ... */);
auto custom_material = schizo::graphics::Material::Create(custom_shader);

// PBR material
auto pbr_material = schizo::graphics::Material::CreatePBR(
    device.get(),
    albedo_texture,
    normal_texture,
    metallic_texture,
    roughness_texture,
    ao_texture
);

// Set material uniforms
material->SetUniform1f("uAlpha", 0.8f);
material->SetUniform3f("uOffset", 1.0f, 2.0f, 3.0f);
material->SetUniformVec3("uLightPos", light_position);
material->SetUniformMat4("uTransform", transform_matrix);
material->SetTexture("uTexture", texture, 0);
material->SetTexture("uNormalMap", normal_texture, 1);
```

## Creating Meshes

```cpp
// Built-in primitives
auto triangle_mesh = schizo::graphics::Mesh::CreateTriangle(device.get());
auto cube_mesh = schizo::graphics::Mesh::CreateCube(device.get());
auto grid_mesh = schizo::graphics::Mesh::CreateGrid(device.get(), 10, 10);
auto sphere_mesh = schizo::graphics::Mesh::CreateSphere(device.get(), 1.0f, 32, 16);

// Custom mesh
std::vector<schizo::graphics::Vertex> vertices = { /* ... */ };
std::vector<uint32_t> indices = { /* ... */ };
schizo::graphics::Mesh custom_mesh;
custom_mesh.SetData(device.get(), vertices, indices);
```

## Creating Framebuffers

```cpp
// Simple color + depth framebuffer
auto framebuffer = schizo::graphics::Framebuffer::Create(
    device.get(),
    1280, 720,
    schizo::graphics::TextureFormat::RGBA8,
    schizo::graphics::TextureFormat::Depth24
);

// Complex G-buffer for deferred rendering
auto g_buffer = schizo::graphics::Framebuffer::Create(device.get(), 1280, 720);

auto albedo = schizo::graphics::Texture2D::Create(device.get(), 1280, 720, 
    schizo::graphics::TextureFormat::RGBA8);
auto normal = schizo::graphics::Texture2D::Create(device.get(), 1280, 720, 
    schizo::graphics::TextureFormat::RGBA16F);
auto position = schizo::graphics::Texture2D::Create(device.get(), 1280, 720, 
    schizo::graphics::TextureFormat::RGBA32F);
auto depth = schizo::graphics::Texture2D::Create(device.get(), 1280, 720, 
    schizo::graphics::TextureFormat::Depth32F);

g_buffer->AttachColor(device.get(), 0, albedo);      // G0: Albedo
g_buffer->AttachColor(device.get(), 1, normal);      // G1: Normal
g_buffer->AttachColor(device.get(), 2, position);    // G2: Position
g_buffer->AttachDepth(device.get(), depth);          // Depth

// Validation
if (!g_buffer->IsValid()) {
    LOG_ERROR("Framebuffer validation failed: {}", g_buffer->GetValidationError());
}
```

## Rendering

```cpp
// Frame setup
schizo::graphics::Camera camera;
camera.position = glm::vec3(0.0f, 1.0f, 5.0f);
camera.direction = glm::vec3(0.0f, 0.0f, -1.0f);
camera.up = glm::vec3(0.0f, 1.0f, 0.0f);
camera.fov = 45.0f;
camera.viewport_width = 1280;
camera.viewport_height = 720;

renderer->BeginFrame(camera);
{
    // Render objects
    schizo::graphics::Transform transform;
    transform.position = glm::vec3(0.0f, 0.0f, 0.0f);
    transform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    transform.scale = glm::vec3(1.0f, 1.0f, 1.0f);
    
    material->Bind(renderer->GetDevice());
    material->SetUniformMat4("uModel", transform.GetMatrix());
    material->SetUniformMat4("uView", camera.GetViewMatrix());
    material->SetUniformMat4("uProjection", camera.GetProjectionMatrix());
    mesh->Draw(renderer->GetDevice());
    material->Unbind(renderer->GetDevice());
}
renderer->EndFrame();

// Get performance stats
auto stats = renderer->GetStats();
printf("Draw calls: %u\n", stats.draw_calls);
printf("Vertices rendered: %u\n", stats.vertices_rendered);
printf("Triangles rendered: %u\n", stats.triangles_rendered);
printf("GPU time: %.2f ms\n", stats.gpu_time_ms);
```

## Off-Screen Rendering (Shadow Mapping)

```cpp
// Create shadow framebuffer
uint32_t shadow_resolution = 2048;
auto shadow_fb = schizo::graphics::Framebuffer::Create(
    device.get(),
    shadow_resolution, shadow_resolution,
    schizo::graphics::TextureFormat::R32F,      // Store depth as texture
    schizo::graphics::TextureFormat::Depth32F
);

// Render from light perspective
shadow_fb->Bind(device.get());
device.get()->Clear(true, true, true, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

schizo::graphics::Light light;
light.type = schizo::graphics::Light::Type::Directional;
light.direction = glm::normalize(glm::vec3(1.0f, -1.0f, 1.0f));

// Use light matrix for shadow rendering
glm::mat4 light_matrix = schizo::graphics::CreateLightMatrix(
    light.direction,
    scene_bounding_box
);

shadow_material->SetUniformMat4("uLightViewProj", light_matrix);
mesh->Draw(device.get());

shadow_fb->Unbind(device.get());

// Use shadow map in lighting
auto shadow_texture = shadow_fb->GetColorAttachment(0);
lighting_material->SetTexture("uShadowMap", shadow_texture, 0);
```

## Color Space Conversions

```cpp
// sRGB to Linear (for proper lighting)
glm::vec3 linear_color = schizo::graphics::SRGBToLinear(srgb_color);

// Linear to sRGB (for display)
glm::vec3 display_color = schizo::graphics::LinearToSRGB(linear_color);

// HSV color selection (color picker friendly)
glm::vec3 red = schizo::graphics::HSVToRGB(0.0f, 1.0f, 1.0f);
glm::vec3 hsv = schizo::graphics::RGBToHSV(rgb_color);
```

## Geometry Utilities

```cpp
// Calculate bounding box
schizo::graphics::BoundingBox bbox = schizo::graphics::CalculateBoundingBox(
    positions, vertex_count
);
glm::vec3 center = bbox.GetCenter();
glm::vec3 extents = bbox.GetExtents();
float radius = bbox.GetRadius();

// Calculate normals
schizo::graphics::CalculateNormals(
    normal_buffer, position_buffer, index_buffer, index_count
);

// Calculate tangent space (for normal mapping)
schizo::graphics::CalculateTangentSpace(
    tangent_buffer, bitangent_buffer,
    position_buffer, normal_buffer, texcoord_buffer,
    index_buffer, index_count
);
```

## Matrix Utilities

```cpp
// Create view matrix
glm::mat4 view = schizo::graphics::CreateViewMatrix(
    eye_position,
    target_position,
    up_vector
);

// Create projection matrices
glm::mat4 perspective = schizo::graphics::CreatePerspectiveMatrix(
    45.0f,      // FOV in degrees
    1.77f,      // Aspect ratio
    0.1f,       // Near plane
    1000.0f     // Far plane
);

glm::mat4 orthogonal = schizo::graphics::CreateOrthogonalMatrix(
    -10.0f, 10.0f,  // Left, Right
    -5.0f, 5.0f,    // Bottom, Top
    0.1f, 100.0f    // Near, Far
);

// Create light matrix (shadow mapping)
glm::mat4 light_matrix = schizo::graphics::CreateLightMatrix(
    light_direction,
    scene_bounds
);
```

## Error Handling

```cpp
// Check device errors
if (!device->Initialize()) {
    LOG_ERROR("Device init failed: {}", device->GetLastError());
    return false;
}

// Check framebuffer validity
if (!framebuffer->IsValid()) {
    LOG_ERROR("Framebuffer error: {}", framebuffer->GetValidationError());
    return false;
}

// Check shader compilation
auto shader = schizo::graphics::ShaderProgram::CreateBasicShader(device.get());
if (!shader->IsValid()) {
    LOG_ERROR("Shader compilation failed: {}", shader->GetLastError());
    return false;
}
```

## Cleanup

```cpp
// Resources are automatically cleaned up via smart pointers
// Manual cleanup only if needed:
framebuffer->Unbind(device.get());
material->Unbind(device.get());

// Shutdown in order
renderer->Shutdown();
device->Shutdown();
```

## Common Patterns

### Pattern 1: Textured Cube Rendering
```cpp
auto texture = schizo::graphics::Texture2D::LoadFromFile(device.get(), "texture.png");
auto material = schizo::graphics::Material::CreateTextured(device.get(), texture);
auto mesh = schizo::graphics::Mesh::CreateCube(device.get());

renderer->BeginFrame(camera);
material->Bind(device.get());
mesh->Draw(device.get());
material->Unbind(device.get());
renderer->EndFrame();
```

### Pattern 2: Custom Shader with Multiple Uniforms
```cpp
auto material = schizo::graphics::Material::Create(custom_shader);
material->SetUniform4f("uColor", r, g, b, a);
material->SetUniformVec3("uLightPos", light_pos);
material->SetUniformMat4("uModel", model_matrix);
material->SetTexture("uAlbedo", albedo_texture, 0);
material->SetTexture("uNormal", normal_texture, 1);

material->Bind(device.get());
mesh->Draw(device.get());
material->Unbind(device.get());
```

### Pattern 3: Post-Processing Pipeline
```cpp
// 1. Render scene to framebuffer
scene_fb->Bind(device.get());
// ... render scene ...
scene_fb->Unbind(device.get());

// 2. Apply post-processing to result
postprocess_material->SetTexture("uScene", scene_fb->GetColorAttachment(0), 0);
postprocess_material->SetUniform1f("uIntensity", 1.0f);
fullscreen_quad->Draw(device.get());
```
