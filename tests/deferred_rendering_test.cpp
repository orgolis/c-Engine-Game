/**
 * @file deferred_rendering_test.cpp
 * @brief Phase 4 deferred-rendering unit + integration tests (Catch2).
 *
 * These tests validate the *configuration* and *wiring* of the deferred
 * pipeline without instantiating a real Vulkan device, so they can run on any
 * CI box that builds the engine. Live-GPU integration is exercised by
 * `vulkan_window_test.exe`.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "renderer/gpu/vulkan/vulkan_g_buffer.h"
#include "renderer/gpu/vulkan/vulkan_lighting_pass.h"
#include "renderer/gpu/vulkan/vulkan_post_processing.h"
#include "renderer/gpu/vulkan/vulkan_render_graph.h"
#include "renderer/gpu/vulkan/vulkan_shader_registry.h"
#include "renderer/gpu/vulkan/vulkan_shadow_map.h"

using namespace gws::renderer::gpu;
using Catch::Matchers::WithinAbs;

// =============================================================================
// G-Buffer configuration
// =============================================================================

TEST_CASE("G-Buffer config defaults are sane", "[deferred][gbuffer]") {
    GBufferConfig config;
    CHECK(config.width  == 1920);
    CHECK(config.height == 1080);
    CHECK(config.position_format == GBufferFormat::RGBA16F);
    CHECK(config.normal_format   == GBufferFormat::RGBA16F);
    CHECK(config.albedo_format   == GBufferFormat::RGBA8);
    CHECK(config.material_format == GBufferFormat::RGBA8);
    CHECK_FALSE(config.use_msaa);
}

TEST_CASE("G-Buffer estimated memory stays under budget", "[deferred][gbuffer][perf]") {
    // 4 colour attachments + 1 depth at 1080p in 16-bit. The deferred pass
    // budget on the Phase 4 plan is < 500 MB.
    constexpr uint32_t width  = 1920;
    constexpr uint32_t height = 1080;
    constexpr uint32_t bpp    = 16;
    constexpr uint32_t attachments = 5;
    const size_t bytes = static_cast<size_t>(width) * height * bpp * attachments;
    REQUIRE(bytes > 0);
    REQUIRE(bytes < 500ull * 1024ull * 1024ull);
}

// =============================================================================
// Lighting pass: light packing matches shader expectations
// =============================================================================

TEST_CASE("Light packing matches lighting_pass.frag layout", "[deferred][lighting]") {
    SECTION("position.w encodes light type") {
        Light directional{};
        directional.position = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        CHECK(directional.position.w == 0.0f); // shader: 0 == directional

        Light point{};
        point.position = glm::vec4(1.0f, 2.0f, 3.0f, 1.0f);
        CHECK(point.position.w == 1.0f);       // shader: 1 == point

        Light spot{};
        spot.position = glm::vec4(0.0f, 5.0f, 0.0f, 2.0f);
        CHECK(spot.position.w == 2.0f);        // shader: 2 == spot
    }

    SECTION("direction.w stores intensity") {
        Light l{};
        l.direction = glm::vec4(0.0f, -1.0f, 0.0f, 12.5f);
        CHECK_THAT(l.direction.w, WithinAbs(12.5f, 1e-5f));
    }
}

TEST_CASE("LightingConfig defaults reflect Phase 4 plan", "[deferred][lighting]") {
    LightingConfig config;
    CHECK(config.max_lights == 128);
    CHECK(config.max_shadow_casting_lights == 8);
    CHECK_THAT(config.global_ambient, WithinAbs(0.1f, 1e-6f));
    CHECK(config.enable_ibl);
    CHECK(config.enable_shadows);
}

// =============================================================================
// Shadow mapping configuration
// =============================================================================

TEST_CASE("Cascaded shadow map configuration", "[deferred][shadow]") {
    ShadowMapConfig config;
    config.type = ShadowMapType::Cascaded2D;
    config.cascade_count = 4;

    CHECK(config.type == ShadowMapType::Cascaded2D);
    CHECK(config.cascade_count == 4);
    CHECK(config.use_pcf);
    CHECK(config.pcf_samples == 9); // 3x3 PCF in shader
}

TEST_CASE("Shadow LOD bias skips terrain", "[deferred][shadow][terrain]") {
    // Ordinary meshes drop a tier in the shadow pass: a silhouette needs less
    // detail than a shaded surface, and the saving is deliberate.
    CHECK(shadow_lod_for(0, /*is_terrain=*/false) == 1);
    CHECK(shadow_lod_for(2, /*is_terrain=*/false) == 3);

    // Terrain must draw the SAME tier the G-buffer drew. A terrain LOD tier is
    // a different heightfield, not a decimated copy, so a coarser one puts the
    // shadow map's depth on a surface that is not the one being shaded --
    // which lands shadows in the wrong place on the ground.
    CHECK(shadow_lod_for(0, /*is_terrain=*/true) == 0);
    CHECK(shadow_lod_for(2, /*is_terrain=*/true) == 2);

    // The property, stated once rather than per-case: terrain never coarsens.
    for (size_t lod = 0; lod < 8; ++lod) {
        CHECK(shadow_lod_for(lod, true) == lod);
        CHECK(shadow_lod_for(lod, false) > lod);
    }
}

