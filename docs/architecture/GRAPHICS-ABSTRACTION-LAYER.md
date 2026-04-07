# Graphics Abstraction Layer

## Overview

The Graphics abstraction layer provides a comprehensive, API-agnostic interface for rendering, resource management, and graphics operations. It's designed to support multiple graphics APIs (OpenGL, Vulkan, DirectX12, Metal) while maintaining a clean, high-level interface for game code.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Game/Editor Code                     │
└──────────────────────┬──────────────────────────────────┘
                       │
         ┌─────────────▼──────────────┐
         │   Graphics Abstraction     │
         │   (graphics.h)             │
         ├────────────────────────────┤
         │  - Renderer                │
         │  - Material                │
         │  - Mesh                    │
         │  - Texture                 │
         │  - Framebuffer             │
         │  - Graphics Utils          │
         └─────────────┬──────────────┘
                       │
      ┌────────────────┴────────────────┐
      │                                 │
      ▼                                 ▼
┌───────────────┐              ┌──────────────┐
│  RenderDevice │              │   Renderer   │
│  (API Layer)  │              │ (High-level) │
└───────┬───────┘              └──────┬───────┘
        │                             │
    ┌───▼────────────────────────────▼───┐
    │  OpenGL Implementation              │
    │  (OpenGLDevice, OpenGLTexture2D...) │
    │                                      │
    │  Future: Vulkan, DX12, Metal        │
    └──────────────────────────────────────┘
```

## Core Components

### 1. **RenderDevice** (`render_device.h`)
Abstract interface to low-level graphics API operations.

**Responsibilities:**
- Shader compilation and linking
- Buffer management (VBO, IBO, UBO)
- Texture and framebuffer management
- Viewport and render state management
- Drawing commands

**Key Methods:**
```cpp
// Shader Operations
uint32_t CompileShader(ShaderStage stage, const std::string& source);
uint32_t LinkProgram(const uint32_t* stages, int count);
void UseProgram(uint32_t program);

// Buffer Operations
uint32_t CreateBuffer(uint32_t target, size_t size, const void* data, bool dynamic);
void UpdateBuffer(uint32_t handle, size_t offset, size_t size, const void* data);

// Texture Operations
uint32_t CreateTexture2D(uint32_t width, uint32_t height, uint32_t format, const void* data);
uint32_t CreateTextureCube(uint32_t size, uint32_t format, const void** faces);
void BindTexture(uint32_t texture, uint32_t unit);

// Draw Operations
void DrawIndexed(uint32_t mode, uint32_t index_count, uint32_t index_type, size_t offset);
void Draw(uint32_t mode, uint32_t vertex_count);

// State Management
void SetDepthTest(bool enabled);
void SetBlending(bool enabled);
void SetFaceCulling(bool enabled, bool cull_back);
```

### 2. **Renderer** (`renderer.h`)
High-level rendering interface for typical rendering tasks.

**Responsibilities:**
- Frame lifecycle management (BeginFrame/EndFrame)
- Scene rendering and composition
- Debug visualization
- Performance statistics

**Key Methods:**
```cpp
bool Initialize();
void Shutdown();

void BeginFrame(const Camera& camera);
void EndFrame();

void DrawTriangle(const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, const glm::vec4& color);
void DrawCube(const Transform& transform, const glm::vec4& color);
void DrawSphere(const glm::vec3& center, float radius, const glm::vec4& color);

