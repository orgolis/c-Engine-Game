#pragma once

// Use a relative path so we always pick up the new gpu/render_device.h
// (the legacy renderer/include/render_device.h shares a filename and is
// resolved first when we use a bare include).
#include <vulkan/vulkan.h>

#include <memory>
#include <queue>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include "../render_device.h"

namespace gws::renderer::gpu {

// ============================================================================
// Vulkan-Specific Forward Declarations
// ============================================================================

class VulkanBuffer;
class VulkanImage;
class VulkanShader;
class VulkanPipeline;
class VulkanCommandBuffer;
class VulkanSwapchain;
struct QueueFamilyIndices;

// ============================================================================
// Vulkan Command Buffer
// ============================================================================

class VulkanCommandBuffer : public CommandBuffer {
public:
    VkCommandBuffer get_vk_command_buffer() const {
        return command_buffer;
    }

private:
    friend class VulkanDevice;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    bool is_recording = false;
};

// ============================================================================
// Vulkan Buffer
// ============================================================================

class VulkanBuffer {
public:
    VkBuffer get_vk_buffer() const {
        return buffer;
    }
    VkDeviceMemory get_memory() const {
        return memory;
    }

private:
    friend class VulkanDevice;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

// ============================================================================
// Vulkan Image
// ============================================================================

class VulkanImage {
public:
    VkImage get_vk_image() const {
        return image;
    }
    VkImageView get_vk_image_view() const {
        return image_view;
    }
    VkFormat get_format() const {
        return format;
    }

private:
    friend class VulkanDevice;
    VkImage image = VK_NULL_HANDLE;
    VkImageView image_view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
};

// ============================================================================
// Vulkan Shader
// ============================================================================

class VulkanShader {
public:
    VkShaderModule get_vk_shader_module() const {
        return shader_module;
    }
    const std::string& get_entry_point() const {
        return entry_point;
    }
    uint32_t get_stage() const {
        return stage;
    }

private:
    friend class VulkanDevice;
    VkShaderModule shader_module = VK_NULL_HANDLE;
    std::string entry_point;
    uint32_t stage = 0;
};

// ============================================================================
// Vulkan Pipeline
// ============================================================================

class VulkanPipeline {
public:
    VkPipeline get_vk_pipeline() const {
        return pipeline;
    }
    VkPipelineLayout get_vk_layout() const {
        return layout;
    }

private:
    friend class VulkanDevice;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
};

// ============================================================================
// Vulkan Fence & Semaphore
// ============================================================================

class VulkanFence {
public:
    VkFence get_vk_fence() const {
        return fence;
    }

private:
    friend class VulkanDevice;
    VkFence fence = VK_NULL_HANDLE;
};

class VulkanSemaphore {
public:
    VkSemaphore get_vk_semaphore() const {
        return semaphore;
    }

private:
    friend class VulkanDevice;
    VkSemaphore semaphore = VK_NULL_HANDLE;
};

// ============================================================================
// Main Vulkan Device Implementation
// ============================================================================

class VulkanDevice : public RenderDevice {
public:
    VulkanDevice();
    ~VulkanDevice() override;

    // ========================================================================
    // Device Lifecycle
    // ========================================================================

    void initialize(const RenderConfig& config) override;
    void shutdown() override;
    void wait_idle() override;

    // ========================================================================
    // Swapchain Management
    // ========================================================================

    SwapchainInfo get_swapchain_info() const override;
    uint32_t acquire_next_image(Handle<Semaphore> signal_semaphore) override;
    void present_image(uint32_t image_index, Handle<Semaphore> wait_semaphore) override;
    void recreate_swapchain(uint32_t width, uint32_t height) override;
    Handle<Image> get_current_swapchain_image() override;
    uint32_t get_swapchain_format() const override;

    // ========================================================================
    // Command Buffer Management
    // ========================================================================

    CommandBuffer* begin_command_buffer() override;
    void submit_command_buffer(CommandBuffer* cmd_buffer, Handle<Fence> signal_fence) override;
    void wait_fence(Handle<Fence> fence) override;
    void reset_fence(Handle<Fence> fence) override;

    // ========================================================================
    // Resource Creation
    // ========================================================================

    Handle<Buffer> create_buffer(const BufferInfo& info) override;
    void destroy_buffer(Handle<Buffer> buffer) override;
    void* map_buffer(Handle<Buffer> buffer) override;
    void unmap_buffer(Handle<Buffer> buffer) override;

