#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace gws::renderer::gpu {

// ============================================================================
// Forward Declarations
// ============================================================================

class RenderDevice;

/// Polymorphic base for backend command-buffer wrappers. Backends derive from
/// this so callers can pass `CommandBuffer*` through the abstract interface.
class CommandBuffer {
public:
    virtual ~CommandBuffer() = default;
};

// Resource types are referenced via Handle<T> below; they remain incomplete
// at the abstract-interface level since handles only carry an opaque id.
class Buffer;
class Image;
class Shader;
class Pipeline;
class Fence;
class Semaphore;

// ============================================================================
// Handle Types (Opaque References)
// ============================================================================

template<typename T>
class Handle {
public:
    Handle() = default;
    explicit Handle(uint64_t id) : id_(id) {}
    
    uint64_t get_id() const { return id_; }
    bool is_valid() const { return id_ != 0; }
    
    explicit operator bool() const { return is_valid(); }
    
private:
    uint64_t id_ = 0;
};

// ============================================================================
// Configuration & Structs
// ============================================================================

struct RenderConfig {
    uint32_t window_width = 1920;
    uint32_t window_height = 1080;
    bool enable_validation = true;
    bool enable_vsync = true;
    /// Whether to take ray tracing when the device offers it.
    ///
    /// Measured on an RTX 3060 with a trivial scene (18 draws, 96k triangles):
    /// the deferred lighting pass costs **10-12 ms** because its ray-query
    /// variant casts shadow rays per pixel, in a loop for soft shadows. That is
    /// ~94% of measured GPU time on a fast card. Turning it off was previously
    /// possible only via GWS_FORCE_NO_RT=1 -- an environment variable, and so
    /// unreachable for anyone launching from the Hub.
    bool enable_ray_tracing = true;
    uint32_t frames_in_flight = 2;
    std::string app_name = "GameWorldshaper";
};

struct SwapchainInfo {
    uint32_t width;
    uint32_t height;
    uint32_t image_count;
    float aspect_ratio() const {
        return static_cast<float>(width) / static_cast<float>(height);
    }
};

struct BufferInfo {
    uint64_t size;
    uint32_t usage_flags;  // VkBufferUsageFlagBits
    uint32_t memory_flags; // VMA allocation flags
    std::string debug_name;
};

struct ImageInfo {
    uint32_t width;
    uint32_t height;
    uint32_t format;          // VkFormat
    uint32_t usage_flags;     // VkImageUsageFlagBits
    uint32_t aspect_flags;    // VkImageAspectFlagBits
    uint32_t mip_levels = 1;
    uint32_t array_layers = 1;
    std::string debug_name;
};

struct ShaderStageInfo {
    uint32_t stage;                      // VkShaderStageFlagBits
    std::vector<uint32_t> spirv_code;    // Pre-compiled SPIR-V
    std::string entry_point = "main";
};

struct PipelineLayoutInfo {
    std::vector<uint32_t> push_constant_ranges;
    std::vector<uint32_t> descriptor_set_layouts;
};

struct GraphicsPipelineInfo {
    std::vector<ShaderStageInfo> shader_stages;
    PipelineLayoutInfo layout;
    uint32_t render_pass;  // Handle or ID
    uint32_t subpass = 0;
    
    // Vertex input state
    std::vector<uint32_t> vertex_bindings;
    std::vector<uint32_t> vertex_attributes;
    
    // Fixed-function state
    bool depth_test_enable = true;
    bool depth_write_enable = true;
    uint32_t polygon_mode = 0;  // VkPolygonMode::FILL
    std::string debug_name;
};

// ============================================================================
// RenderDevice - Abstract Graphics API Interface
// ============================================================================

class RenderDevice {
public:
    virtual ~RenderDevice() = default;
    
    // ========================================================================
    // Device Lifecycle
    // ========================================================================
    
    /// Initialize the render device with configuration
    /// Must be called before any rendering operations
    virtual void initialize(const RenderConfig& config) = 0;
    
    /// Cleanup and destroy the device
    virtual void shutdown() = 0;
    
    /// Wait for all GPU operations to complete
    virtual void wait_idle() = 0;
    
    // ========================================================================
    // Swapchain Management
    // ========================================================================
    
    /// Get current swapchain information
    virtual SwapchainInfo get_swapchain_info() const = 0;
    
    /// Acquire the next swapchain image for rendering
    /// Returns the image index to render to
    virtual uint32_t acquire_next_image(Handle<Semaphore> signal_semaphore) = 0;
    
    /// Present the rendered image to the display
    virtual void present_image(uint32_t image_index,
                              Handle<Semaphore> wait_semaphore) = 0;
    
    /// Recreate swapchain (called on window resize)
    virtual void recreate_swapchain(uint32_t width, uint32_t height) = 0;
    
