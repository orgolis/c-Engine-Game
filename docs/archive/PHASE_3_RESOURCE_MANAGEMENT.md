# Phase 3: Resource Management (Weeks 9-12)
## Vulkan Buffer, Image, Mesh Loading & Material System

**Phase Status:** 🔄 In Progress  
**Weeks:** 9-12  
**Estimated Duration:** 4 weeks (20 working days)  
**Dependencies:** Phase 1 ✅, Phase 2 ✅  

---

## OVERVIEW

Phase 3 focuses on GPU resource management—the foundation for rendering complex geometry and materials. We'll build abstractions for buffers and images, implement mesh loading via glTF, and establish a material system with parameter binding.

**Key Assumption:** Phase 2 graphics pipeline (shaders, render passes, barriers) is complete and tested.

### Deliverables
| Component | Files | Status |
|-----------|-------|--------|
| VulkanBuffer | buffer.h/cpp | ⏳ To Do |
| VulkanImage | image.h/cpp | ⏳ To Do |
| VulkanMesh | mesh.h/cpp | ⏳ To Do |
| Material System | material.h/cpp | ⏳ To Do |
| Render Graph | render_graph.h/cpp | ⏳ To Do |
| glTF Loader | gltf_loader.h/cpp | ⏳ To Do |
| Phase 3 Test | mesh_rendering_test.cpp | ⏳ To Do |

---

## COMPONENT 1: VULKAN BUFFER ABSTRACTION

### File: `engine/renderer/gpu/vulkan/buffer.h`

**Purpose:** RAII wrapper for VkBuffer with automatic memory management, staging, and CPU-GPU synchronization.

**Design:**
- Wrapper around VkBuffer + VkDeviceMemory
- Automatic VMA allocation (from Phase 1)
- Support for vertex, index, uniform, storage buffer types
- Lazy upload via staging buffers

**Interface:**
```cpp
namespace vks {

enum class BufferUsage {
    VERTEX,           // VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    INDEX,            // VK_BUFFER_USAGE_INDEX_BUFFER_BIT
    UNIFORM,          // VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
    STORAGE,          // VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
    TRANSFER_DST,     // VK_BUFFER_USAGE_TRANSFER_DST_BIT
    TRANSFER_SRC,     // VK_BUFFER_USAGE_TRANSFER_SRC_BIT
};

class VulkanBuffer {
public:
    VulkanBuffer() = default;
    ~VulkanBuffer() { destroy(); }
    
    // Create buffer with CPU data
    static VulkanBuffer create(VkDevice device, VkPhysicalDevice pdev,
                              BufferUsage usage, size_t size, const void* data = nullptr);
    
    // Getters
    VkBuffer get() const { return buffer; }
    size_t get_size() const { return size; }
    BufferUsage get_usage() const { return usage; }
    
    // CPU updates (for uniform buffers)
    void update(void* data, size_t size, size_t offset = 0);
    
    // GPU copies (for mesh data)
    void upload(VkCommandBuffer cmd, const void* data, size_t size, size_t offset = 0);
    
    // Move semantics
    VulkanBuffer(VulkanBuffer&& other) noexcept;
    VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;
    
    // Delete copies
    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;

private:
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    size_t size = 0;
    BufferUsage usage = BufferUsage::VERTEX;
    
    void destroy();
};

}  // namespace vks
```

**Implementation Notes:**
- Use VMA for allocation (easier than raw vkAllocateMemory)
- For VERTEX/INDEX: Allocate GPU-only, use staging buffer for upload
- For UNIFORM: Allocate HOST_VISIBLE for CPU updates
- Implement upload() with command recording (caller provides VkCommandBuffer)

---

## COMPONENT 2: VULKAN IMAGE ABSTRACTION

### File: `engine/renderer/gpu/vulkan/image.h`

**Purpose:** RAII wrapper for VkImage, VkImageView, and samplers. Handles texture loading and mip generation.

**Design:**
- Wrapper around VkImage + VkImageView + optional VkSampler
- Support for 2D textures, cubemaps, arrays
- Automatic mip generation via compute shader (Phase 5) or pre-load
- Layout management coordinated with barrier system

