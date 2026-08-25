// Bring in Win32 Vulkan extension symbols (VK_KHR_WIN32_SURFACE_EXTENSION_NAME).
// Define VK_USE_PLATFORM_WIN32_KHR before including vulkan.h so the SDK pulls
// in vulkan_win32.h *with* the Vk* types already declared. <windows.h> must
// come first because vulkan_win32.h references HINSTANCE / HWND / HANDLE.
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <cstring>

#include "vulkan_rt_functions.h"

#include "vulkan_device.h"

#include <cstdlib>
#include "vulkan_swapchain.h"
#include "logging/logger.h"
#include <algorithm>
#include <set>
#include <stdexcept>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace gws::renderer::gpu {

// ============================================================================
// Debug Callback
// ============================================================================

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* user_data) {
    
    if (message_severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        GWS_LOG_WARN("[Vulkan] {}", callback_data->pMessage);
    } else {
        GWS_LOG_INFO("[Vulkan] {}", callback_data->pMessage);
    }
    
    return VK_FALSE;
}

// ============================================================================
// VulkanDevice Implementation
// ============================================================================

VulkanDevice::VulkanDevice() = default;

VulkanDevice::~VulkanDevice() {
    shutdown();
}

void VulkanDevice::initialize(const RenderConfig& config) {
    this->config = config;
    
    GWS_LOG_INFO("Initializing Vulkan device: {}", config.app_name);
    
    // Create Vulkan instance (may clear this->config.enable_validation if the
    // validation layer isn't installed on this machine).
    create_instance(this->config);

    // Setup debug messenger only if validation actually ended up enabled.
    if (this->config.enable_validation) {
        setup_debug_messenger();
    }
    
    // Select physical device
    pick_physical_device();
    
    // Create logical device
    create_logical_device();

    // Create command pool
    create_command_pool();

    // Load (or start) the cross-run pipeline cache. Must come after the
    // logical device exists; every subsequent vkCreate*Pipelines call in the
    // renderer should pass get_pipeline_cache() instead of VK_NULL_HANDLE.
    create_pipeline_cache();

    GWS_LOG_INFO("✅ Vulkan device initialized successfully");
    GWS_LOG_INFO("   GPU: {}", get_device_name());
}

void VulkanDevice::shutdown() {
    if (device != VK_NULL_HANDLE) {
        wait_idle();

        // Cleanup swapchain
        swapchain.reset();

        // Cleanup surface
        if (surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }

        // Cleanup command pool
        if (command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, command_pool, nullptr);
            command_pool = VK_NULL_HANDLE;
        }

        // Persist the pipeline cache before the device that owns it goes
        // away, so the NEXT run's first frame doesn't pay driver JIT cost
        // this run already paid.
        save_pipeline_cache();
        if (pipeline_cache_ != VK_NULL_HANDLE) {
            vkDestroyPipelineCache(device, pipeline_cache_, nullptr);
            pipeline_cache_ = VK_NULL_HANDLE;
        }

        // Cleanup resources
        buffers.clear();
        images.clear();
        shaders.clear();
        pipelines.clear();
        fences.clear();
        semaphores.clear();
        command_buffers.clear();
        
        // Cleanup device
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }
    
    // Cleanup debug messenger
    if (debug_messenger != VK_NULL_HANDLE) {
        auto destroy_func = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroy_func) {
            destroy_func(instance, debug_messenger, nullptr);
        }
        debug_messenger = VK_NULL_HANDLE;
    }
    
    // Cleanup instance
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
    
    GWS_LOG_INFO("✅ Vulkan device shut down");
}

namespace {
// %LOCALAPPDATA%/GameWorldshaper/cache/vulkan_pipeline_cache.bin — same
// %LOCALAPPDATA%/GameWorldshaper convention as the diagnostics directory
// (editor/src/main.cpp), so both live under one user-data root.
std::string pipeline_cache_path() {
    std::string dir;
    if (const char* la = std::getenv("LOCALAPPDATA"))
        dir = std::string(la) + "\\GameWorldshaper\\cache";
    else
        dir = "cache";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir + "/vulkan_pipeline_cache.bin";
}
}  // namespace

