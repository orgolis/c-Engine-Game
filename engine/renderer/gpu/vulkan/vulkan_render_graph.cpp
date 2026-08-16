/**
 * @file vulkan_render_graph.cpp
 * @brief Implementation of the Phase 4 Week 3 deferred render graph.
 */

#include "vulkan_render_graph.h"

#include "vulkan_device.h"
#include "vulkan_g_buffer.h"
#include "vulkan_lighting_pass.h"
#include "vulkan_post_processing.h"
#include "vulkan_shadow_map.h"
#include "vulkan_transparent_pass.h"
#include "vulkan_occlusion_culler.h"
#include "vulkan_hzb_culler.h"
#include "vulkan_scene_mesh.h"
#include "vulkan_scene_material.h"
#include "culling.h"
#include "gpu_profiler.h"  // Stage 14 N2: per-pass GPU timing sink (now compiled)

#include <spdlog/spdlog.h>

#include <array>
#include <cstdint>

namespace gws::renderer::gpu {

namespace {

constexpr uint32_t to_index(RenderGraphStage stage) {
    return static_cast<uint32_t>(stage);
}

constexpr uint32_t kStageCount = static_cast<uint32_t>(RenderGraphStage::StageCount);
constexpr uint32_t kTimestampSlotsPerStage = 2; // begin + end

/// Timestamp slots are RINGED over frames in flight.
///
/// There used to be exactly one set of slots, reset at the top of every frame.
/// With two or three frames in flight that is a race the CPU loses: it waits on
/// frame N's fence and then reads slots that frame N+1 has already reset, so the
/// queries are unavailable. Combined with VK_QUERY_RESULT_WAIT_BIT, waiting on a
/// query that will never become available returns VK_ERROR_DEVICE_LOST on
/// NVIDIA — which is what filled the log with five warnings PER FRAME and made
/// the editor look like it had hung on load. (It had not; it was drowning in its
/// own logging, and warnings are flushed to disk immediately.)
///
/// Four rings is comfortably more than any frames-in-flight this renderer uses,
/// so a frame's results survive until the CPU reads them.
constexpr uint32_t kTimestampRings   = 4;
constexpr uint32_t kSlotsPerRing     = kStageCount * kTimestampSlotsPerStage;
constexpr uint32_t kTimestampPoolSize = kSlotsPerRing * kTimestampRings;

/// First slot of `stage` within ring `ring`.
constexpr uint32_t timestamp_begin_slot(RenderGraphStage stage, uint32_t ring) {
    return ring * kSlotsPerRing + to_index(stage) * kTimestampSlotsPerStage;
}

constexpr uint32_t timestamp_end_slot(RenderGraphStage stage, uint32_t ring) {
    return timestamp_begin_slot(stage, ring) + 1;
}

} // namespace

const char* VulkanRenderGraph::stage_name(RenderGraphStage stage) {
    switch (stage) {
        case RenderGraphStage::Shadow:      return "Shadow";
        case RenderGraphStage::Geometry:    return "Geometry";
        case RenderGraphStage::Lighting:    return "Lighting";
        case RenderGraphStage::Transparent: return "Transparent";
        case RenderGraphStage::PostProcess: return "PostProcess";
        case RenderGraphStage::StageCount:  return "StageCount";
    }
    return "Unknown";
}

std::string VulkanRenderGraph::validate_config(const RenderGraphConfig& config) {
    if (config.device == nullptr) {
        return "RenderGraphConfig.device is null";
    }
    if (config.g_buffer == nullptr) {
        return "RenderGraphConfig.g_buffer is null (geometry pass requires a G-Buffer)";
    }
    if (config.lighting == nullptr) {
        return "RenderGraphConfig.lighting is null (deferred shading requires a lighting pass)";
    }
    if (config.width == 0 || config.height == 0) {
        return "RenderGraphConfig dimensions must be non-zero";
    }
    return {};
}

std::unique_ptr<VulkanRenderGraph> VulkanRenderGraph::create(const RenderGraphConfig& config) {
    const std::string error = validate_config(config);
    if (!error.empty()) {
        spdlog::error("VulkanRenderGraph::create: {}", error);
        return nullptr;
    }

    auto graph = std::make_unique<VulkanRenderGraph>();
    graph->config_ = config;

    // Probe the device's timestamp support. The Vulkan spec guarantees
    // graphics-capable queues report their timestamp_valid_bits, which must
    // be > 0 for vkCmdWriteTimestamp to be useful. The physical-device
    // limit `timestampPeriod` converts ticks to nanoseconds.
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(config.device->get_physical_device(), &props);
    graph->timestamp_period_ns_ = static_cast<double>(props.limits.timestampPeriod);

    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(config.device->get_physical_device(),
                                             &qf_count, nullptr);
    std::vector<VkQueueFamilyProperties> qfs(qf_count);
    vkGetPhysicalDeviceQueueFamilyProperties(config.device->get_physical_device(),
                                             &qf_count, qfs.data());
    const uint32_t qf = config.device->get_graphics_queue_family();
    const bool valid_bits = qf < qfs.size() && qfs[qf].timestampValidBits > 0;
    graph->timestamps_supported_ = (graph->timestamp_period_ns_ > 0.0) && valid_bits;

    if (graph->timestamps_supported_) {
        VkQueryPoolCreateInfo qp_info{};
        qp_info.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        qp_info.queryType  = VK_QUERY_TYPE_TIMESTAMP;
        qp_info.queryCount = kTimestampPoolSize;
        if (vkCreateQueryPool(config.device->get_device(), &qp_info, nullptr,
                              &graph->timestamp_pool_) != VK_SUCCESS) {
            spdlog::warn("VulkanRenderGraph: failed to create timestamp query pool, "
                         "GPU profiling disabled");
            graph->timestamps_supported_ = false;
            graph->timestamp_pool_ = VK_NULL_HANDLE;
        }
    } else {
        spdlog::info("VulkanRenderGraph: GPU timestamp queries unavailable on this "
                     "device/queue, profiling disabled");
    }

    // Stages are appended in execution order. A stage is "enabled" when the
    // corresponding component is provided.
    graph->stages_.reserve(to_index(RenderGraphStage::StageCount));

    StageEntry shadow_entry{};
    shadow_entry.stage       = RenderGraphStage::Shadow;
    shadow_entry.enabled     = (config.shadow_map != nullptr);
    shadow_entry.debug_label = "Shadow";
    graph->stages_.push_back(shadow_entry);

    StageEntry geometry_entry{};
    geometry_entry.stage       = RenderGraphStage::Geometry;
    geometry_entry.enabled     = true;
    geometry_entry.debug_label = "Geometry";
    graph->stages_.push_back(geometry_entry);

    StageEntry lighting_entry{};
    lighting_entry.stage       = RenderGraphStage::Lighting;
    lighting_entry.enabled     = true;
    lighting_entry.debug_label = "Lighting";
    graph->stages_.push_back(lighting_entry);

    StageEntry transparent_entry{};
    transparent_entry.stage       = RenderGraphStage::Transparent;
    transparent_entry.enabled     = (config.transparent != nullptr);
    transparent_entry.debug_label = "Transparent";
    graph->stages_.push_back(transparent_entry);

    StageEntry post_entry{};
    post_entry.stage       = RenderGraphStage::PostProcess;
    post_entry.enabled     = (config.post_processing != nullptr);
    post_entry.debug_label = "PostProcess";
    graph->stages_.push_back(post_entry);

    spdlog::info("VulkanRenderGraph created at {}x{} (shadow={}, transparent={}, post={})",
                 config.width, config.height,
                 shadow_entry.enabled      ? "on" : "off",
                 transparent_entry.enabled ? "on" : "off",
                 post_entry.enabled        ? "on" : "off");

    return graph;
}

bool VulkanRenderGraph::has_stage(RenderGraphStage stage) const {
    const uint32_t i = to_index(stage);
    if (i >= stages_.size()) return false;
    return stages_[i].enabled;
}

VkImageView VulkanRenderGraph::get_final_output_view() const {
    if (config_.post_processing != nullptr) {
        return config_.post_processing->get_output_image();
    }
    // Lighting pass currently exposes its output image internally; until that
    // is surfaced, callers must read from post-processing in production
    // builds. Tests that omit post-processing may simply check has_stage.
    return VK_NULL_HANDLE;
}

VulkanRenderGraph::~VulkanRenderGraph() {
    destroy_query_pool();
}

void VulkanRenderGraph::destroy_query_pool() {
    if (timestamp_pool_ != VK_NULL_HANDLE && config_.device != nullptr) {
        vkDestroyQueryPool(config_.device->get_device(), timestamp_pool_, nullptr);
    }
    timestamp_pool_ = VK_NULL_HANDLE;
}

void VulkanRenderGraph::set_camera(const CameraData& camera) {
    camera_ = camera;
    if (config_.lighting != nullptr) {
        config_.lighting->set_camera_position(camera.position);
    }
}

void VulkanRenderGraph::set_draw_items(std::vector<DrawItem> draws) {
    draw_items_ = std::move(draws);
}

void VulkanRenderGraph::begin_frame(VkCommandBuffer cmd) {
    const uint32_t prev_frame = stats_.frame_index;
    stats_ = RenderGraphStats{};
    stats_.frame_index = prev_frame + 1;
    last_executed_ = RenderGraphStage::StageCount;
    frame_in_flight_ = true;

    if (timestamps_supported_ && timestamp_pool_ != VK_NULL_HANDLE && cmd != VK_NULL_HANDLE) {
        // Reset only this frame's ring. Resetting the whole pool is what
        // destroyed the results the CPU had not read yet.
        write_ring_ = static_cast<uint32_t>(stats_.frame_index % kTimestampRings);
        vkCmdResetQueryPool(cmd, timestamp_pool_,
                            write_ring_ * kSlotsPerRing, kSlotsPerRing);
        ring_written_[write_ring_] = true;
    }
}

void VulkanRenderGraph::end_frame(VkCommandBuffer /*cmd*/) {
    frame_in_flight_ = false;
}

bool VulkanRenderGraph::resolve_timings() {
    if (!timestamps_supported_ || timestamp_pool_ == VK_NULL_HANDLE ||
        config_.device == nullptr) {
        return false;
    }
    if (stats_.frame_index == 0) {
        return false;
    }

    // Pick the NEWEST ring whose results are actually available.
    //
    // The caller has waited on a fence, but which frame that fence belongs to
    // depends on how many frames the app keeps in flight — the graph does not
    // know that number, and guessing it wrong means always reading a ring that
    // is still executing and therefore never reporting a timing at all. Probing
    // from newest backwards costs a handful of non-blocking queries and is
    // correct for any frames-in-flight count up to the ring count.
    uint32_t read_ring = static_cast<uint32_t>(stats_.frame_index % kTimestampRings);
    {
        bool found = false;
        for (uint32_t back = 0; back < kTimestampRings && !found; ++back) {
            const uint32_t ring = static_cast<uint32_t>(
                (stats_.frame_index + kTimestampRings - back) % kTimestampRings);
            // Only probe a ring that has actually been RESET and written.
            // Reading a query that was never reset is undefined behaviour, not a
            // benign VK_NOT_READY — on this driver it comes back as
            // VK_ERROR_DEVICE_LOST, which is the failure this whole change was
            // supposed to remove.
            if (!ring_written_[ring]) continue;
            std::array<uint64_t, kTimestampSlotsPerStage> probe{};
            const VkResult pr = vkGetQueryPoolResults(
                config_.device->get_device(), timestamp_pool_,
                timestamp_begin_slot(RenderGraphStage::Geometry, ring),
                kTimestampSlotsPerStage, sizeof(probe), probe.data(),
                sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
            if (pr == VK_SUCCESS) { read_ring = ring; found = true; }
        }
        if (!found) return true;   // nothing complete yet; keep last frame's numbers
    }

    // NO WAIT_BIT. Profiling must never block the CPU on the GPU: if a result
    // is not ready this frame, the honest answer is "no number this frame", not
    // a stall. WAIT_BIT here also turned an unavailable query into
    // VK_ERROR_DEVICE_LOST rather than a benign VK_NOT_READY.
    //
    // A stage that did not run leaves its pair unwritten, which reads back as
    // VK_NOT_READY — expected, and silent.
    auto compute_us = [&](RenderGraphStage stage, double previous) -> double {
        std::array<uint64_t, kTimestampSlotsPerStage> pair{};
        const VkResult vr = vkGetQueryPoolResults(
            config_.device->get_device(),
            timestamp_pool_,
            timestamp_begin_slot(stage, read_ring),
            kTimestampSlotsPerStage,
            sizeof(pair),
            pair.data(),
            sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT);
        if (vr == VK_NOT_READY) return previous;   // normal; keep the last good value
        if (vr != VK_SUCCESS) {
            // Log ONCE. A per-frame warning at over a hundred frames a second
            // is not a diagnostic, it is a denial of service against the log —
            // warnings are flushed to disk on write, so this alone made the
            // editor appear frozen on load.
            static bool reported = false;
            if (!reported) {
                reported = true;
                spdlog::warn("VulkanRenderGraph::resolve_timings: vkGetQueryPoolResults "
                             "returned {} for stage {} — GPU stage timings are "
                             "unavailable this session (reported once)",
                             static_cast<int>(vr), stage_name(stage));
            }
            return previous;
        }
        if (pair[1] <= pair[0]) return previous;
        const double ns = static_cast<double>(pair[1] - pair[0]) * timestamp_period_ns_;
        return ns / 1000.0; // ns → µs
    };

    // Carry the previous value forward when a stage has no fresh result, so the
    // overlay shows the last real measurement instead of flickering to zero.
    stats_.shadow_us      = compute_us(RenderGraphStage::Shadow,      last_shadow_us_);
    stats_.geometry_us    = compute_us(RenderGraphStage::Geometry,    last_geometry_us_);
    stats_.lighting_us    = compute_us(RenderGraphStage::Lighting,    last_lighting_us_);
    stats_.transparent_us = compute_us(RenderGraphStage::Transparent, last_transparent_us_);
    stats_.post_process_us = compute_us(RenderGraphStage::PostProcess, last_post_process_us_);

    last_shadow_us_       = stats_.shadow_us;
    last_geometry_us_     = stats_.geometry_us;
    last_lighting_us_     = stats_.lighting_us;
    last_transparent_us_  = stats_.transparent_us;
    last_post_process_us_ = stats_.post_process_us;
    return true;
}

void VulkanRenderGraph::update_gpu_profiler() {
    // Feed this frame's resolved per-stage GPU timings (set by resolve_timings,
    // in µs) into the GPUProfiler the Stage 14 overlay reads (N2). Only stages
    // that actually ran are submitted; submit_frame() zeroes the rest.
    std::vector<std::pair<std::string, float>> passes;
    const auto add = [&](RenderGraphStage stage, double us) {
        if (us > 0.0)
            passes.emplace_back(stage_name(stage), static_cast<float>(us / 1000.0));  // µs → ms
    };
    add(RenderGraphStage::Shadow,      stats_.shadow_us);
    add(RenderGraphStage::Geometry,    stats_.geometry_us);
    add(RenderGraphStage::Lighting,    stats_.lighting_us);
    add(RenderGraphStage::Transparent, stats_.transparent_us);
    add(RenderGraphStage::PostProcess, stats_.post_process_us);
    engine::vulkan::GPUProfiler::instance().submit_frame(passes);
}



void VulkanRenderGraph::execute_stage(VkCommandBuffer cmd,
                                      RenderGraphStage stage,
                                      const StageRecorder& recorder) {
    if (!frame_in_flight_) {
        spdlog::warn("VulkanRenderGraph::execute_stage called outside of begin/end_frame");
    }
    if (!has_stage(stage)) {
        // Stage was skipped at build time (e.g. shadows disabled).
        return;
    }

    if (last_executed_ != RenderGraphStage::StageCount) {
        insert_barrier_between(cmd, last_executed_, stage);
    }

    const bool emit_ts = timestamps_supported_ && timestamp_pool_ != VK_NULL_HANDLE &&
                         cmd != VK_NULL_HANDLE;
    if (emit_ts) {
        // TOP_OF_PIPE for the begin marker means "the moment the GPU
        // pulls this command off the queue", which is the right reference
        // point for the start of the stage.
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            timestamp_pool_, timestamp_begin_slot(stage, write_ring_));
    }

    switch (stage) {
        case RenderGraphStage::Shadow:
            record_shadow(cmd, recorder);
            ++stats_.shadow_passes_run;
            break;
        case RenderGraphStage::Geometry:
            record_geometry(cmd, recorder);
            ++stats_.geometry_passes_run;
            break;
        case RenderGraphStage::Lighting:
            record_lighting(cmd, recorder);
            ++stats_.lighting_passes_run;
            break;
        case RenderGraphStage::Transparent:
            record_transparent(cmd, recorder);
            ++stats_.transparent_passes_run;
            break;
        case RenderGraphStage::PostProcess:
            record_post_process(cmd, recorder);
            ++stats_.post_process_passes_run;
            break;
        case RenderGraphStage::StageCount:
            break;
    }

    if (emit_ts) {
        // BOTTOM_OF_PIPE for the end marker waits until all preceding
        // commands in the stage have finished executing on the GPU.
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            timestamp_pool_, timestamp_end_slot(stage, write_ring_));
    }