**Interface:**
```cpp
namespace vks {

enum class ImageFormat {
    RGBA8_SRGB,       // For color textures
    RGBA8_UNORM,      // For attachment outputs
    RGBA16F,          // For HDR
    RGBA32F,          // For high precision
    DEPTH32F,         // For depth attachments
};

enum class ImageUsage {
    TEXTURE,          // Sampled
    ATTACHMENT,       // Render target
    STORAGE,          // Compute read/write
};

class VulkanImage {
public:
    VulkanImage() = default;
    ~VulkanImage() { destroy(); }
    
    // Create empty image
    static VulkanImage create(VkDevice device, VkPhysicalDevice pdev,
                             uint32_t width, uint32_t height, ImageFormat format,
                             ImageUsage usage);
    
    // Load from file (PNG/JPG/EXR via stb_image)
    static VulkanImage load_from_file(VkDevice device, VkPhysicalDevice pdev,
                                     const std::string& path, ImageUsage usage = ImageUsage::TEXTURE);
    
    // Getters
    VkImage get_image() const { return image; }
    VkImageView get_view() const { return view; }
    VkSampler get_sampler() const { return sampler; }
    uint32_t get_width() const { return width; }
    uint32_t get_height() const { return height; }
    VkFormat get_vk_format() const { return vk_format; }
    
    // Layout management
    VkImageLayout get_current_layout() const { return current_layout; }
    void set_layout(VkImageLayout layout) { current_layout = layout; }
    
    // Move semantics
    VulkanImage(VulkanImage&& other) noexcept;
    VulkanImage& operator=(VulkanImage&& other) noexcept;
    
    // Delete copies
    VulkanImage(const VulkanImage&) = delete;
    VulkanImage& operator=(const VulkanImage&) = delete;

private:
    VkDevice device = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint32_t width = 0, height = 0;
    VkFormat vk_format = VK_FORMAT_UNDEFINED;
    VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    void destroy();
    static VkFormat format_to_vk(ImageFormat fmt);
};

}  // namespace vks
```

**Implementation Notes:**
- Use stb_image for loading PNG/JPG/TGA
- Create sampler with reasonable defaults (LINEAR, REPEAT, ANISOTROPY_16)
- Track current layout (for barrier tracking in Phase 3)
- Mip generation deferred to Phase 5 (for now, upload as-is or use linear filtering)

---

## COMPONENT 3: VULKAN MESH ABSTRACTION

### File: `engine/renderer/gpu/vulkan/mesh.h`

**Purpose:** High-level mesh wrapper combining vertex buffer, index buffer, and draw parameters.

**Design:**
- Aggregates VulkanBuffer (vertex/index) + draw info
- Matches PhysicsShape for collision (shares geometry)
- Supports multiple submeshes (for complex models)

**Interface:**
```cpp
namespace vks {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec3 tangent;
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

struct Submesh {
    uint32_t index_offset;  // Start in index buffer
    uint32_t index_count;   // Number of indices to render
    uint32_t material_id;   // Material reference
};

class VulkanMesh {
public:
    VulkanMesh() = default;
    ~VulkanMesh() = default;
    
    // Create from raw vertex/index data
    static VulkanMesh create(VkDevice device, VkPhysicalDevice pdev,
                            const MeshData& data);
    
    // Create with submeshes
    static VulkanMesh create_multi(VkDevice device, VkPhysicalDevice pdev,
                                  const std::vector<MeshData>& submeshes);
    
    // Getters
    const VulkanBuffer& get_vertex_buffer() const { return vertex_buffer; }
    const VulkanBuffer& get_index_buffer() const { return index_buffer; }
    const std::vector<Submesh>& get_submeshes() const { return submeshes; }
    
    // Draw info
    uint32_t get_vertex_count() const { return vertex_count; }
    uint32_t get_index_count() const { return index_count; }
    
    // Move semantics
    VulkanMesh(VulkanMesh&& other) noexcept;
    VulkanMesh& operator=(VulkanMesh&& other) noexcept;

private:
    VulkanBuffer vertex_buffer;
    VulkanBuffer index_buffer;
    std::vector<Submesh> submeshes;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
};

}  // namespace vks
```

---

## COMPONENT 4: MATERIAL SYSTEM

### File: `engine/renderer/gpu/vulkan/material.h`

**Purpose:** Material definition with shader, textures, and parameter binding.