void VulkanDevice::create_pipeline_cache() {
    std::vector<char> initial_data;
    const std::string path = pipeline_cache_path();
    {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (f) {
            const std::streamsize size = f.tellg();
            if (size > 0) {
                initial_data.resize(static_cast<size_t>(size));
                f.seekg(0);
                f.read(initial_data.data(), size);
            }
        }
    }

    VkPipelineCacheCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    ci.initialDataSize = initial_data.size();
    ci.pInitialData    = initial_data.empty() ? nullptr : initial_data.data();
    // A cache file from a different GPU/driver fails the header check
    // vkCreatePipelineCache does internally; the driver just discards the
    // stale data and returns an empty cache rather than an error, so no
    // manual UUID check is needed here.
    if (vkCreatePipelineCache(device, &ci, nullptr, &pipeline_cache_) != VK_SUCCESS) {
        GWS_LOG_WARN("VulkanDevice: vkCreatePipelineCache failed; pipelines will not be cached across runs");
        pipeline_cache_ = VK_NULL_HANDLE;
        return;
    }
    GWS_LOG_INFO("Vulkan pipeline cache: {} ({} bytes loaded from {})",
                 initial_data.empty() ? "starting empty" : "warm-started",
                 initial_data.size(), path);
}

void VulkanDevice::save_pipeline_cache() {
    if (pipeline_cache_ == VK_NULL_HANDLE) return;
    size_t size = 0;
    if (vkGetPipelineCacheData(device, pipeline_cache_, &size, nullptr) != VK_SUCCESS || size == 0) return;
    std::vector<char> data(size);
    if (vkGetPipelineCacheData(device, pipeline_cache_, &size, data.data()) != VK_SUCCESS) return;
    std::ofstream f(pipeline_cache_path(), std::ios::binary | std::ios::trunc);
    if (f) f.write(data.data(), static_cast<std::streamsize>(size));
}

void VulkanDevice::wait_idle() {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
    }
}

bool VulkanDevice::attach_surface(VkSurfaceKHR new_surface) {
    if (instance == VK_NULL_HANDLE || physical_device == VK_NULL_HANDLE) {
        GWS_LOG_ERROR("attach_surface called before VulkanDevice::initialize");
        return false;
    }
    if (new_surface == VK_NULL_HANDLE) {
        GWS_LOG_ERROR("attach_surface called with VK_NULL_HANDLE");
        return false;
    }
    if (this->surface != VK_NULL_HANDLE) {
        GWS_LOG_WARN("attach_surface: replacing existing surface (resetting swapchain)");
        if (swapchain) swapchain.reset();
        vkDestroySurfaceKHR(instance, this->surface, nullptr);
        this->surface = VK_NULL_HANDLE;
    }

    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, graphics_queue_family,
                                        new_surface, &supported);
    if (!supported) {
        GWS_LOG_ERROR("attach_surface: graphics queue family {} doesn't support presentation",
                        graphics_queue_family);
        return false;
    }

    this->surface = new_surface;
    return true;
}

bool VulkanDevice::create_window_swapchain(uint32_t width, uint32_t height) {
    if (surface == VK_NULL_HANDLE) {
        GWS_LOG_ERROR("create_window_swapchain called before attach_surface");
        return false;
    }
    if (swapchain) {
        swapchain.reset();
    }
    swapchain = std::make_unique<VulkanSwapchain>(
        device, physical_device, surface,
        graphics_queue, present_queue,
        graphics_queue_family, present_queue_family);
    swapchain->initialize(width, height);
    return swapchain->get_vk_swapchain() != VK_NULL_HANDLE;
}

uint32_t VulkanDevice::find_memory_type(uint32_t type_filter,
                                        VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties mem_props{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        const bool type_ok = (type_filter & (1u << i)) != 0u;
        const bool props_ok =
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties;
        if (type_ok && props_ok) {
            return i;
        }
    }
    throw std::runtime_error("VulkanDevice::find_memory_type: no suitable memory type");
}

// ============================================================================
// Swapchain Management
// ============================================================================

SwapchainInfo VulkanDevice::get_swapchain_info() const {
    if (!swapchain) {
        return SwapchainInfo{0, 0, 0};
    }
    return SwapchainInfo{
        swapchain->get_width(),
        swapchain->get_height(),
        swapchain->get_image_count()
    };
}

uint32_t VulkanDevice::acquire_next_image(Handle<Semaphore> signal_semaphore) {
    if (!swapchain) {
        GWS_LOG_ERROR("Swapchain not initialized!");
        return ~0u;
    }
    
    VulkanSemaphore* sem = get_resource<VulkanSemaphore>(
        *reinterpret_cast<Handle<VulkanSemaphore>*>(&signal_semaphore));
    
    return swapchain->acquire_next_image(
        sem ? sem->get_vk_semaphore() : VK_NULL_HANDLE);
}

