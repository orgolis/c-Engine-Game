/**
 * @file vulkan_g_buffer.h
 * @brief Vulkan implementation of G-Buffer for deferred rendering
 * 
 * Manages multiple render targets for geometry pass in deferred rendering pipeline.
 * Standard layout:
 * - Attachment 0: Position (world space) - RGBA16F
 * - Attachment 1: Normal (world space) + Roughness - RGBA16F
 * - Attachment 2: Albedo + Metallic - RGBA8
 * - Attachment 3: Material ID + AO + Emission + Depth - RGBA8
 * - Depth: Standard depth buffer - D32F
 */

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <cstdint>
#include <glm/glm.hpp>

namespace gws::renderer::gpu {

class VulkanImage;
class VulkanRenderPass;
class VulkanDevice;
class VulkanShaderRegistry;
class VulkanOcclusionCuller;
class VulkanHzbCuller;
struct DrawItem;

/**
 * @struct GeometryPushConstants
 * @brief Per-draw constants pushed to the geometry shader.
 *
 * One mat4 MVP fits comfortably under the 128-byte spec guarantee with
 * room left over for material parameters.
 */
struct GeometryPushConstants {
    glm::mat4 mvp;            // 64 bytes
    glm::vec4 albedo_metallic; // RGB albedo + A metallic — 16 bytes
};

/**
 * @enum GBufferFormat
 * @brief Storage format for G-Buffer textures
 */
enum class GBufferFormat {
    RGBA8,          // 8-bit RGBA
    RGBA16F,        // 16-bit float RGBA
    RGBA32F,        // 32-bit float RGBA
    RGB16F,         // 16-bit float RGB
    RG16F,          // 16-bit float RG
    R32F,           // 32-bit float R
    R8,             // 8-bit single channel
};

/**
 * @struct GBufferConfig
 * @brief Configuration for G-Buffer creation
 */
struct GBufferConfig {
    uint32_t width = 1920;
    uint32_t height = 1080;

    // Attachment formats
    GBufferFormat position_format = GBufferFormat::RGBA16F;      // World position + padding
    GBufferFormat normal_format = GBufferFormat::RGBA16F;        // Normal + roughness
    GBufferFormat albedo_format = GBufferFormat::RGBA8;          // Albedo + metallic
    GBufferFormat material_format = GBufferFormat::RGBA8;        // Material ID + AO + emission

    // Options
    bool use_msaa = false;
    uint32_t msaa_samples = 4;

    /// Optional per-material descriptor set layout (set=1) that the textured
    /// scene pipeline will be built against. Pass `Material::create_descriptor_set_layout(...)`
    /// and keep the layout alive for the G-Buffer's lifetime. When null, only
    /// the demo pipeline is built — `draw_items` will be a no-op.
    VkDescriptorSetLayout material_set_layout = VK_NULL_HANDLE;
};

class VulkanIndirectDrawBuffer;

/**
 * @class VulkanGBuffer
 * @brief Vulkan G-Buffer implementation for deferred rendering
 * 
 * Manages 4 color attachments + 1 depth attachment for deferred rendering geometry pass.
 */
class VulkanGBuffer {
public:
    /// Route meshlet draws through an indirect buffer (3.2).
    ///
    /// Optional and off by default: passing nullptr keeps the direct path
    /// exactly as it was, so the two can be compared on the same frame rather
    /// than on two builds. Comparability is the whole acceptance criterion for
    /// this item -- "it uses indirect draws" is not a result, "it draws the
    /// same triangles in fewer submissions" is.
    void set_indirect_draws(VulkanIndirectDrawBuffer* buf) { indirect_ = buf; }

    /// GPU submissions issued for meshlets last frame. With multi-draw this is
    /// one per submesh rather than one per meshlet.
    uint32_t last_indirect_submissions() const { return last_indirect_submissions_; }

    VulkanGBuffer() = default;
    ~VulkanGBuffer();
    
    // Deleted copy operations
    VulkanGBuffer(const VulkanGBuffer&) = delete;
    VulkanGBuffer& operator=(const VulkanGBuffer&) = delete;
    