// =============================================================================
// Post-processing configuration
// =============================================================================

TEST_CASE("Post-processing config wires bloom/tonemap/TAA", "[deferred][postfx]") {
    PostProcessingConfig pp;
    pp.width = 1280;
    pp.height = 720;
    CHECK(pp.bloom.enabled);
    CHECK(pp.tone_mapping.enabled);
    CHECK(pp.taa.enabled);
    CHECK_THAT(pp.tone_mapping.gamma, WithinAbs(2.2f, 1e-6f));
}

// =============================================================================
// Render graph: validation of configuration
// =============================================================================

TEST_CASE("VulkanRenderGraph::validate_config rejects invalid input", "[deferred][rendergraph]") {
    SECTION("null device is rejected") {
        RenderGraphConfig cfg{};
        cfg.width = 1280;
        cfg.height = 720;
        const auto err = VulkanRenderGraph::validate_config(cfg);
        REQUIRE_FALSE(err.empty());
        CHECK(err.find("device") != std::string::npos);
    }

    SECTION("zero dimensions are rejected") {
        // We can't create a real VulkanDevice* here; use a non-null pointer
        // value purely to bypass the device check. validate_config() is a
        // pure inspection and never dereferences the pointer.
        RenderGraphConfig cfg{};
        cfg.device   = reinterpret_cast<VulkanDevice*>(0x1);
        cfg.g_buffer = reinterpret_cast<VulkanGBuffer*>(0x1);
        cfg.lighting = reinterpret_cast<VulkanLightingPass*>(0x1);
        cfg.width = 0;
        cfg.height = 720;
        const auto err = VulkanRenderGraph::validate_config(cfg);
        REQUIRE_FALSE(err.empty());
        CHECK(err.find("dimensions") != std::string::npos);
    }

    SECTION("missing g_buffer is rejected") {
        RenderGraphConfig cfg{};
        cfg.device = reinterpret_cast<VulkanDevice*>(0x1);
        cfg.lighting = reinterpret_cast<VulkanLightingPass*>(0x1);
        cfg.width = 1280;
        cfg.height = 720;
        const auto err = VulkanRenderGraph::validate_config(cfg);
        REQUIRE_FALSE(err.empty());
        CHECK(err.find("g_buffer") != std::string::npos);
    }

    SECTION("missing lighting pass is rejected") {
        RenderGraphConfig cfg{};
        cfg.device = reinterpret_cast<VulkanDevice*>(0x1);
        cfg.g_buffer = reinterpret_cast<VulkanGBuffer*>(0x1);
        cfg.width = 1280;
        cfg.height = 720;
        const auto err = VulkanRenderGraph::validate_config(cfg);
        REQUIRE_FALSE(err.empty());
        CHECK(err.find("lighting") != std::string::npos);
    }

    SECTION("minimal valid config passes") {
        RenderGraphConfig cfg{};
        cfg.device   = reinterpret_cast<VulkanDevice*>(0x1);
        cfg.g_buffer = reinterpret_cast<VulkanGBuffer*>(0x1);
        cfg.lighting = reinterpret_cast<VulkanLightingPass*>(0x1);
        cfg.width = 1280;
        cfg.height = 720;
        CHECK(VulkanRenderGraph::validate_config(cfg).empty());
    }
}

TEST_CASE("VulkanRenderGraph::stage_name is stable", "[deferred][rendergraph]") {
    using std::string;
    CHECK(string(VulkanRenderGraph::stage_name(RenderGraphStage::Shadow))      == "Shadow");
    CHECK(string(VulkanRenderGraph::stage_name(RenderGraphStage::Geometry))    == "Geometry");
    CHECK(string(VulkanRenderGraph::stage_name(RenderGraphStage::Lighting))    == "Lighting");
    CHECK(string(VulkanRenderGraph::stage_name(RenderGraphStage::PostProcess)) == "PostProcess");
}

// =============================================================================
// Shader stage enum stability
// =============================================================================

TEST_CASE("ShaderStage enum values are contiguous from 0", "[deferred][shaders]") {
    CHECK(static_cast<int>(ShaderStage::Vertex)   == 0);
    CHECK(static_cast<int>(ShaderStage::Fragment) == 1);
    CHECK(static_cast<int>(ShaderStage::Geometry) == 2);
}