**Design:**
- Represents a complete material (shader + uniforms + textures)
- Uses descriptor sets for GPU binding
- Supports material inheritance/variants

**Interface:**
```cpp
namespace vks {

enum class MaterialParameter {
    ALBEDO,
    NORMAL,
    METALLIC,
    ROUGHNESS,
    AMBIENT_OCCLUSION,
    EMISSION,
    HEIGHT,
};

struct MaterialData {
    glm::vec4 albedo_color = {1, 1, 1, 1};
    float metallic = 0.0f;
    float roughness = 0.8f;
    float ao = 1.0f;
    glm::vec3 emission = {0, 0, 0};
};

class Material {
public:
    Material() = default;
    ~Material() { destroy(); }
    
    // Create material with shader
    static Material create(VkDevice device, const VulkanShader& shader);
    
    // Set textures
    void set_texture(MaterialParameter param, const VulkanImage& texture);
    
    // Set uniform data
    void set_data(const MaterialData& data);
    
    // Bind for rendering
    void bind(VkCommandBuffer cmd, VkPipelineLayout layout);
    
    // Getters
    const VulkanShader* get_shader() const { return shader; }
    VkDescriptorSet get_descriptor_set() const { return descriptor_set; }

private:
    VkDevice device = VK_NULL_HANDLE;
    const VulkanShader* shader = nullptr;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VulkanBuffer uniform_buffer;
    std::map<MaterialParameter, const VulkanImage*> textures;
    
    void destroy();
};

}  // namespace vks
```

---

## COMPONENT 5: GLFW MESH LOADER

### File: `engine/renderer/utils/gltf_loader.h`

**Purpose:** Load glTF 2.0 meshes into VulkanMesh format.

**Design:**
- Use tinygltf library (already available in third_party)
- Parse glTF JSON + bin/GLB format
- Convert to Vertex format (position, normal, uv, tangent)
- Extract materials and create Material objects

**Interface:**
```cpp
namespace vks {

struct LoadedModel {
    std::vector<VulkanMesh> meshes;
    std::vector<Material> materials;
    std::vector<VulkanImage> textures;
};

class glTFLoader {
public:
    static LoadedModel load(const std::string& path, VkDevice device, 
                           VkPhysicalDevice pdev);
};

}  // namespace vks
```

---

## COMPONENT 6: RENDER GRAPH (SIMPLIFIED FOR PHASE 3)

### File: `engine/renderer/gpu/vulkan/render_graph.h`

**Purpose:** Declarative render pass dependency graph with automatic barrier insertion.

**Design:**
- Simplified version for Phase 3 (full version in Phase 4)
- Declare passes and resource reads/writes
- Auto-generate barriers based on access patterns

**Interface:**
```cpp
namespace vks {

enum class ResourceAccess {
    WRITE_COLOR,
    WRITE_DEPTH,
    READ_SAMPLER,
    READ_STORAGE,
};

struct RenderGraphPass {
    std::string name;
    VkRenderPass render_pass;
    std::vector<std::pair<std::string, ResourceAccess>> accesses;  // resource -> access
};

class RenderGraph {
public:
    void add_pass(const RenderGraphPass& pass);
    void build();  // Analyzes dependencies, generates barriers
    void execute(VkCommandBuffer cmd);
    
private:
    std::vector<RenderGraphPass> passes;
    std::map<std::string, ResourceAccess> resource_accesses;
};

}  // namespace vks
```

---

## IMPLEMENTATION ROADMAP

### Week 1 (Days 1-5): Buffer & Image Abstraction
**Goal:** Complete VulkanBuffer and VulkanImage with basic file loading

1. **Day 1-2:** VulkanBuffer implementation
   - Create buffer.h/cpp
   - Implement create(), update(), upload()
   - Test with simple vertex data

2. **Day 3-4:** VulkanImage implementation
   - Create image.h/cpp
   - Add stb_image integration (already in third_party)
   - Test PNG/JPG loading

3. **Day 5:** Integration test
   - Load texture in window test
   - Display on fullscreen quad
   - Commit: "feat: implement VulkanBuffer and VulkanImage"

### Week 2 (Days 6-10): Mesh System & glTF Loading
**Goal:** Load glTF models and render them

1. **Day 6-7:** VulkanMesh implementation
   - Create mesh.h/cpp
   - Implement multi-submesh support
   - Test with simple geometry

