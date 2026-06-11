/**
 * @file vulkan_vxao_pass.h
 * @brief Voxel-cone-traced ambient occlusion.
 *
 * Two stages per frame:
 *   1. voxelize(): rasterize the opaque draw list into a 3D occupancy grid
 *      (camera-centred volume), rendering three times along the dominant
 *      axes so surfaces of every orientation get marked.
 *   2. compute_ao(): cone-trace the grid from each visible pixel and write
 *      a single-channel occlusion image the lighting pass samples (same
 *      slot SSAO/RT-AO use).
 *
 * World-space, so it darkens sealed rooms / off-screen occlusion like RT
 * AO — the non-ray-traced member of the AO suite.
 */
#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace gws::renderer::gpu {

class VulkanDevice;
class VulkanGBuffer;
struct DrawItem;

class VulkanVxaoPass {
public:
    static std::unique_ptr<VulkanVxaoPass> create(VulkanDevice* device,
                                                   VulkanGBuffer* g_buffer,
                                                   uint32_t width,
                                                   uint32_t height);
    VulkanVxaoPass() = default;
    ~VulkanVxaoPass();
    VulkanVxaoPass(const VulkanVxaoPass&) = delete;
    VulkanVxaoPass& operator=(const VulkanVxaoPass&) = delete;

    /// Stage 1 — rebuild the voxel grid from this frame's opaque draws.
    /// The volume is a cube of `volume_size` world units centred on
    /// `camera_position`, so it follows the viewer.
    void voxelize(VkCommandBuffer cmd, const DrawItem* items, size_t count,
                  const glm::vec3& camera_position);

    /// Stage 2 — cone-trace the grid into the occlusion output image.
    void compute_ao(VkCommandBuffer cmd);

    /// Occlusion output (R8), SHADER_READ_ONLY after compute_ao. Lighting
    /// binds this when the VXAO technique is selected.
    VkImageView get_output_view() const { return ao_view_; }
    VkSampler   get_output_sampler() const { return ao_sampler_; }

private:
    bool initialize(VulkanDevice* device, VulkanGBuffer* g_buffer,
                    uint32_t width, uint32_t height);
    bool create_voxel_image();
    bool create_volume_ubo();
    bool create_voxelize_pipeline();
    bool create_ao_image();
    bool create_cone_pipeline();
    void destroy();

    VulkanDevice*  device_   = nullptr;
    VulkanGBuffer* g_buffer_ = nullptr;
    uint32_t       width_    = 0;
    uint32_t       height_   = 0;

    static constexpr uint32_t kGrid = 128;   // voxels per side
    float                     volume_size_ = 40.0f; // world units per side

    // 3D occupancy grid.
    VkImage        voxel_image_  = VK_NULL_HANDLE;
    VkDeviceMemory voxel_memory_ = VK_NULL_HANDLE;
    VkImageView    voxel_storage_view_ = VK_NULL_HANDLE; // image3D (write)
    VkImageView    voxel_sampled_view_ = VK_NULL_HANDLE; // sampler3D (read)
    VkSampler      voxel_sampler_      = VK_NULL_HANDLE;

    // Volume params UBO (host-visible).
    VkBuffer       vol_ubo_        = VK_NULL_HANDLE;
    VkDeviceMemory vol_ubo_memory_ = VK_NULL_HANDLE;
    void*          vol_ubo_mapped_ = nullptr;

    // Voxelization graphics pipeline (attachment-less).
    VkRenderPass          voxelize_rp_     = VK_NULL_HANDLE;
    VkFramebuffer         voxelize_fb_     = VK_NULL_HANDLE;
    VkDescriptorSetLayout voxelize_dsl_    = VK_NULL_HANDLE;
    VkDescriptorPool      voxelize_pool_   = VK_NULL_HANDLE;
    VkDescriptorSet       voxelize_set_    = VK_NULL_HANDLE;
    VkPipelineLayout      voxelize_layout_ = VK_NULL_HANDLE;
    VkPipeline            voxelize_pipe_   = VK_NULL_HANDLE;

    // Occlusion output image.
    VkImage        ao_image_   = VK_NULL_HANDLE;
    VkDeviceMemory ao_memory_  = VK_NULL_HANDLE;
    VkImageView    ao_view_    = VK_NULL_HANDLE;
    VkSampler      ao_sampler_ = VK_NULL_HANDLE;

    // Cone-trace compute pipeline.
    VkDescriptorSetLayout cone_dsl_    = VK_NULL_HANDLE;
    VkDescriptorPool      cone_pool_   = VK_NULL_HANDLE;
    VkDescriptorSet       cone_set_    = VK_NULL_HANDLE;
    VkPipelineLayout      cone_layout_ = VK_NULL_HANDLE;
    VkPipeline            cone_pipe_   = VK_NULL_HANDLE;
    VkSampler             gbuffer_sampler_ = VK_NULL_HANDLE;
};

} // namespace gws::renderer::gpu
