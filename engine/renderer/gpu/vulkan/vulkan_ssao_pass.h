/**
 * @file vulkan_ssao_pass.h
 * @brief Screen-space ambient occlusion compute pass.
 *
 * Reads the G-Buffer (position, normal, depth) and writes a single-channel
 * R8_UNORM occlusion image, sampled by the deferred lighting pass and
 * multiplied into its ambient/IBL term. Runs between the Geometry and
 * Lighting stages.
 */
#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>

namespace gws::renderer::gpu {

class VulkanDevice;
class VulkanGBuffer;

struct SsaoConfig {
    // Hemisphere radius for the "near" sample group (crevice / contact AO).
    // The shader runs a second sample group at radius × 10 for large-scale
    // skylight occlusion (cave darkening, sealed rooms).
    float radius    = 0.8f;
    float bias      = 0.005f; // NDC z bias to suppress self-occlusion (near group)
    float intensity = 1.0f;   // scales final occlusion contrast
};

/// Selectable ambient-occlusion algorithm. All write the same R8 occlusion
/// image which is then bilateral-blurred and sampled by the lighting pass.
///   - SSAO/HBAO/HDAO/GTAO: screen-space (G-Buffer only)
///   - VXAO: voxel-cone-traced (world-space, needs the voxelizer)
///   - RT:   ray-traced against the scene BVH (world-space, needs RT GPU)
enum class AoTechnique { SSAO = 0, HBAO, HDAO, GTAO, VXAO, RT };

class VulkanSsaoPass {
public:
    static std::unique_ptr<VulkanSsaoPass> create(VulkanDevice* device,
                                                   VulkanGBuffer* g_buffer,
                                                   uint32_t width,
                                                   uint32_t height,
                                                   bool use_rt = false);

    VulkanSsaoPass() = default;
    ~VulkanSsaoPass();
    VulkanSsaoPass(const VulkanSsaoPass&) = delete;
    VulkanSsaoPass& operator=(const VulkanSsaoPass&) = delete;

    /// Dispatch the SSAO compute. `view` and `proj` are the camera matrices
    /// used by the shader to project sample points back onto the screen.
    void execute(VkCommandBuffer cmd,
                 const glm::mat4& view,
                 const glm::mat4& proj);

    /// View of the **blurred** R8 occlusion image, in SHADER_READ_ONLY_OPTIMAL
    /// after `execute`. Lighting pass should bind this. (Internally there's
    /// a raw SSAO output too, but it's pre-blur — use the blurred view.)
    VkImageView get_output_view() const { return blurred_view_; }
    VkSampler   get_output_sampler() const { return sampler_; }

    /// Resize when the swapchain extents change. Recreates the output
    /// image and rebinds the descriptor set.
    void resize(uint32_t width, uint32_t height);

    SsaoConfig& mutable_config() { return config_; }
    const SsaoConfig& config() const { return config_; }
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool is_enabled() const { return enabled_; }

    /// Bind the scene TLAS for RT-AO mode (binding 4). No-op otherwise.
    void set_tlas(VkAccelerationStructureKHR tlas);
    bool uses_rt() const { return technique_ == AoTechnique::RT; }

    /// Switch the AO algorithm at runtime. Rebuilds the compute pipeline +
    /// descriptors for the new technique (waits for device idle). RT/VXAO
    /// silently fall back to SSAO when unavailable.
    void set_technique(AoTechnique t);
    AoTechnique technique() const { return technique_; }
    bool rt_available() const { return rt_available_; }

private:
    bool initialize(VulkanDevice* device, VulkanGBuffer* g_buffer,
                    uint32_t width, uint32_t height);
    bool create_output_image();
    bool create_pipeline();
    bool create_descriptor_resources();
    bool create_blur_pipeline();
    bool create_blur_descriptor_resources();
    void write_blur_descriptor_set();
    void destroy_output_image();
    void destroy();
    void write_descriptor_set();

    VulkanDevice*  device_   = nullptr;
    VulkanGBuffer* g_buffer_ = nullptr;
    uint32_t       width_    = 0;
    uint32_t       height_   = 0;
    bool           enabled_  = true;
    AoTechnique    technique_ = AoTechnique::SSAO;
    bool           rt_available_ = false;
    VkAccelerationStructureKHR tlas_ = VK_NULL_HANDLE;
    SsaoConfig     config_{};

    // Rebuild just the compute DSL/pool/set/layout/pipeline for the current
    // technique_ (used on a runtime technique switch). Leaves the images +
    // blur intact.
    void destroy_compute_resources();
    bool build_compute_for_technique();

    VkImage        output_image_  = VK_NULL_HANDLE;
    VkDeviceMemory output_memory_ = VK_NULL_HANDLE;
    VkImageView    output_view_   = VK_NULL_HANDLE;
    VkSampler      sampler_       = VK_NULL_HANDLE;

    // Blurred output — the public view returned by get_output_view().
    VkImage        blurred_image_  = VK_NULL_HANDLE;
    VkDeviceMemory blurred_memory_ = VK_NULL_HANDLE;
    VkImageView    blurred_view_   = VK_NULL_HANDLE;

    // Previous frame's accumulated AO (RT temporal reprojection). The RT shader
    // samples this via the reprojected UV (binding 5); output_image_ is copied
    // into it at the end of each frame.
    VkImage        history_image_  = VK_NULL_HANDLE;
    VkDeviceMemory history_memory_ = VK_NULL_HANDLE;
    VkImageView    history_view_   = VK_NULL_HANDLE;

    VkDescriptorSetLayout dsl_      = VK_NULL_HANDLE;
    VkDescriptorPool      pool_     = VK_NULL_HANDLE;
    VkDescriptorSet       set_      = VK_NULL_HANDLE;
    VkPipelineLayout      layout_   = VK_NULL_HANDLE;
    VkPipeline            pipeline_ = VK_NULL_HANDLE;

    // Blur pipeline + descriptor set + DSL (separate from the SSAO compute).
    VkDescriptorSetLayout blur_dsl_      = VK_NULL_HANDLE;
    VkDescriptorPool      blur_pool_     = VK_NULL_HANDLE;
    VkDescriptorSet       blur_set_      = VK_NULL_HANDLE;
    VkPipelineLayout      blur_layout_   = VK_NULL_HANDLE;
    VkPipeline            blur_pipeline_ = VK_NULL_HANDLE;

    VkSampler             gbuffer_sampler_ = VK_NULL_HANDLE;

    // Temporal accumulation state (RT AO): when the camera is static, the RT AO
    // compute blends each frame into the persistent output image so the grain
    // converges. Reset when the camera moves or the image is recreated.
    glm::mat4 prev_view_proj_{1.0f};
    uint32_t  frame_counter_  = 0;
    bool      history_valid_  = false;
};

} // namespace gws::renderer::gpu