    // Allow move operations
    VulkanGBuffer(VulkanGBuffer&&) = default;
    VulkanGBuffer& operator=(VulkanGBuffer&&) = default;
    
    /**
     * @brief Create G-Buffer with specified configuration
     * @param device Vulkan device
     * @param config G-Buffer configuration
     * @return Unique pointer to G-Buffer
     */
    static std::unique_ptr<VulkanGBuffer> create(VulkanDevice* device, 
                                                 const GBufferConfig& config);
    
    // Lifecycle
    /**
     * @brief Begin geometry pass (render to G-Buffer)
     */
    void begin_geometry_pass(VkCommandBuffer cmd);
    
    /**
     * @brief End geometry pass
     */
    void end_geometry_pass(VkCommandBuffer cmd);
    
    /**
     * @brief Begin lighting pass (read from G-Buffer)
     */
    void begin_lighting_pass(VkCommandBuffer cmd);
    
    /**
     * @brief End lighting pass
     */
    void end_lighting_pass(VkCommandBuffer cmd);
    
    /**
     * @brief Clear all G-Buffer attachments
     */
    void clear(VkCommandBuffer cmd, const glm::vec4& clear_color = glm::vec4(0.0f));
    
    /**
     * @brief Resize G-Buffer to new dimensions
     */
    void resize(VulkanDevice* device, uint32_t width, uint32_t height);
    
    // Attachment access
    /**
     * @brief Get position texture handle
     */
    VkImageView get_position_view() const { return position_view_; }
    
    /**
     * @brief Get normal texture handle
     */
    VkImageView get_normal_view() const { return normal_view_; }
    
    /**
     * @brief Get albedo texture handle
     */
    VkImageView get_albedo_view() const { return albedo_view_; }
    
    /**
     * @brief Get material texture handle
     */
    VkImageView get_material_view() const { return material_view_; }
    
    /**
     * @brief Get depth texture handle
     */
    VkImageView get_depth_view() const { return depth_view_; }
    
    /**
     * @brief Get framebuffer handle
     */
    VkFramebuffer get_framebuffer() const { return framebuffer_; }

    /**
     * @brief Get render pass handle
     */
    VkRenderPass get_render_pass() const { return render_pass_; }

    /**
     * @brief Bind the demo geometry pipeline + draw a single hardcoded
     *        triangle into the G-Buffer. Intended as a smoke-test
     *        surrogate for real scene submission. The MVP push constant
     *        is built from the supplied view/proj matrices.
     */
    void draw_demo_triangle(VkCommandBuffer cmd,
                            const glm::mat4& view,
                            const glm::mat4& proj);

    /**
     * @brief Bind the textured scene pipeline and iterate the supplied
     *        DrawItems, binding each item's Material at set=1 and pushing
     *        its MVP. No-op if `material_set_layout` was null at create time.
     *
     * `camera_position` is used for per-draw LOD selection: the LOD tier
     * with the highest `distance_threshold` not exceeding the world-space
     * distance from camera to draw origin is chosen. Items are issued in
     * submission order — caller is responsible for sorting (front-to-back,
     * by-material, etc.) if it matters.
     */
    void draw_items(VkCommandBuffer cmd,
                    const glm::mat4& view,
                    const glm::mat4& proj,
                    const glm::vec3& camera_position,
                    const DrawItem* draws,
                    size_t draw_count,
                    uint32_t* out_draw_calls = nullptr,
                    uint32_t* out_triangles  = nullptr,
                    /// Deprecated. Left for ABI compatibility; nullptr in
                    /// current builds (the per-query approach had a stall
                    /// hazard — replaced by VulkanHzbCuller).
                    VulkanOcclusionCuller* occlusion = nullptr,
                    /// HZB occlusion culler. When non-null, draws whose AABB
                    /// was reported occluded by the culler's previous-frame
                    /// HZB test are skipped entirely.
                    VulkanHzbCuller* hzb_culler = nullptr,
                    /// Optional view frustum (world-space planes). When
                    /// supplied, LOD 0 draws are split per-meshlet and each
                    /// meshlet's bounding sphere is frustum-tested before
                    /// submission — gives finer-grained culling than the
                    /// per-entity test in the render graph.
                    const class Frustum* meshlet_frustum = nullptr);
    