void VulkanDevice::present_image(uint32_t image_index,
                                Handle<Semaphore> wait_semaphore) {
    if (!swapchain) {
        GWS_LOG_ERROR("Swapchain not initialized!");
        return;
    }
    
    VulkanSemaphore* sem = get_resource<VulkanSemaphore>(
        *reinterpret_cast<Handle<VulkanSemaphore>*>(&wait_semaphore));
    
    swapchain->present_image(image_index,
                            sem ? sem->get_vk_semaphore() : VK_NULL_HANDLE);
}

void VulkanDevice::recreate_swapchain(uint32_t width, uint32_t height) {
    if (swapchain) {
        swapchain->recreate(width, height);
    }
}

Handle<Image> VulkanDevice::get_current_swapchain_image() {
    if (!swapchain) {
        return Handle<Image>();
    }
    // This should return a handle to the current frame's swapchain image
    // For now, return invalid handle
    return Handle<Image>();
}

uint32_t VulkanDevice::get_swapchain_format() const {
    if (!swapchain) {
        return 0;
    }
    return static_cast<uint32_t>(swapchain->get_format());
}

// ============================================================================
// Command Buffer Management
// ============================================================================

CommandBuffer* VulkanDevice::begin_command_buffer() {
    // Allocate or reuse command buffer
    VulkanCommandBuffer* cmd_buffer = nullptr;
    
    if (!available_command_buffers.empty()) {
        cmd_buffer = available_command_buffers.front();
        available_command_buffers.pop();
    } else {
        // Allocate new command buffer
        auto new_cmd = std::make_unique<VulkanCommandBuffer>();
        
        VkCommandBufferAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate_info.commandPool = command_pool;
        allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate_info.commandBufferCount = 1;
        
        vkAllocateCommandBuffers(device, &allocate_info, 
                                &new_cmd->command_buffer);
        
        cmd_buffer = new_cmd.get();
        command_buffers.push_back(std::move(new_cmd));
    }
    
    // Begin recording
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(cmd_buffer->get_vk_command_buffer(), &begin_info);
    cmd_buffer->is_recording = true;
    
    return cmd_buffer;
}

void VulkanDevice::submit_command_buffer(CommandBuffer* cmd_buffer,
                                        Handle<Fence> signal_fence) {
    auto vk_cmd = dynamic_cast<VulkanCommandBuffer*>(cmd_buffer);
    if (!vk_cmd) {
        return;
    }
    
    // End recording
    vkEndCommandBuffer(vk_cmd->get_vk_command_buffer());
    vk_cmd->is_recording = false;
    
    // Submit
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &vk_cmd->command_buffer;
    
    VulkanFence* fence = get_resource<VulkanFence>(
        *reinterpret_cast<Handle<VulkanFence>*>(&signal_fence));
    
    vkQueueSubmit(graphics_queue, 1, &submit_info,
                fence ? fence->get_vk_fence() : VK_NULL_HANDLE);
}

void VulkanDevice::wait_fence(Handle<Fence> fence) {
    VulkanFence* vk_fence = get_resource<VulkanFence>(
        *reinterpret_cast<Handle<VulkanFence>*>(&fence));
    
    if (!vk_fence) {
        return;
    }
    
    vkWaitForFences(device, 1, &vk_fence->fence, VK_TRUE, UINT64_MAX);
}

void VulkanDevice::reset_fence(Handle<Fence> fence) {
    VulkanFence* vk_fence = get_resource<VulkanFence>(
        *reinterpret_cast<Handle<VulkanFence>*>(&fence));
    
    if (!vk_fence) {
        return;
    }
    
    vkResetFences(device, 1, &vk_fence->fence);
}

// ============================================================================
// Resource Creation
// ============================================================================

Handle<Buffer> VulkanDevice::create_buffer(const BufferInfo& info) {
    // TODO: Implement with VMA
    return Handle<Buffer>();
}

void VulkanDevice::destroy_buffer(Handle<Buffer> buffer) {
    // TODO: Implement
}

void* VulkanDevice::map_buffer(Handle<Buffer> buffer) {
    // TODO: Implement
    return nullptr;
}

void VulkanDevice::unmap_buffer(Handle<Buffer> buffer) {
    // TODO: Implement
}

Handle<Image> VulkanDevice::create_image(const ImageInfo& info) {
    // TODO: Implement
    return Handle<Image>();
}

void VulkanDevice::destroy_image(Handle<Image> image) {
    // TODO: Implement
}

Handle<Shader> VulkanDevice::create_shader(const ShaderStageInfo& info) {
    // TODO: Implement
    return Handle<Shader>();
}

