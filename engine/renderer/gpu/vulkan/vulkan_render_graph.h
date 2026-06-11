/**
 * @file vulkan_render_graph.h
 * @brief Declarative render graph for the deferred pipeline (Phase 4 Week 3).
 *
 * Wires the existing Vulkan deferred-rendering components — G-Buffer, shadow
 * maps, lighting pass, post-processing — into an ordered execution graph and
 * inserts the layout / synchronisation barriers between them.
 *
 * The graph is intentionally simple: a fixed list of stages whose ordering and
 * resource accesses are known at construction time. It is *not* a generalised
 * frame graph in the Frostbite sense — that comes in Phase 5.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gws::renderer::gpu {

class VulkanDevice;
class VulkanGBuffer;
class VulkanLightingPass;
class VulkanShadowMap;
class VulkanPostProcessing;
class VulkanTransparentPass;
class VulkanOcclusionCuller;
class VulkanHzbCuller;
class Mesh;
class Material;

/**
 * @struct DrawItem
 * @brief One renderable submission for the geometry / shadow stages.
 *
 * `mesh` and `material` are non-owning — the caller (typically a `Scene`
 * or test fixture) keeps them alive across the frame. `model` is the
 * world-space transform applied in the vertex shader.
 *
 * `submesh_index` selects which submesh within `mesh` to draw. Use
 * `Mesh::submeshes()` to enumerate available submeshes; pass 0 for
 * single-submesh meshes.
 */
struct DrawItem {
    const Mesh*     mesh          = nullptr;
    const Material* material      = nullptr;
    glm::mat4       model         = glm::mat4(1.0f);
    uint32_t        submesh_index = 0;
    /// True when this draw uses alpha blend (forward transparent pass) instead
    /// of the deferred G-Buffer path. Cutout-style alpha-test draws stay false
    /// (the G-Buffer fragment shader's `discard` handles them via the
    /// alpha_cutoff packed into `MaterialUniforms::emissive_factor.a`).
    bool            is_blend      = false;
};

/**
 * @enum RenderGraphStage
 * @brief Fixed pipeline stages of the deferred renderer, in execution order.
 */
enum class RenderGraphStage : uint32_t {
    Shadow = 0,        ///< Render shadow maps for shadow-casting lights
    Geometry,          ///< Geometry pass writes to the G-Buffer
    Lighting,          ///< Full-screen lighting reads G-Buffer + shadow maps; sky inline at empty pixels
    Transparent,       ///< Forward alpha-blended pass (reads lit HDR + depth)
    PostProcess,       ///< Post-processing chain (bloom / tonemap / TAA)
    StageCount
};

/**
 * @struct CameraData
 * @brief Per-frame view/projection state shared across stages.
 *
 * Geometry stage builds MVP from `view`/`proj`; lighting stage uses
 * `position` for view-direction reconstruction. Set via
 * `VulkanRenderGraph::set_camera` before each frame.
 */
struct CameraData {
    glm::mat4 view     = glm::mat4(1.0f);
    glm::mat4 proj     = glm::mat4(1.0f);
    glm::vec3 position = glm::vec3(0.0f);
};

/**
 * @struct RenderGraphConfig
 * @brief Inputs required to build a deferred render graph.
 *
 * Pointers may be null for stages the caller wants to skip (e.g. shadows
 * disabled, or post-processing turned off for headless tests).
 */
struct RenderGraphConfig {
    VulkanDevice* device = nullptr;
    VulkanGBuffer* g_buffer = nullptr;
    VulkanLightingPass* lighting = nullptr;
    VulkanShadowMap* shadow_map = nullptr;
    VulkanPostProcessing* post_processing = nullptr;
    VulkanTransparentPass* transparent = nullptr;
    /// Optional. When set, the Geometry stage wraps each draw in an
    /// occlusion query and skips draws that were fully hidden last frame.
    VulkanOcclusionCuller* occlusion_culler = nullptr;
    /// Optional. HZB-based occlusion culling — draws whose previous-frame
    /// AABB test failed are skipped, and the HZB is rebuilt at the end of
    /// each frame's geometry stage from the current depth buffer.
    VulkanHzbCuller* hzb_culler = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
};