RenderStats GetStats() const;
```

### 3. **Texture System** (`texture.h`)

#### Texture (Base Class)
Represents a GPU texture resource.

#### Texture2D
2D texture for standard textured geometry.

```cpp
auto texture = Texture2D::Create(device, 1024, 1024, TextureFormat::RGBA8);
texture->SetData(device, pixel_data);
texture->GenerateMipmaps(device);
texture->Bind(device, 0);  // Bind to texture unit 0
```

#### TextureCube
Cubemap texture for skyboxes and environment mapping.

```cpp
const void* faces[6] = { /* 6 face data pointers */ };
auto cubemap = TextureCube::Create(device, 512, TextureFormat::RGBA8, faces);
cubemap->GenerateMipmaps(device);
cubemap->Bind(device, 0);
```

**TextureFormat Options:**
- `RGB8`, `RGBA8` - 8-bit per channel
- `RGB16F`, `RGBA16F` - 16-bit float per channel
- `RGB32F`, `RGBA32F` - 32-bit float per channel
- `R8`, `R16F`, `R32F` - Single channel
- `Depth16`, `Depth24`, `Depth32F` - Depth-only formats
- `Depth24Stencil8` - Depth + stencil

**TextureSettings:**
```cpp
TextureSettings settings;
settings.min_filter = TextureFilter::LinearMipmapLinear;
settings.mag_filter = TextureFilter::Linear;
settings.wrap_u = TextureWrap::Repeat;
settings.wrap_v = TextureWrap::Repeat;
settings.generate_mipmaps = true;
```

### 4. **Material System** (`material.h`)

Materials encapsulate shader programs with their uniform parameters and texture bindings.

```cpp
// Create from existing shader
auto material = Material::Create(shader);

// Set uniforms
material->SetUniform4f("uColor", 1.0f, 0.0f, 0.0f, 1.0f);
material->SetUniformMat4("uModelMatrix", transform.GetMatrix());
material->SetUniformVec3("uLightPos", light.position);

// Bind textures
material->SetTexture("uTexture", texture, 0);
material->SetTexture("uNormal", normal_map, 1);

// Use for rendering
material->Bind(device);
// ... draw calls ...
material->Unbind(device);
```

**Preset Materials:**

```cpp
// Solid color material
auto solid = Material::CreateSolidColor(device, glm::vec4(1, 0, 0, 1));

// Textured material
auto textured = Material::CreateTextured(device, albedo_texture);

// PBR material (physically-based rendering)
auto pbr = Material::CreatePBR(device, albedo, normal, metallic, roughness, ao);
```

### 5. **Framebuffer** (`framebuffer.h`)

Framebuffers enable off-screen rendering to textures.

**Use Cases:**
- Post-processing effects
- Shadow mapping
- Deferred rendering (G-buffers)
- Scene capture
- Bloom effects

```cpp
// Create with color and depth attachments
auto fb = Framebuffer::Create(device, 1280, 720, TextureFormat::RGBA8, TextureFormat::Depth24);

// Bind for rendering
fb->Bind(device);
device->Clear(true, true, true, glm::vec4(0, 0, 0, 1));
// ... render to framebuffer ...
fb->Unbind(device);

// Access rendered textures
auto color_tex = fb->GetColorAttachment(0);
auto depth_tex = fb->GetDepthAttachment();

// Resize if needed
fb->Resize(device, 1920, 1080);
```

**Multiple Attachments:**
```cpp
auto fb = Framebuffer::Create(device, 1280, 720);

auto color1 = Texture2D::Create(device, 1280, 720, TextureFormat::RGBA16F);
auto color2 = Texture2D::Create(device, 1280, 720, TextureFormat::RGBA16F);
auto normal = Texture2D::Create(device, 1280, 720, TextureFormat::RGBA8);
auto depth = Texture2D::Create(device, 1280, 720, TextureFormat::Depth32F);

fb->AttachColor(device, 0, color1);  // G-buffer: Albedo
fb->AttachColor(device, 1, color2);  // G-buffer: Position
fb->AttachColor(device, 2, normal);  // G-buffer: Normal
fb->AttachDepth(device, depth);
```

### 6. **Graphics Utilities** (`graphics_utils.h`)

Helper functions for common graphics operations.

**Color Space Conversions:**
```cpp
glm::vec3 linear = SRGBToLinear(srgb_color);
glm::vec3 srgb = LinearToSRGB(linear_color);
glm::vec3 rgb = HSVToRGB(h, s, v);
glm::vec3 hsv = RGBToHSV(rgb);
```

**Geometry Utilities:**
```cpp
BoundingBox bbox = CalculateBoundingBox(positions, vertex_count);
CalculateNormals(normals, positions, indices, index_count);
CalculateTangentSpace(tangents, bitangents, positions, normals, texcoords, indices, index_count);
```

**Matrix Operations:**
```cpp
glm::mat4 view = CreateViewMatrix(eye_pos, target, up);
glm::mat4 proj = CreatePerspectiveMatrix(45.0f, aspect, 0.1f, 1000.0f);
glm::mat4 ortho = CreateOrthogonalMatrix(-1, 1, -1, 1, 0.1f, 100.0f);
glm::mat4 shadow = CreateLightMatrix(light_dir, scene_bounds);
```

**Format Information:**
```cpp
uint32_t texel_size = GetTexelSize(TextureFormat::RGBA8);
uint64_t total_bytes = CalculateTextureSizeBytes(1024, 1024, format, true);
glm::ivec2 mip_dims = GetMipmapDimensions(3, 1024, 1024);
```

## Usage Examples

### Basic Rendering Setup

```cpp
#include "graphics.h"