    Handle<Image> create_image(const ImageInfo& info) override;
    void destroy_image(Handle<Image> image) override;

    Handle<Shader> create_shader(const ShaderStageInfo& info) override;
    void destroy_shader(Handle<Shader> shader) override;

    Handle<Pipeline> create_graphics_pipeline(const GraphicsPipelineInfo& info) override;
    void destroy_pipeline(Handle<Pipeline> pipeline) override;

    Handle<Fence> create_fence(bool signaled = false) override;
    void destroy_fence(Handle<Fence> fence) override;

    Handle<Semaphore> create_semaphore() override;
    void destroy_semaphore(Handle<Semaphore> semaphore) override;

    // ========================================================================
    // Rendering Commands
    // ========================================================================

    void begin_render_pass(CommandBuffer* cmd_buffer, const glm::vec4& clear_color) override;
    void end_render_pass(CommandBuffer* cmd_buffer) override;

    void bind_pipeline(CommandBuffer* cmd_buffer, Handle<Pipeline> pipeline) override;

    void draw(CommandBuffer* cmd_buffer, uint32_t vertex_count, uint32_t instance_count = 1, uint32_t first_vertex = 0,
              uint32_t first_instance = 0) override;

    void draw_indexed(CommandBuffer* cmd_buffer, uint32_t index_count, uint32_t instance_count = 1,
                      uint32_t first_index = 0, int32_t vertex_offset = 0, uint32_t first_instance = 0) override;