    /**
     * @brief Get width
     */
    uint32_t get_width() const { return config_.width; }
    
    /**
     * @brief Get height
     */
    uint32_t get_height() const { return config_.height; }

private:
    // Configuration
    GBufferConfig config_;
    VulkanDevice* device_ = nullptr;
    
    // Images
    VkImage position_image_ = VK_NULL_HANDLE;
    VkImage normal_image_ = VK_NULL_HANDLE;
    VkImage albedo_image_ = VK_NULL_HANDLE;
    VkImage material_image_ = VK_NULL_HANDLE;
    VkImage depth_image_ = VK_NULL_HANDLE;
    
    // Image views
    VkImageView position_view_ = VK_NULL_HANDLE;
    VkImageView normal_view_ = VK_NULL_HANDLE;
    VkImageView albedo_view_ = VK_NULL_HANDLE;
    VkImageView material_view_ = VK_NULL_HANDLE;
    VkImageView depth_view_ = VK_NULL_HANDLE;
    
    // Memory
    VkDeviceMemory position_memory_ = VK_NULL_HANDLE;
    VkDeviceMemory normal_memory_ = VK_NULL_HANDLE;
    VkDeviceMemory albedo_memory_ = VK_NULL_HANDLE;
    VkDeviceMemory material_memory_ = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
    
    // Render pass and framebuffer
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;

    // Geometry pipelines. Two of them: a demo pipeline (legacy, no UV, no
    // material descriptor) and a scene pipeline (full PBR vertex format,
    // per-material descriptor set at set=1, samples textures into the
    // G-Buffer). Both target the same render pass.
    std::unique_ptr<VulkanShaderRegistry> shader_registry_;
    VkPipeline       demo_pipeline_              = VK_NULL_HANDLE;
    VkPipelineLayout demo_pipeline_layout_       = VK_NULL_HANDLE;
    VkBuffer         demo_vertex_buffer_         = VK_NULL_HANDLE;
    VkDeviceMemory   demo_vertex_buffer_memory_  = VK_NULL_HANDLE;
    /// Two scene pipelines sharing layout / render pass / shaders — only
    /// the rasterizer's `cullMode` differs. `scene_pipeline_back_` culls
    /// back-faces (trusted CCW meshes: primitives, glTF), while
    /// `scene_pipeline_none_` renders both sides (untrusted-winding meshes:
    /// OBJ). draw_items picks per draw based on `Mesh::is_double_sided`.
    VkPipeline       scene_pipeline_back_        = VK_NULL_HANDLE;
    VkPipeline       scene_pipeline_none_        = VK_NULL_HANDLE;
    VkPipelineLayout scene_pipeline_layout_      = VK_NULL_HANDLE;

    /// Terrain splat pipeline. Shares `scene_pipeline_layout_` (same set=1
    /// material layout + ScenePushConstants) but uses the terrain splat-blend
    /// fragment shader. Cull-none (terrain is viewed from above but the brim
    /// can be grazed from below). Selected for `DrawItem::is_terrain` draws.
    /// VK_NULL_HANDLE when no material layout was supplied (draw_items skips).
    VkPipeline       terrain_pipeline_           = VK_NULL_HANDLE;

    // Helper functions
    VkFormat format_to_vk(GBufferFormat fmt);
    void create_render_pass();
    void create_framebuffer();
    void create_demo_pipeline();
    void create_scene_pipeline();
    void create_terrain_pipeline();
    void create_demo_vertex_buffer();
    void cleanup();

    VulkanIndirectDrawBuffer* indirect_ = nullptr;   // not owned; nullptr = direct path
    uint32_t last_indirect_submissions_ = 0;
};

} // namespace gws::renderer::gpu