    last_executed_ = stage;
}

void VulkanRenderGraph::insert_barrier_between(VkCommandBuffer cmd,
                                               RenderGraphStage previous,
                                               RenderGraphStage next) {
    // The G-Buffer and shadow attachments transition from
    // COLOR/DEPTH_ATTACHMENT_OPTIMAL to SHADER_READ_ONLY_OPTIMAL between the
    // producing pass and the consuming pass. The individual passes already
    // emit their own image-memory barriers in their begin/end methods, so
    // here we only need to insert a coarse execution barrier to enforce
    // ordering.
    if (cmd == VK_NULL_HANDLE) {
        return; // Tests sometimes pass a null command buffer.
    }

    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                     VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    if (previous == RenderGraphStage::Lighting && next == RenderGraphStage::PostProcess) {
        src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (previous == RenderGraphStage::Lighting && next == RenderGraphStage::Transparent) {
        // Lighting wrote HDR colour; transparent pass reads it as a colour
        // attachment (LOAD_OP_LOAD) and samples G-Buffer depth as a depth
        // attachment.
        src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else if (previous == RenderGraphStage::Transparent && next == RenderGraphStage::PostProcess) {
        // Transparent pass wrote HDR colour; post-processing samples it.
        src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0,
                         0, nullptr,
                         0, nullptr,
                         0, nullptr);
}

void VulkanRenderGraph::record_shadow(VkCommandBuffer cmd, const StageRecorder& recorder) {
    if (config_.shadow_map == nullptr) return;
    config_.shadow_map->begin_directional_pass(cmd, 0);
    if (recorder) {
        recorder(cmd);
    } else if (!draw_items_.empty()) {
        // Default: rasterise every draw item with the depth-only pipeline.
        // Shadow caster matrices come from the camera for now (single
        // directional cascade); a future cascade-matrix setter will replace
        // this with per-cascade orthographic projections.
        std::vector<DrawItem> items_to_draw = draw_items_;
        if (frustum_culling_enabled_ && has_shadow_cull_view_proj_) {
            // Cull casters against the LIGHT'S frustum, never the camera's:
            // an object outside the camera view whose shadow falls INTO the
            // view must still render into the shadow map. When no light
            // matrix was provided this frame, skip culling entirely
            // (conservative — drawing an extra caster is cheap, a missing
            // shadow is a visible bug).
            Frustum light_frustum = Frustum::from_matrix(shadow_cull_view_proj_);
            // Shadow stage doesn't update the per-frame frustum stats —
            // those track the geometry-stage view, which is the canonical
            // "what the camera sees" measurement. Pass nullptr to skip.
            cull_draw_items_frustum(items_to_draw, light_frustum);
        }
        config_.shadow_map->draw_items(cmd, camera_.view, camera_.proj,
                                       camera_.position,
                                       items_to_draw.data(), items_to_draw.size(),
                                       &stats_.shadow_draw_calls,
                                       &stats_.shadow_triangles);
    }
    config_.shadow_map->end_pass(cmd);
}

void VulkanRenderGraph::record_geometry(VkCommandBuffer cmd, const StageRecorder& recorder) {
    if (config_.g_buffer == nullptr) return;

    // Reset and arm the occlusion culler BEFORE begin_geometry_pass so the
    // vkCmdResetQueryPool happens outside the active render pass (Vulkan
    // forbids vkCmdResetQueryPool inside a render pass).
    if (config_.occlusion_culler != nullptr && !draw_items_.empty() && !recorder) {
        config_.occlusion_culler->begin_frame(
            cmd, static_cast<uint32_t>(draw_items_.size()));
    }

    config_.g_buffer->begin_geometry_pass(cmd);
    if (recorder) {
        recorder(cmd);
    } else if (!draw_items_.empty()) {
        // Default: textured-pipeline iteration over the caller's draw list.
        std::vector<DrawItem> items_to_draw = draw_items_;
        Frustum frustum = Frustum::from_matrix(camera_.proj * camera_.view);
        if (frustum_culling_enabled_) {
            CullingStats cull{};
            cull_draw_items_frustum(items_to_draw, frustum, &cull);
            stats_.frustum_input_items   = cull.input_items;
            stats_.frustum_visible_items = cull.output_items;
            stats_.frustum_culled_items  = cull.culled_frustum;
        } else {
            // Culling disabled: report the input list as fully visible so
            // downstream UI can still display sensible numbers.
            stats_.frustum_input_items   = static_cast<uint32_t>(items_to_draw.size());
            stats_.frustum_visible_items = static_cast<uint32_t>(items_to_draw.size());
            stats_.frustum_culled_items  = 0;
        }
        // Pass the same frustum into draw_items so it can do finer-grained
        // per-meshlet culling on top of the entity-level cull above.
        // (Verified not to be the cause of the city-OBJ rendering bugs we
        // chased earlier — that was back-face culling, since reverted.)
        // HZB is deliberately NOT forwarded here: was_visible() is indexed by
        // position in the ORIGINAL draw list the caller tested, but
        // items_to_draw was just frustum-culled — indices shift and the
        // G-Buffer would read the WRONG draw's visibility (the "random
        // objects disappear" bug). HZB filtering now happens in the caller
        // (main.cpp) on the un-culled list where indices line up; the culler
        // stays in config_ only for build_and_readback below.
        config_.g_buffer->draw_items(cmd, camera_.view, camera_.proj,
                                     camera_.position,
                                     items_to_draw.data(), items_to_draw.size(),
                                     &stats_.geometry_draw_calls,
                                     &stats_.geometry_triangles,
                                     config_.occlusion_culler,
                                     /*hzb_culler=*/nullptr,
                                     &frustum);
    } else {
        // Smoke-test fallback: paint the G-Buffer with the built-in demo
        // triangle so downstream stages have real data.
        config_.g_buffer->draw_demo_triangle(cmd, camera_.view, camera_.proj);
    }
    config_.g_buffer->end_geometry_pass(cmd);

    // Build the HZB from this frame's depth so next frame's visibility test
    // has fresh data. Done immediately after the geometry pass while the
    // depth attachment is still in DEPTH_STENCIL_READ_ONLY_OPTIMAL.
    if (config_.hzb_culler != nullptr) {
        config_.hzb_culler->build_and_readback(cmd);
    }
}

void VulkanRenderGraph::resolve_occlusion_queries() {
    if (config_.occlusion_culler != nullptr) {
        config_.occlusion_culler->resolve_results();
    }
}

void VulkanRenderGraph::record_lighting(VkCommandBuffer cmd, const StageRecorder& recorder) {
    if (config_.lighting == nullptr) return;
    config_.lighting->begin_pass(cmd, config_.width, config_.height);
    if (recorder) {
        recorder(cmd);
    } else {
        config_.lighting->render(cmd);
    }
    config_.lighting->end_pass(cmd);
}

void VulkanRenderGraph::record_transparent(VkCommandBuffer cmd, const StageRecorder& recorder) {
    if (config_.transparent == nullptr) return;
    // Filter the per-frame draw list for the items that belong here. For now
    // the editor passes only the opaque list into the graph, so this default
    // recorder has nothing to draw — Blend-mode entities are intentionally
    // invisible until the transparent forward pipeline is implemented.
    if (recorder) {
        recorder(cmd);
    } else {
        config_.transparent->execute(cmd, draw_items_, camera_);
    }
}

void VulkanRenderGraph::record_post_process(VkCommandBuffer cmd, const StageRecorder& recorder) {
    if (config_.post_processing == nullptr) return;
    if (recorder) {
        recorder(cmd);
    } else {
        config_.post_processing->render(cmd);
    }
}

void VulkanRenderGraph::resize(uint32_t width, uint32_t height) {
    if (width == config_.width && height == config_.height) {
        return;
    }
    config_.width = width;
    config_.height = height;
    if (config_.g_buffer != nullptr) {
        config_.g_buffer->resize(config_.device, width, height);
    }
    if (config_.lighting != nullptr) {
        config_.lighting->resize(width, height);
    }
    if (config_.post_processing != nullptr) {
        config_.post_processing->resize(width, height);
    }
}

} // namespace gws::renderer::gpu