namespace gfx = schizo::graphics;

class Game {
private:
    std::unique_ptr<gfx::Renderer> renderer;
    std::unique_ptr<gfx::Mesh> mesh;
    std::unique_ptr<gfx::Material> material;
    
public:
    bool Initialize() {
        // Create renderer
        renderer = gfx::Renderer::Create();
        if (!renderer->Initialize()) {
            return false;
        }
        
        // Create geometry
        mesh = gfx::Mesh::CreateCube(renderer->GetDevice());
        
        // Create material
        material = gfx::Material::CreateSolidColor(renderer->GetDevice(), glm::vec4(1, 0, 0, 1));
        
        return true;
    }
    
    void Update(float delta_time) {
        // Update camera, inputs, etc.
    }
    
    void Render() {
        gfx::Camera camera;
        camera.position = glm::vec3(0, 1, 5);
        camera.direction = glm::vec3(0, 0, -1);
        
        renderer->BeginFrame(camera);
        {
            gfx::Transform transform;
            transform.position = glm::vec3(0, 0, 0);
            
            material->Bind(renderer->GetDevice());
            material->SetUniformMat4("uModel", transform.GetMatrix());
            mesh->Draw(renderer->GetDevice());
            material->Unbind(renderer->GetDevice());
        }
        renderer->EndFrame();
        
        // Display stats
        auto stats = renderer->GetStats();
        printf("Draw calls: %u, Vertices: %u\n", stats.draw_calls, stats.vertices_rendered);
    }
};
```

### Textured Rendering

```cpp
void CreateTexturedScene(gfx::RenderDevice* device) {
    // Load texture
    auto texture = gfx::Texture2D::LoadFromFile(device, "assets/textures/wall.png");
    if (!texture) {
        spdlog::error("Failed to load texture");
        return;
    }
    
    // Create material with texture
    auto material = gfx::Material::CreateTextured(device, texture);
    
    // Create mesh
    auto mesh = gfx::Mesh::CreateCube(device);
    
    // Render
    material->Bind(device);
    mesh->Draw(device);
    material->Unbind(device);
}
```

### Off-Screen Rendering (Shadow Mapping)

```cpp
void RenderShadowMap(gfx::Renderer* renderer, gfx::Mesh* mesh) {
    auto device = renderer->GetDevice();
    
    // Create shadow framebuffer
    uint32_t shadow_size = 2048;
    auto shadow_fb = gfx::Framebuffer::Create(
        device, shadow_size, shadow_size,
        gfx::TextureFormat::R32F,  // Store depth in color
        gfx::TextureFormat::Depth32F
    );
    
    // Render to shadow map
    shadow_fb->Bind(device);
    device->Clear(true, true, true, glm::vec4(1, 1, 1, 1));
    
    auto shadow_material = gfx::Material::CreateSolidColor(device, glm::vec4(1, 1, 1, 1));
    shadow_material->Bind(device);
    mesh->Draw(device);
    shadow_material->Unbind(device);
    
    shadow_fb->Unbind(device);
    
    // Use shadow map texture for lighting pass
    auto shadow_texture = shadow_fb->GetColorAttachment(0);
    // ... render with shadow lookup ...
}
```

### Deferred Rendering Setup

```cpp
void SetupDeferredRendering(gfx::Renderer* renderer) {
    auto device = renderer->GetDevice();
    uint32_t w = 1280, h = 720;
    
    // Create G-buffer framebuffer
    auto g_buffer = gfx::Framebuffer::Create(device, w, h);
    
    // G-buffer attachments
    auto albedo = gfx::Texture2D::Create(device, w, h, gfx::TextureFormat::RGBA8);
    auto normal = gfx::Texture2D::Create(device, w, h, gfx::TextureFormat::RGBA16F);
    auto position = gfx::Texture2D::Create(device, w, h, gfx::TextureFormat::RGBA32F);
    auto metallic_roughness = gfx::Texture2D::Create(device, w, h, gfx::TextureFormat::RG8);
    auto depth = gfx::Texture2D::Create(device, w, h, gfx::TextureFormat::Depth32F);
    
    // Attach to framebuffer
    g_buffer->AttachColor(device, 0, albedo);
    g_buffer->AttachColor(device, 1, normal);
    g_buffer->AttachColor(device, 2, position);
    g_buffer->AttachColor(device, 3, metallic_roughness);
    g_buffer->AttachDepth(device, depth);
    
    // Geometry pass: render to G-buffer
    g_buffer->Bind(device);
    device->Clear(true, true, true, glm::vec4(0, 0, 0, 1));
    
    // Render geometry with G-buffer shaders
    // ... geometry rendering ...
    
    g_buffer->Unbind(device);
    
    // Lighting pass: use G-buffers for lighting
    // ... render lit scene ...
}
```

## API Support

### Current
- **OpenGL 4.5+** - Full support

### Planned
- **Vulkan** - In development
- **DirectX 12** - Planned
- **Metal** - Planned

## Performance Considerations

### Texture Management
- Mipmaps reduce GPU memory bandwidth during sampling
- Compressed formats recommended for large textures
- Bind textures efficiently (minimize unit changes)

### Framebuffer Management
- Framebuffer attachments must be compatible
- Invalidate unused attachments to improve performance on tile-based deferred renderers
- Resize only when necessary

### Material Caching
- Reuse materials across multiple objects with the same shader
- Batch uniform updates before draw calls
- Use material presets for common cases

### State Management
- Minimize state changes between draw calls
- Group draw calls by material to reduce program switches
- Use vertex array objects for efficient vertex format binding

## Error Handling

All graphics operations provide error reporting:

```cpp
auto device = gfx::RenderDevice::Create();
if (!device->Initialize()) {
    spdlog::error("Failed to initialize: {}", device->GetLastError());
    return false;
}