/**
 * @struct RenderGraphStats
 * @brief Per-frame execution statistics emitted by the graph.
 *
 * The `*_us` fields hold per-stage GPU time in microseconds, populated by
 * `resolve_timings()` after the frame's fence has been waited on. They
 * remain 0 for stages that were skipped or for frames where timing wasn't
 * resolved (e.g. when the device lacks timestampComputeAndGraphics).
 */
struct RenderGraphStats {
    uint32_t frame_index = 0;
    uint32_t shadow_passes_run = 0;
    uint32_t geometry_passes_run = 0;
    uint32_t lighting_passes_run = 0;
    uint32_t transparent_passes_run = 0;
    uint32_t post_process_passes_run = 0;

    double shadow_us = 0.0;
    double geometry_us = 0.0;
    double lighting_us = 0.0;
    double transparent_us = 0.0;
    double post_process_us = 0.0;

    // CPU-side per-frame draw / culling counters. Populated by the default
    // geometry and shadow recorders when they iterate the draw list.
    uint32_t geometry_draw_calls = 0;
    uint32_t geometry_triangles  = 0;
    uint32_t shadow_draw_calls   = 0;
    uint32_t shadow_triangles    = 0;

    // Frustum-culling counters (geometry stage). Zero when culling is off.
    uint32_t frustum_input_items   = 0;
    uint32_t frustum_visible_items = 0;
    uint32_t frustum_culled_items  = 0;
};

/**
 * @class VulkanRenderGraph
 * @brief Orchestrates the deferred pipeline and inserts inter-stage barriers.
 *
 * Typical use per frame:
 *
 * @code
 *   graph.begin_frame(cmd);
 *   graph.execute_stage(cmd, RenderGraphStage::Shadow,   record_shadow_geometry);
 *   graph.execute_stage(cmd, RenderGraphStage::Geometry, record_scene_geometry);
 *   graph.execute_stage(cmd, RenderGraphStage::Lighting,  {});  // built-in fullscreen draw
 *   graph.execute_stage(cmd, RenderGraphStage::PostProcess, {});
 *   graph.end_frame(cmd);
 * @endcode
 */
class VulkanRenderGraph {
public:
    using StageRecorder = std::function<void(VkCommandBuffer)>;

    VulkanRenderGraph() = default;
    ~VulkanRenderGraph();

    VulkanRenderGraph(const VulkanRenderGraph&) = delete;
    VulkanRenderGraph& operator=(const VulkanRenderGraph&) = delete;
    VulkanRenderGraph(VulkanRenderGraph&&) = default;
    VulkanRenderGraph& operator=(VulkanRenderGraph&&) = default;

    /// Build a graph from the supplied configuration. Returns nullptr on
    /// invalid configuration (no device, no G-Buffer, mismatched extents).
    static std::unique_ptr<VulkanRenderGraph> create(const RenderGraphConfig& config);

    /// Validate a config without constructing the graph. Useful for tests.
    /// @return Empty string when valid; otherwise a human-readable reason.
    static std::string validate_config(const RenderGraphConfig& config);

    /// Set the per-frame camera state. Forwarded to the lighting pass on
    /// `begin_frame` and consumed by the geometry stage's default
    /// recorder. Caller may also read `get_camera()` from a custom recorder.
    void set_camera(const CameraData& camera);
    const CameraData& get_camera() const { return camera_; }

    /// Set the per-frame draw list. The default geometry and shadow
    /// recorders iterate this list; bind material descriptor sets at set=1
    /// for geometry; bind only mesh+MVP for shadows. Pass an empty vector
    /// to fall back to the legacy demo-triangle behaviour (smoke tests).
    void set_draw_items(std::vector<DrawItem> draws);
    const std::vector<DrawItem>& get_draw_items() const { return draw_items_; }