void VulkanDevice::destroy_shader(Handle<Shader> shader) {
    // TODO: Implement
}

Handle<Pipeline> VulkanDevice::create_graphics_pipeline(
    const GraphicsPipelineInfo& info) {
    // TODO: Implement
    return Handle<Pipeline>();
}

void VulkanDevice::destroy_pipeline(Handle<Pipeline> pipeline) {
    // TODO: Implement
}

Handle<Fence> VulkanDevice::create_fence(bool signaled) {
    auto fence = std::make_unique<VulkanFence>();
    
    VkFenceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    create_info.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;
    
    vkCreateFence(device, &create_info, nullptr, &fence->fence);
    
    uint64_t handle_id = allocate_handle<VulkanFence>();
    fences[handle_id] = std::move(fence);
    
    return Handle<Fence>(handle_id);
}

void VulkanDevice::destroy_fence(Handle<Fence> fence) {
    VulkanFence* vk_fence = get_resource<VulkanFence>(
        *reinterpret_cast<Handle<VulkanFence>*>(&fence));
    
    if (!vk_fence) {
        return;
    }
    
    vkDestroyFence(device, vk_fence->fence, nullptr);
    free_resource<VulkanFence>(*reinterpret_cast<Handle<VulkanFence>*>(&fence));
}

Handle<Semaphore> VulkanDevice::create_semaphore() {
    auto semaphore = std::make_unique<VulkanSemaphore>();
    
    VkSemaphoreCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    vkCreateSemaphore(device, &create_info, nullptr, &semaphore->semaphore);
    
    uint64_t handle_id = allocate_handle<VulkanSemaphore>();
    semaphores[handle_id] = std::move(semaphore);
    
    return Handle<Semaphore>(handle_id);
}

void VulkanDevice::destroy_semaphore(Handle<Semaphore> semaphore) {
    VulkanSemaphore* vk_sem = get_resource<VulkanSemaphore>(
        *reinterpret_cast<Handle<VulkanSemaphore>*>(&semaphore));
    
    if (!vk_sem) {
        return;
    }
    
    vkDestroySemaphore(device, vk_sem->semaphore, nullptr);
    free_resource<VulkanSemaphore>(*reinterpret_cast<Handle<VulkanSemaphore>*>(&semaphore));
}

// ============================================================================
// Rendering Commands
// ============================================================================

void VulkanDevice::begin_render_pass(CommandBuffer* cmd_buffer,
                                    const glm::vec4& clear_color) {
    // TODO: Implement
}

void VulkanDevice::end_render_pass(CommandBuffer* cmd_buffer) {
    // TODO: Implement
}

void VulkanDevice::bind_pipeline(CommandBuffer* cmd_buffer,
                                Handle<Pipeline> pipeline) {
    // TODO: Implement
}

void VulkanDevice::draw(CommandBuffer* cmd_buffer,
                        uint32_t vertex_count,
                        uint32_t instance_count,
                        uint32_t first_vertex,
                        uint32_t first_instance) {
    // TODO: Implement
}

void VulkanDevice::draw_indexed(CommandBuffer* cmd_buffer,
                                uint32_t index_count,
                                uint32_t instance_count,
                                uint32_t first_index,
                                int32_t vertex_offset,
                                uint32_t first_instance) {
    // TODO: Implement
}

void VulkanDevice::set_viewport(CommandBuffer* cmd_buffer,
                                uint32_t x, uint32_t y,
                                uint32_t width, uint32_t height) {
    // TODO: Implement
}

void VulkanDevice::set_scissor(CommandBuffer* cmd_buffer,
                              uint32_t x, uint32_t y,
                              uint32_t width, uint32_t height) {
    // TODO: Implement
}

// ============================================================================
// Debugging & Profiling
// ============================================================================

std::string VulkanDevice::get_device_name() const {
    if (physical_device == VK_NULL_HANDLE) {
        return "Unknown";
    }
    
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical_device, &properties);
    return properties.deviceName;
}

void VulkanDevice::set_resource_name(Handle<Buffer> buffer,
                                    const std::string& name) {
    // TODO: Implement with vkSetDebugUtilsObjectNameEXT
}

void VulkanDevice::set_resource_name(Handle<Image> image,
                                    const std::string& name) {
    // TODO: Implement with vkSetDebugUtilsObjectNameEXT
}

void VulkanDevice::set_debug_enabled(bool enabled) {
    debug_enabled = enabled;
}

// ============================================================================
// Private Implementation
// ============================================================================