auto texture = gfx::Texture2D::Create(device.get(), 1024, 1024, TextureFormat::RGBA8);
if (!texture) {
    spdlog::error("Failed to create texture");
    return false;
}

if (!framebuffer->IsValid()) {
    spdlog::error("Framebuffer validation failed: {}", framebuffer->GetValidationError());
    return false;
}
```

## Thread Safety

- Graphics operations are **NOT** thread-safe
- All rendering must occur on the main thread with an active graphics context
- Resource creation can happen asynchronously if loading on background threads
- Upload to GPU on main thread only

## Future Enhancements

1. **Compute Shaders** - For physics simulation, post-processing
2. **Indirect Rendering** - GPU-driven rendering pipelines
3. **Mesh Shaders** - Next-gen GPU rendering
4. **Variable Rate Shading** - Performance optimization
5. **Ray Tracing** - Advanced lighting effects
6. **Streaming** - Virtual texturing and megatextures
7. **Profiling Tools** - Built-in performance analysis
8. **Shader Compilation Caching** - Faster startup times

## Debugging

Enable debug output for detailed graphics information:

```cpp
device->EnableDebugOutput(true);

// Shader compilation errors
if (!material->GetShader()->IsValid()) {
    spdlog::error("Shader error: {}", material->GetShader()->GetLastError());
}

// RenderDevice errors
std::string last_error = device->GetLastError();
if (!last_error.empty()) {
    spdlog::error("Graphics error: {}", last_error);
}
```

## References

- OpenGL 4.5 Specification
- GPU Architecture & Performance
- Modern Graphics Techniques
- Game Engine Architecture