    /// Get the current swapchain image for rendering
    virtual Handle<Image> get_current_swapchain_image() = 0;
    
    /// Get render target image format
    virtual uint32_t get_swapchain_format() const = 0;
    
    // ========================================================================
    // Command Buffer Management
    // ========================================================================
    
    /// Begin recording a new command buffer
    virtual CommandBuffer* begin_command_buffer() = 0;
    
    /// Submit command buffer for execution
    virtual void submit_command_buffer(CommandBuffer* cmd_buffer,
                                      Handle<Fence> signal_fence) = 0;
    
    /// Wait for a fence to complete
    virtual void wait_fence(Handle<Fence> fence) = 0;
    
    /// Reset a fence to unsignaled state
    virtual void reset_fence(Handle<Fence> fence) = 0;
    
    // ========================================================================
    // Resource Creation
    // ========================================================================
    
    /// Create a GPU buffer
    virtual Handle<Buffer> create_buffer(const BufferInfo& info) = 0;
    
    /// Destroy a GPU buffer
    virtual void destroy_buffer(Handle<Buffer> buffer) = 0;
    
    /// Map buffer memory for CPU access
    virtual void* map_buffer(Handle<Buffer> buffer) = 0;
    
    /// Unmap buffer memory
    virtual void unmap_buffer(Handle<Buffer> buffer) = 0;
    
    /// Create a GPU image
    virtual Handle<Image> create_image(const ImageInfo& info) = 0;
    
    /// Destroy a GPU image
    virtual void destroy_image(Handle<Image> image) = 0;
    
    /// Create a shader
    virtual Handle<Shader> create_shader(const ShaderStageInfo& info) = 0;
    
    /// Destroy a shader
    virtual void destroy_shader(Handle<Shader> shader) = 0;
    
    /// Create a graphics pipeline
    virtual Handle<Pipeline> create_graphics_pipeline(
        const GraphicsPipelineInfo& info) = 0;
    
    /// Destroy a pipeline
    virtual void destroy_pipeline(Handle<Pipeline> pipeline) = 0;
    
    /// Create a synchronization fence
    virtual Handle<Fence> create_fence(bool signaled = false) = 0;
    
    /// Destroy a fence
    virtual void destroy_fence(Handle<Fence> fence) = 0;
    
    /// Create a synchronization semaphore
    virtual Handle<Semaphore> create_semaphore() = 0;
    
    /// Destroy a semaphore
    virtual void destroy_semaphore(Handle<Semaphore> semaphore) = 0;
    
    // ========================================================================
    // Rendering Commands
    // ========================================================================
    
    /// Begin a render pass
    virtual void begin_render_pass(CommandBuffer* cmd_buffer,
                                  const glm::vec4& clear_color) = 0;
    
    /// End the current render pass
    virtual void end_render_pass(CommandBuffer* cmd_buffer) = 0;
    
    /// Bind a pipeline for rendering
    virtual void bind_pipeline(CommandBuffer* cmd_buffer,
                              Handle<Pipeline> pipeline) = 0;
    
    /// Draw vertices
    virtual void draw(CommandBuffer* cmd_buffer,
                     uint32_t vertex_count,
                     uint32_t instance_count = 1,
                     uint32_t first_vertex = 0,
                     uint32_t first_instance = 0) = 0;
    
    /// Draw indexed vertices
    virtual void draw_indexed(CommandBuffer* cmd_buffer,
                             uint32_t index_count,
                             uint32_t instance_count = 1,
                             uint32_t first_index = 0,
                             int32_t vertex_offset = 0,
                             uint32_t first_instance = 0) = 0;
    
    /// Set viewport
    virtual void set_viewport(CommandBuffer* cmd_buffer,
                             uint32_t x, uint32_t y,
                             uint32_t width, uint32_t height) = 0;
    
    /// Set scissor rectangle
    virtual void set_scissor(CommandBuffer* cmd_buffer,
                            uint32_t x, uint32_t y,
                            uint32_t width, uint32_t height) = 0;
    
    // ========================================================================
    // Debugging & Profiling
    // ========================================================================
    
    /// Get GPU name/driver information
    virtual std::string get_device_name() const = 0;
    
    /// Set debug name for resource (helps with debugging tools)
    virtual void set_resource_name(Handle<Buffer> buffer,
                                  const std::string& name) = 0;
    
    virtual void set_resource_name(Handle<Image> image,
                                  const std::string& name) = 0;
    
    /// Enable/disable debug output
    virtual void set_debug_enabled(bool enabled) = 0;
};

// ============================================================================
// Factory Function
// ============================================================================

/// Create a render device of the specified type
/// Supported types: "vulkan", "opengl"
std::unique_ptr<RenderDevice> create_render_device(const std::string& type);

}  // namespace gws::renderer::gpu