    /// Enable/disable frustum culling on the CPU before submission.
    /// Default is disabled (false) for backward compatibility. When enabled,
    /// draw items are tested against the view frustum and removed if
    /// entirely outside the view.
    void set_frustum_culling_enabled(bool enable) { frustum_culling_enabled_ = enable; }
    bool is_frustum_culling_enabled() const { return frustum_culling_enabled_; }

    /// Mark the start of a new frame. Resets per-frame stats.
    void begin_frame(VkCommandBuffer cmd);

    /// Execute a stage. The recorder is invoked between begin/end of that
    /// stage's render pass; pass an empty function to use the stage's default
    /// behaviour (for stages that own their draw commands, like Lighting).
    void execute_stage(VkCommandBuffer cmd,
                       RenderGraphStage stage,
                       const StageRecorder& recorder);

    /// Mark the end of the frame (final layout transitions for presentation).
    void end_frame(VkCommandBuffer cmd);

    /// Read back GPU timestamp queries written during the most recent frame
    /// and populate the per-stage `*_us` fields of `get_stats()`. The
    /// caller MUST have waited on the frame's submission fence before
    /// calling this — otherwise the queries may not yet be available.
    /// No-op (and returns false) if the device doesn't support timestamps
    /// or if no frame has run yet.
    bool resolve_timings();

    /// Pull the previous frame's per-draw occlusion query results from the
    /// GPU into the culler. Call after waiting on the previous frame's fence.
    void resolve_occlusion_queries();

    /// Feed resolved GPU timings to the GPUProfiler for display.
    /// Call this after resolve_timings() to push the resolved stage times
    /// to the profiler. No-op if timestamps aren't supported.
    void update_gpu_profiler();

    /// Resize all owned attachments. No-op if dimensions match.
    void resize(uint32_t width, uint32_t height);

    /// Check whether the graph contains a given stage (some stages are
    /// skipped when their component is null).
    bool has_stage(RenderGraphStage stage) const;

    /// Read the human-readable label for a stage (debug / profiling labels).
    static const char* stage_name(RenderGraphStage stage);

    const RenderGraphStats& get_stats() const { return stats_; }
    uint32_t get_width() const { return config_.width; }
    uint32_t get_height() const { return config_.height; }

    /// Output image view of the final composited frame. Sourced from
    /// post-processing if present, otherwise from the lighting pass.
    VkImageView get_final_output_view() const;

private:
    /// Bookkeeping for a single stage entry in the graph.
    struct StageEntry {
        RenderGraphStage stage = RenderGraphStage::StageCount;
        bool enabled = false;
        const char* debug_label = "";
    };

    void insert_barrier_between(VkCommandBuffer cmd,
                                RenderGraphStage previous,
                                RenderGraphStage next);

    void record_geometry(VkCommandBuffer cmd, const StageRecorder& recorder);
    void record_shadow(VkCommandBuffer cmd, const StageRecorder& recorder);
    void record_lighting(VkCommandBuffer cmd, const StageRecorder& recorder);
    void record_transparent(VkCommandBuffer cmd, const StageRecorder& recorder);
    void record_post_process(VkCommandBuffer cmd, const StageRecorder& recorder);

    void destroy_query_pool();

    RenderGraphConfig config_{};
    CameraData camera_{};
    std::vector<DrawItem> draw_items_{};
    std::vector<StageEntry> stages_{};
    RenderGraphStats stats_{};
    RenderGraphStage last_executed_ = RenderGraphStage::StageCount;
    bool frame_in_flight_ = false;
    bool frustum_culling_enabled_ = false;

    // GPU timestamp profiling. Two slots per stage (start, end), indexed by
    // (stage * 2) and (stage * 2 + 1). `timestamp_period_ns_` is the GPU's
    // tick-to-nanosecond conversion factor (queried from the physical
    // device). Disabled (pool == NULL_HANDLE) when the GPU/queue doesn't
    // support timestamps.
    VkQueryPool timestamp_pool_ = VK_NULL_HANDLE;
    double timestamp_period_ns_ = 0.0;
    bool timestamps_supported_ = false;
};

} // namespace gws::renderer::gpu