2. **Day 8-9:** glTF loader implementation
   - Create gltf_loader.h/cpp
   - Add tinygltf integration
   - Parse geometry, materials, textures

3. **Day 10:** Integration
   - Load sample glTF model
   - Commit: "feat: implement mesh loading and glTF support"

### Week 3 (Days 11-15): Material System & Binding
**Goal:** Complete material system with descriptor sets

1. **Day 11-12:** Material implementation
   - Create material.h/cpp
   - Setup descriptor set binding
   - Implement set_texture(), set_data()

2. **Day 13-14:** Descriptor set allocator
   - Use Phase 1 descriptor allocator
   - Implement layout caching
   - Test material binding

3. **Day 15:** Integration test
   - Render mesh with material
   - Apply textures
   - Commit: "feat: implement material system"

### Week 4 (Days 16-20): Render Graph & Final Integration
**Goal:** Complete Phase 3 with full mesh+material+texture rendering

1. **Day 16-17:** Render graph (simplified)
   - Create render_graph.h/cpp
   - Implement pass dependency tracking
   - Auto-barrier generation

2. **Day 18-19:** Phase 3 test executable
   - Create mesh_rendering_test.cpp
   - Load complex glTF model
   - Render with textures and materials
   - Test frame performance

3. **Day 20:** Polish & documentation
   - Update CMakeLists.txt with tinygltf
   - Write Phase 3 API reference
   - Update engine architecture docs
   - Commit: "feat: phase-3-complete"

---

## INTEGRATION CHECKLIST

### Prerequisites
- [ ] Phase 2 complete (shader compiler, render pass, pipeline)
- [ ] VMA working (Phase 1)
- [ ] Descriptor allocator working (Phase 1)

### New Dependencies
- [ ] tinygltf (already in third_party/)
- [ ] stb_image (add to third_party/stb/)
- [ ] tinygltf integrated in CMakeLists.txt

### Code Changes
- [ ] New directory: `engine/renderer/gpu/vulkan/buffer.h/cpp`
- [ ] New directory: `engine/renderer/gpu/vulkan/image.h/cpp`
- [ ] New directory: `engine/renderer/gpu/vulkan/mesh.h/cpp`
- [ ] New directory: `engine/renderer/gpu/vulkan/material.h/cpp`
- [ ] New directory: `engine/renderer/utils/gltf_loader.h/cpp`
- [ ] New file: `tests/mesh_rendering_test.cpp`
- [ ] Updated: `engine/renderer/CMakeLists.txt`
- [ ] Updated: `root CMakeLists.txt`

### Build Validation
- [ ] `cmake --build . --target mesh_rendering_test`
- [ ] Test executable runs without crashes
- [ ] No validation layer errors
- [ ] All 27 original tests pass

### Visual Validation
- [ ] Mesh renders on screen
- [ ] Textures apply correctly
- [ ] Normals affect lighting (if applicable)
- [ ] No obvious artifacts

---

## RESOURCE ALLOCATION

### GPU Memory
- Vertex buffers: GPU-only (fast)
- Index buffers: GPU-only (fast)
- Uniform buffers: HOST_VISIBLE (CPU updates per-frame)
- Textures: GPU-only (sampled)

### CPU Optimization
- Pre-allocate staging buffers (Phase 5)
- Batch texture uploads
- Material caching (same material = 1 descriptor set)

---

## SUCCESS CRITERIA

✅ **Week 1:** VulkanBuffer and VulkanImage working, texture loads from PNG  
✅ **Week 2:** glTF mesh loads and renders (untextured)  
✅ **Week 3:** Material system binds textures to mesh  
✅ **Week 4:** Complex mesh+material+texture rendering working  

**Final Success:** mesh_rendering_test.exe renders a textured 3D model (e.g., Sponza) with no validation errors.

---

## NEXT STEPS (Phase 4)

After Phase 3 is complete:
1. **Phase 4 Preview:** Deferred rendering pipeline
   - Extend RenderGraph for complex pass dependencies
   - Implement G-Buffer attachment targets
   - Add lighting pass for dynamic lights

---

**Version:** 1.0  
**Status:** Ready for Implementation  
**Estimated Start:** After Phase 2 completion