// True if VK_LAYER_KHRONOS_validation is actually installed (i.e. the Vulkan SDK
// or the standalone validation layers are present on this machine).
static bool validation_layer_available() {
    uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS || count == 0)
        return false;
    std::vector<VkLayerProperties> layers(count);
    if (vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS)
        return false;
    for (const auto& l : layers)
        if (std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0)
            return true;
    return false;
}

void VulkanDevice::create_instance(RenderConfig& config) {
    // Application info
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = config.app_name.c_str();
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "GameWorldshaper";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    // Required extensions
    std::vector<const char*> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    };

    // Validation layers — ONLY enable when requested AND actually installed. On a
    // machine without the Vulkan SDK the layer is absent; requesting it anyway
    // makes vkCreateInstance fail with VK_ERROR_LAYER_NOT_PRESENT, which (with the
    // instance left null) crashed the editor immediately on other devices.
    std::vector<const char*> layers;
    if (config.enable_validation) {
        if (validation_layer_available()) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            layers.push_back("VK_LAYER_KHRONOS_validation");
            GWS_LOG_INFO("Vulkan validation layers enabled");
        } else {
            config.enable_validation = false;
            GWS_LOG_WARN("VK_LAYER_KHRONOS_validation not installed — running without validation");
        }
    }

    // Create instance
    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();
    create_info.enabledLayerCount = static_cast<uint32_t>(layers.size());
    create_info.ppEnabledLayerNames = layers.data();

    VkResult res = vkCreateInstance(&create_info, nullptr, &instance);
    if (res != VK_SUCCESS && !layers.empty()) {
        // The driver rejected the layer/extension anyway — retry without them
        // rather than proceed with a null instance.
        GWS_LOG_WARN("vkCreateInstance failed with validation ({}); retrying without it",
                     static_cast<int>(res));
        config.enable_validation = false;
        extensions.pop_back();   // drop VK_EXT_DEBUG_UTILS (pushed alongside the layer)
        create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        create_info.ppEnabledExtensionNames = extensions.data();
        create_info.enabledLayerCount = 0;
        create_info.ppEnabledLayerNames = nullptr;
        res = vkCreateInstance(&create_info, nullptr, &instance);
    }
    if (res != VK_SUCCESS) {
        instance = VK_NULL_HANDLE;
        GWS_LOG_ERROR("❌ vkCreateInstance failed ({}) — no compatible Vulkan driver present?",
                      static_cast<int>(res));
        return;
    }
    GWS_LOG_INFO("✅ Vulkan instance created");
}

void VulkanDevice::setup_debug_messenger() {
    VkDebugUtilsMessengerCreateInfoEXT create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    create_info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    create_info.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    create_info.pfnUserCallback = debug_callback;
    
    auto create_func = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    
    if (create_func) {
        create_func(instance, &create_info, nullptr, &debug_messenger);
        GWS_LOG_INFO("✅ Debug messenger enabled");
    }
}

void VulkanDevice::pick_physical_device() {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    
    if (device_count == 0) {
        GWS_LOG_ERROR("No Vulkan devices found!");
        return;
    }
    
    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance, &device_count, devices.data());
    
    // Choose discrete GPU if available, otherwise integrated
    int best_score = -1;
    for (const auto& dev : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);
        
        int score = 0;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 1000;
        } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
            score += 100;
        }
        
        if (score > best_score) {
            best_score = score;
            physical_device = dev;
        }
    }
    
    if (physical_device != VK_NULL_HANDLE) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physical_device, &props);
        GWS_LOG_INFO("✅ Selected GPU: {}", props.deviceName);
    }
}