    void set_viewport(CommandBuffer* cmd_buffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;

    void set_scissor(CommandBuffer* cmd_buffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;

    // ========================================================================
    // Debugging & Profiling
    // ========================================================================

    std::string get_device_name() const override;

    void set_resource_name(Handle<Buffer> buffer, const std::string& name) override;
    void set_resource_name(Handle<Image> image, const std::string& name) override;

    void set_debug_enabled(bool enabled) override;

    // ========================================================================
    // Vulkan-Specific Methods
    // ========================================================================

    /// Get Vulkan instance
    VkInstance get_vk_instance() const {
        return instance;
    }

    /// Get Vulkan physical device
    VkPhysicalDevice get_vk_physical_device() const {
        return physical_device;
    }

    /// Get Vulkan logical device
    VkDevice get_vk_device() const {
        return device;
    }

    /// Short-form accessors used by Phase 3+ components.
    VkDevice get_device() const {
        return device;
    }
    VkPhysicalDevice get_physical_device() const {
        return physical_device;
    }

    /// Get graphics queue
    VkQueue get_graphics_queue() const {
        return graphics_queue;
    }
    uint32_t get_graphics_queue_family() const {
        return graphics_queue_family;
    }
    VkCommandPool get_command_pool() const {
        return command_pool;
    }

    /// Find a memory type matching the requested property flags. Throws on failure.
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const;

    /// True when `samplerAnisotropy` was enabled at logical-device creation.
    /// Sampler creation must gate `anisotropyEnable` on this.
    bool anisotropy_enabled() const { return anisotropy_enabled_; }

    /// True when multiDrawIndirect was actually GRANTED at device creation.
    /// Callers must ask: issuing drawCount > 1 without it is undefined
    /// behaviour rather than a reported error, so it fails silently and
    /// differently on every driver.
    bool     multi_draw_indirect_enabled() const { return multi_draw_indirect_enabled_; }
    uint32_t max_draw_indirect_count()     const { return max_draw_indirect_count_; }

    /// Device's `maxSamplerAnisotropy` limit (1.0 when anisotropy is disabled).
    /// Callers must clamp their requested anisotropy to this.
    float max_anisotropy() const { return max_anisotropy_; }

    /// True when the device supports hardware ray tracing via VK_KHR_ray_query
    /// + VK_KHR_acceleration_structure (and the required dependencies). When
    /// false, RT features will not be enabled even if the engine config
    /// requests them — call sites should fall back to the raster path.
    bool has_ray_tracing() const { return ray_tracing_supported_; }

    /// Resolved RT extension function pointers. Only valid when
    /// `has_ray_tracing()` returns true — call sites must check first.
    const struct VulkanRtFunctions* get_rt_functions() const {
        return ray_tracing_supported_ ? rt_functions_.get() : nullptr;
    }

    /// Shared VkPipelineCache, loaded from disk at startup (if a compatible
    /// cache file exists) and saved back at shutdown. Pass this to every
    /// vkCreate{Graphics,Compute}Pipelines call instead of VK_NULL_HANDLE so
    /// the driver's shader compilation/linking work is reused across runs
    /// instead of being repeated -- silently, in the driver, at whatever
    /// moment a pipeline is first actually used -- every time the process
    /// restarts. Never null after `initialize()` succeeds.
    VkPipelineCache get_pipeline_cache() const { return pipeline_cache_; }

    /// Attach an externally-created surface (typically from `glfwCreateWindowSurface`).
    /// The device takes ownership for shutdown — the caller must NOT destroy it.
    /// Returns false if attached after a previous surface or if the graphics queue
    /// family doesn't support presentation on the given surface.
    bool attach_surface(VkSurfaceKHR new_surface);

    /// Create a `VulkanSwapchain` on the previously attached surface.
    /// Must be called after `attach_surface`. Returns false if no surface is attached.
    bool create_window_swapchain(uint32_t width, uint32_t height);

    /// Access the owned swapchain (null until `create_window_swapchain` succeeds).
    VulkanSwapchain* get_swapchain() const {
        return swapchain.get();
    }

private:
    // ========================================================================
    // Initialization Helpers
    // ========================================================================

    void create_instance(RenderConfig& config);   // may clear enable_validation if the layer is absent
    void setup_debug_messenger();
    void pick_physical_device();
    void create_logical_device();
    void create_swapchain(void* window_handle,  // HWND on Windows
                          uint32_t width, uint32_t height);
    void create_command_pool();
    void create_pipeline_cache();
    void save_pipeline_cache();

    // ========================================================================
    // Vulkan Object Management
    // ========================================================================

    template <typename T>
    uint64_t allocate_handle();

    template <typename T>
    T* get_resource(Handle<T> handle);

    template <typename T>
    void free_resource(Handle<T> handle);

    // ========================================================================
    // Member Variables
    // ========================================================================

    // Instance & Device
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    // Queues
    VkQueue graphics_queue = VK_NULL_HANDLE;
    VkQueue present_queue = VK_NULL_HANDLE;
    uint32_t graphics_queue_family = ~0u;
    uint32_t present_queue_family = ~0u;

    // Swapchain
    std::unique_ptr<VulkanSwapchain> swapchain;
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    // Command Pool
    VkCommandPool command_pool = VK_NULL_HANDLE;

    // Cross-run pipeline cache. See get_pipeline_cache().
    VkPipelineCache pipeline_cache_ = VK_NULL_HANDLE;

    // Configuration
    RenderConfig config;
    bool debug_enabled = false;

    // Set to true at device-creation time if all four RT KHR extensions
    // were available and the rayQuery + accelerationStructure features
    // were enabled on the logical device. See `has_ray_tracing()`.
    bool ray_tracing_supported_ = false;

    // Set at logical-device creation when samplerAnisotropy was supported and
    // enabled. `max_anisotropy_` mirrors the device's limit. See
    // `anisotropy_enabled()` / `max_anisotropy()`.
    bool  anisotropy_enabled_ = false;
    bool  multi_draw_indirect_enabled_ = false;
    uint32_t max_draw_indirect_count_ = 1;
    float max_anisotropy_     = 1.0f;

    // Resolved RT function pointers — populated after logical device
    // creation when ray_tracing_supported_ is true.
    std::unique_ptr<struct VulkanRtFunctions> rt_functions_;

    // Resource Management
    std::unordered_map<uint64_t, std::unique_ptr<VulkanBuffer>> buffers;
    std::unordered_map<uint64_t, std::unique_ptr<VulkanImage>> images;
    std::unordered_map<uint64_t, std::unique_ptr<VulkanShader>> shaders;
    std::unordered_map<uint64_t, std::unique_ptr<VulkanPipeline>> pipelines;
    std::unordered_map<uint64_t, std::unique_ptr<VulkanFence>> fences;
    std::unordered_map<uint64_t, std::unique_ptr<VulkanSemaphore>> semaphores;

    // Command Buffer Pool
    std::vector<std::unique_ptr<VulkanCommandBuffer>> command_buffers;
    std::queue<VulkanCommandBuffer*> available_command_buffers;

    // Handle allocation counter
    uint64_t next_handle_id = 1;
};

}  // namespace gws::renderer::gpu