void VulkanDevice::create_logical_device() {
    // Get queue families
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, 
                                            &queue_family_count, nullptr);
    
    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device,
                                            &queue_family_count,
                                            queue_families.data());
    
    // Find graphics queue family
    for (uint32_t i = 0; i < queue_families.size(); ++i) {
        if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphics_queue_family = i;
            break;
        }
    }
    
    present_queue_family = graphics_queue_family;  // Same queue
    
    // Create device
    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    std::set<uint32_t> unique_queue_families = {graphics_queue_family};
    
    float queue_priority = 1.0f;
    for (uint32_t queue_family : unique_queue_families) {
        VkDeviceQueueCreateInfo queue_create_info{};
        queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_info.queueFamilyIndex = queue_family;
        queue_create_info.queueCount = 1;
        queue_create_info.pQueuePriorities = &queue_priority;
        queue_create_infos.push_back(queue_create_info);
    }
    
    // Device features
    VkPhysicalDeviceFeatures device_features{};

    // Enable anisotropic filtering when the GPU supports it (near-universal).
    // Recorded on the device so the sampler cache only *requests* anisotropy
    // when it is actually enabled here — otherwise the validation layers flag
    // any sampler built with anisotropyEnable=VK_TRUE.
    {
        VkPhysicalDeviceFeatures supported{};
        vkGetPhysicalDeviceFeatures(physical_device, &supported);
        if (supported.samplerAnisotropy) {
            device_features.samplerAnisotropy = VK_TRUE;
            anisotropy_enabled_ = true;
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(physical_device, &props);
            max_anisotropy_ = props.limits.maxSamplerAnisotropy;
        }
    }

    // Multi-draw indirect (3.2). Lets one vkCmdDrawIndexedIndirect issue many
    // draws from a buffer, which is what turns the per-meshlet loop from N
    // draw calls into one. Guarded the same way as anisotropy: requesting an
    // unsupported feature is a device-creation failure, and drawCount > 1
    // without it is undefined behaviour rather than an error -- so the renderer
    // asks whether it was actually granted rather than assuming.
    {
        VkPhysicalDeviceFeatures supported{};
        vkGetPhysicalDeviceFeatures(physical_device, &supported);
        if (supported.multiDrawIndirect) {
            device_features.multiDrawIndirect = VK_TRUE;
            multi_draw_indirect_enabled_ = true;
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(physical_device, &props);
            max_draw_indirect_count_ = props.limits.maxDrawIndirectCount;
        }
        // drawIndirectFirstInstance is needed only if firstInstance != 0; the
        // commands built here always use 0, so it is deliberately not required.
    }

    // shaderInt64 (core VkPhysicalDeviceFeatures, NOT an extension feature):
    // required by every ray-query shader that addresses BLAS vertex/index
    // buffers via 64-bit device addresses -- ssr_rt.comp, ddgi_trace.comp,
    // ssao_rt.comp, lighting_pass.frag. GL_EXT_buffer_reference2's uint64_t
    // arithmetic lowers to SPIR-V OpTypeInt 64 (capability Int64), which
    // this feature gates. It is a DIFFERENT flag from
    // VK_KHR_buffer_device_address's `bufferDeviceAddress` (requested via
    // bda_features below), which only covers the pointer-indirection side,
    // not 64-bit integer arithmetic on the value.
    //
    // This was never requested, so every RT frame ran those shaders with an
    // unrequested SPIR-V capability -- undefined behaviour per the Vulkan
    // spec on every GPU this ever ran on. It went unnoticed because the
    // validation layer's callback (debug_callback, this file) logs through
    // GWS_LOG_WARN, which was a silent no-op for the process's entire life
    // until gws::logging::detail::default_spdlog_logger() gained its
    // spdlog::default_logger_raw() fallback -- so the warning about it was
    // being generated and discarded on every single run.
    bool shader_int64_supported = false;
    {
        VkPhysicalDeviceFeatures supported{};
        vkGetPhysicalDeviceFeatures(physical_device, &supported);
        shader_int64_supported = supported.shaderInt64 == VK_TRUE;
        if (shader_int64_supported) device_features.shaderInt64 = VK_TRUE;
    }

    // Device extensions — swapchain is required; the four KHR ray-tracing
    // extensions are added if the physical device advertises all of them.
    std::vector<const char*> extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    // Enumerate available device extensions so we can opt into the RT set
    // only when the GPU actually supports them.
    uint32_t available_count = 0;
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &available_count, nullptr);
    std::vector<VkExtensionProperties> available_exts(available_count);
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &available_count, available_exts.data());
    auto has_ext = [&](const char* name) {
        for (const auto& e : available_exts) {
            if (std::strcmp(e.extensionName, name) == 0) return true;
        }
        return false;
    };

    const bool rt_exts_present =
        has_ext(VK_KHR_RAY_QUERY_EXTENSION_NAME) &&
        has_ext(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
        has_ext(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) &&
        has_ext(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);

    // Feature chain for RT — must be pNext-chained off VkPhysicalDeviceFeatures2.
    VkPhysicalDeviceRayQueryFeaturesKHR rq_features{};
    rq_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR as_features{};
    as_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    VkPhysicalDeviceBufferDeviceAddressFeatures bda_features{};
    bda_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    // ---- Bindless textures (descriptor indexing) ---------------------------
    //
    // Core in Vulkan 1.2, and this instance asks for 1.3, so no extension is
    // needed -- but the FEATURES still have to be queried and enabled, and are
    // not universal. Terrain currently burns 14 descriptor bindings for one
    // splat map plus three maps across four layers, which is the whole reason
    // it needs a pipeline of its own. Indexing lets a material name a texture
    // by index instead, which is what makes ">4 terrain layers" and material
    // layering possible at all.
    //
    // Queried, gated, and forceable off. The engine has already shipped a build
    // that would not start on an RX 5700 because a capability was assumed, and
    // the lesson recorded from that is that a capability-dependent branch needs
    // a way to force it or it is untested by construction.
    VkPhysicalDeviceVulkan12Features v12_features{};
    v12_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    {
        VkPhysicalDeviceFeatures2 probe{};
        probe.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        probe.pNext = &v12_features;
        vkGetPhysicalDeviceFeatures2(physical_device, &probe);

        const char* force_off = std::getenv("GWS_FORCE_NO_BINDLESS");
        const bool  pretend_off = force_off && force_off[0] == '1';
        if (pretend_off)
            GWS_LOG_INFO("GWS_FORCE_NO_BINDLESS=1 — pretending this GPU has no descriptor indexing");

        // Every one of these is required by the bindless table. Partially-bound
        // matters as much as the rest: the array is sized for the maximum and
        // is legitimately full of holes, and without it every unused slot would
        // have to be written with a dummy or the device is in undefined state.
        const bool all_present =
            v12_features.descriptorIndexing &&
            v12_features.runtimeDescriptorArray &&
            v12_features.shaderSampledImageArrayNonUniformIndexing &&
            v12_features.descriptorBindingPartiallyBound &&
            v12_features.descriptorBindingSampledImageUpdateAfterBind &&
            v12_features.descriptorBindingVariableDescriptorCount;

        if (!pretend_off && all_present) {
            VkPhysicalDeviceDescriptorIndexingProperties di_props{};
            di_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;
            VkPhysicalDeviceProperties2 props2{};
            props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            props2.pNext = &di_props;
            vkGetPhysicalDeviceProperties2(physical_device, &props2);

            // Cap the table well below the device maximum. Some drivers report
            // millions, and allocating a descriptor pool for that is a waste
            // measured in hundreds of megabytes for a slot count no project
            // here will approach.
            const uint32_t device_max = di_props.maxPerStageDescriptorUpdateAfterBindSampledImages;
            max_bindless_textures_ = device_max < kBindlessTextureCap ? device_max
                                                                      : kBindlessTextureCap;
            bindless_supported_ = max_bindless_textures_ >= 64;

            if (bindless_supported_) {
                // Enable exactly what was checked. Enabling a feature that was
                // not queried is undefined behaviour, which is how the
                // shaderInt64 bug lived undetected for the life of the project.
                v12_features.descriptorIndexing = VK_TRUE;
                v12_features.runtimeDescriptorArray = VK_TRUE;
                v12_features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
                v12_features.descriptorBindingPartiallyBound = VK_TRUE;
                v12_features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
                v12_features.descriptorBindingVariableDescriptorCount = VK_TRUE;
                v12_features.pNext = nullptr;   // head of the chain; linked below
                GWS_LOG_INFO("✅ Bindless textures supported — {} slots (device max {})",
                             max_bindless_textures_, device_max);
            } else {
                GWS_LOG_WARN("Descriptor indexing present but only {} update-after-bind sampled "
                             "images — too few for a bindless table; using bound textures",
                             device_max);
            }
        } else if (!pretend_off) {
            GWS_LOG_INFO("Descriptor indexing unavailable on this device — using bound textures "
                         "(indexing={} runtimeArray={} nonUniform={} partiallyBound={} "
                         "updateAfterBind={} variableCount={})",
                         (bool)v12_features.descriptorIndexing,
                         (bool)v12_features.runtimeDescriptorArray,
                         (bool)v12_features.shaderSampledImageArrayNonUniformIndexing,
                         (bool)v12_features.descriptorBindingPartiallyBound,
                         (bool)v12_features.descriptorBindingSampledImageUpdateAfterBind,
                         (bool)v12_features.descriptorBindingVariableDescriptorCount);
        }
        if (!bindless_supported_) v12_features = {};   // enable nothing we did not check
        v12_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    }

    if (rt_exts_present) {
        // Chain everything and ask the GPU what it actually supports.
        features2.pNext = &rq_features;
        rq_features.pNext = &as_features;
        as_features.pNext = &bda_features;
        vkGetPhysicalDeviceFeatures2(physical_device, &features2);

        // GWS_FORCE_NO_RT=1 pretends the GPU has no ray tracing.
        //
        // Added because v0.6.2 crashed on an AMD RX 5700 and could not be
        // reproduced on any machine here: every dev GPU supports ray query, so
        // the non-RT code path had never once executed. A capability-dependent
        // path nobody can run locally is a path nobody tests, and this makes it
        // one command instead of one shopping trip.
        const char* force_no_rt = std::getenv("GWS_FORCE_NO_RT");
        const bool  pretend_no_rt = force_no_rt && force_no_rt[0] == '1';
        if (pretend_no_rt)
            GWS_LOG_INFO("GWS_FORCE_NO_RT=1 — pretending this GPU has no ray tracing");

        if (!pretend_no_rt &&
            rq_features.rayQuery && as_features.accelerationStructure &&
            bda_features.bufferDeviceAddress && shader_int64_supported) {
            extensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
            extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            extensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
            ray_tracing_supported_ = true;
            GWS_LOG_INFO("✅ Hardware ray tracing supported and enabled");
        } else if (!pretend_no_rt && rq_features.rayQuery && as_features.accelerationStructure &&
                   bda_features.bufferDeviceAddress && !shader_int64_supported) {
            GWS_LOG_WARN("Ray tracing extensions present but shaderInt64 is not — RT disabled "
                         "(the ray-query shaders need 64-bit integer arithmetic on buffer "
                         "device addresses)");
            features2.pNext = nullptr;
        } else {
            GWS_LOG_INFO("Ray tracing extensions present but required features missing — RT disabled");
            features2.pNext = nullptr;
        }
    } else {
        GWS_LOG_INFO("Ray tracing extensions not available on this device — RT disabled");
    }

    // Features2 UNCONDITIONALLY now. Bindless has to be chained whether or not
    // ray tracing is on, and the old shape -- pEnabledFeatures when RT was off,
    // the chain when it was on -- had no room for a second optional feature
    // block. The chain is built here so there is exactly one place that decides
    // what the device is created with.
    features2.features = device_features;
    if (bindless_supported_) {
        v12_features.pNext = features2.pNext;   // in front of the RT chain, if any
        features2.pNext    = &v12_features;
    }

    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = queue_create_infos.size();
    create_info.pQueueCreateInfos = queue_create_infos.data();
    create_info.enabledExtensionCount = extensions.size();
    create_info.ppEnabledExtensionNames = extensions.data();
    create_info.pNext = &features2;
    create_info.pEnabledFeatures = nullptr;   // mutually exclusive with Features2

    vkCreateDevice(physical_device, &create_info, nullptr, &device);

    // Resolve KHR ray-tracing function pointers if RT was enabled. A
    // resolve failure here means the driver advertised the extensions but
    // didn't actually export the entry points — we treat that as a fatal
    // RT setup failure and fall back to raster.
    if (ray_tracing_supported_) {
        rt_functions_ = std::make_unique<VulkanRtFunctions>();
        if (!rt_functions_->load(device)) {
            GWS_LOG_INFO("RT function-pointer resolution failed — disabling RT");
            rt_functions_.reset();
            ray_tracing_supported_ = false;
        }
    }

    // Get queues
    vkGetDeviceQueue(device, graphics_queue_family, 0, &graphics_queue);
    vkGetDeviceQueue(device, present_queue_family, 0, &present_queue);
    
    GWS_LOG_INFO("✅ Logical device created");
}

void VulkanDevice::create_command_pool() {
    VkCommandPoolCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    create_info.queueFamilyIndex = graphics_queue_family;
    
    vkCreateCommandPool(device, &create_info, nullptr, &command_pool);
    GWS_LOG_INFO("✅ Command pool created");
}

// ============================================================================
// Handle Management (Simplified)
// ============================================================================

template<typename T>
uint64_t VulkanDevice::allocate_handle() {
    return next_handle_id++;
}

template<typename T>
T* VulkanDevice::get_resource(Handle<T> handle) {
    // This is a simplified version - real implementation would be more robust
    return nullptr;
}

template<typename T>
void VulkanDevice::free_resource(Handle<T> handle) {
    // TODO: Implement proper cleanup
}

// ============================================================================
// Factory Function
// ============================================================================

std::unique_ptr<RenderDevice> create_render_device(const std::string& type) {
    if (type == "vulkan") {
        return std::make_unique<VulkanDevice>();
    }
    // TODO: Add OpenGL support
    return nullptr;
}

}  // namespace gws::renderer::gpu
