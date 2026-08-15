// ====================
// texture_check — Stage 2 texture support & handling verification
// ====================
//
// Headless (no window) check of the managed texture stack against a REAL
// Vulkan device, in the style of the other tools/*_check verifiers:
//
//   1. SamplerCache      — dedup by desc, distinct descs get distinct samplers
//   2. TextureManager    — shared defaults, missing-file fallback, AssetId dedup
//   3. Mip generation    — stb source upload produces a full vkCmdBlit mip chain
//   4. Cooked `.ctex`    — BC7/BC5 blobs upload via create_compressed; a cooked
//                          sibling is PREFERRED over the source image
//   5. Hot reload        — rewriting the source file reloads IN PLACE (same
//                          object, generation bump, listener fired)
//   6. Material          — shared-default slots + rewrite_textures()
//   7. Streamable `.vt`  — mmap + zero-copy page-aligned tile access
//
// Prints "texture_check: ALL OK" and exits 0 on success; prints the first
// failing check and exits 1 otherwise.

#include "vulkan/vulkan_device.h"
#include "vulkan/vulkan_texture.h"
#include "vulkan/vulkan_texture_manager.h"
#include "vulkan/vulkan_sampler_cache.h"
#include "vulkan/vulkan_scene_material.h"
#include "vulkan/vulkan_virtual_texture.h"

#include "asset_pipeline/texture_cook.h"   // offline cook (bc7enc/rgbcx) — fine in a tool

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace gws::renderer::gpu;
namespace fs = std::filesystem;

namespace {

int g_checks = 0;

bool check(bool ok, const char* what) {
    ++g_checks;
    std::printf("  [%s] %s\n", ok ? "OK" : "FAIL", what);
    return ok;
}

#define REQUIRE(cond, what)                                     \
    do {                                                        \
        if (!check((cond), (what))) {                           \
            std::printf("texture_check: FAILED at '%s'\n", what); \
            return 1;                                           \
        }                                                       \
    } while (0)

// Solid-colour RGBA8 pixel block.
std::vector<uint8_t> solid_pixels(uint32_t w, uint32_t h,
                                  uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        px[i * 4 + 0] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = a;
    }
    return px;
}

// Minimal uncompressed 32-bit TGA writer (BGRA, bottom-up — stb handles it).
bool write_tga(const std::string& path, uint32_t w, uint32_t h,
               const std::vector<uint8_t>& rgba) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    uint8_t hdr[18] = {0};
    hdr[2]  = 2;                                   // uncompressed truecolour
    hdr[12] = static_cast<uint8_t>(w & 0xFF);  hdr[13] = static_cast<uint8_t>(w >> 8);
    hdr[14] = static_cast<uint8_t>(h & 0xFF);  hdr[15] = static_cast<uint8_t>(h >> 8);
    hdr[16] = 32;                                  // bpp
    hdr[17] = 8;                                   // 8 alpha bits
    f.write(reinterpret_cast<const char*>(hdr), sizeof hdr);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        const uint8_t bgra[4] = { rgba[i * 4 + 2], rgba[i * 4 + 1],
                                  rgba[i * 4 + 0], rgba[i * 4 + 3] };
        f.write(reinterpret_cast<const char*>(bgra), 4);
    }
    return static_cast<bool>(f);
}

bool write_blob(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== texture_check: Stage 2 texture support & handling ===\n");

    // Scratch dir next to the executable's CWD; recreated fresh each run.
    const fs::path tmp = fs::path("texture_check_tmp");
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);

    // ---- Device (headless: no surface/swapchain needed) --------------------
    VulkanDevice device;
    RenderConfig cfg{};
    cfg.window_width      = 64;
    cfg.window_height     = 64;
    cfg.enable_validation = true;
    cfg.app_name          = "texture_check";
    try {
        device.initialize(cfg);
    } catch (const std::exception& e) {
        std::printf("texture_check: VulkanDevice init failed: %s\n", e.what());
        return 1;
    }
    std::printf("  [OK] VulkanDevice up (anisotropy %s, max %.1fx)\n",
                device.anisotropy_enabled() ? "enabled" : "OFF",
                device.max_anisotropy());

    {
        // ---- 1. SamplerCache dedup ----------------------------------------
        SamplerCache samplers(&device);
        SamplerDesc a{};                       // defaults (linear/repeat/aniso 16)
        SamplerDesc b{};
        b.address_u = b.address_v = b.address_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VkSampler s1 = samplers.get(a);
        VkSampler s2 = samplers.get(a);
        VkSampler s3 = samplers.get(b);
        REQUIRE(s1 != VK_NULL_HANDLE, "sampler cache creates a sampler");
        REQUIRE(s1 == s2, "same desc -> same VkSampler (dedup)");
        REQUIRE(s3 != s1, "different desc -> different VkSampler");
        REQUIRE(samplers.size() == 2, "cache holds exactly 2 samplers");

        // ---- 2. TextureManager defaults + fallback + dedup ----------------
        TextureManager mgr(&device);
        REQUIRE(mgr.white() && mgr.normal() && mgr.black(),
                "shared default white/normal/black exist");
        REQUIRE(mgr.white()->width() == 1 && mgr.white()->height() == 1,
                "defaults are 1x1");

        auto missing = mgr.load((tmp / "does_not_exist.png").string());
        REQUIRE(missing.get() == mgr.white(),
                "missing file falls back to the shared default white");

        // ---- 3. Source load: mips + dedup ---------------------------------
        const std::string red_tga = (tmp / "red.tga").string();
        REQUIRE(write_tga(red_tga, 8, 8, solid_pixels(8, 8, 255, 0, 0)),
                "test TGA written");
        TextureImportSettings st;
        st.srgb = true;                        // colour texture
        auto t1 = mgr.load(red_tga, st);
        REQUIRE(t1 && t1.get() != mgr.white(), "TGA loads through the manager");
        REQUIRE(t1->width() == 8 && t1->height() == 8, "loaded dimensions correct");
        REQUIRE(t1->mip_levels() == 4, "8x8 source generated 4 mips (8/4/2/1)");
        auto t2 = mgr.load(red_tga, st);
        REQUIRE(t1.get() == t2.get(), "same path -> same GPU texture (AssetId dedup)");
        REQUIRE(mgr.live_texture_count() == 1, "one live managed texture");

        // ---- 4. Cooked `.ctex`: BC7 direct + sibling preference -----------
        using namespace schizo::assets;
        // BC7 32x32 cooked blob loaded directly.
        auto px32 = solid_pixels(32, 32, 40, 120, 220);
        std::vector<uint8_t> bc7 = cook_texture(px32.data(), 32, 32, TexFormat::BC7);
        const std::string bc7_path = (tmp / "direct.ctex").string();
        REQUIRE(write_blob(bc7_path, bc7), "BC7 .ctex written");
        auto tc = mgr.load_cooked(bc7_path, SamplerDesc{}, /*srgb=*/true);
        REQUIRE(tc && tc.get() != mgr.white(), "BC7 .ctex uploads (create_compressed)");
        REQUIRE(tc->width() == 32 && tc->mip_levels() == 6,
                "cooked BC7 has 32px base + 6 mips");

        // BC5 (normal-map format) round trip.
        auto npx = solid_pixels(16, 16, 128, 128, 255);
        std::vector<uint8_t> bc5 = cook_texture(npx.data(), 16, 16, TexFormat::BC5);
        const std::string bc5_path = (tmp / "normal.ctex").string();
        REQUIRE(write_blob(bc5_path, bc5), "BC5 .ctex written");
        auto tn = mgr.load_cooked(bc5_path, SamplerDesc{}, /*srgb=*/false);
        REQUIRE(tn && tn.get() != mgr.white(), "BC5 .ctex uploads");

        // Sibling preference: sib.tga is 8x8 but sib.ctex is 32x32 — loading
        // "sib.tga" must pick the cooked blob (proven by the 32px dimensions).
        const std::string sib_tga = (tmp / "sib.tga").string();
        REQUIRE(write_tga(sib_tga, 8, 8, solid_pixels(8, 8, 0, 255, 0)),
                "sibling source TGA written");
        REQUIRE(write_blob((tmp / "sib.ctex").string(), bc7), "sibling .ctex written");
        auto ts = mgr.load(sib_tga, st);
        REQUIRE(ts && ts->width() == 32,
                "cooked .ctex sibling preferred over source image");

        // ---- 5. Hot reload in place ---------------------------------------
        int reload_events = 0;
        mgr.add_reload_listener([&reload_events](uint64_t, Texture*) { ++reload_events; });

        mgr.poll_hot_reload();
        REQUIRE(reload_events == 0, "no reload when nothing changed");

        Texture* before = t1.get();
        const uint32_t gen_before = t1->generation();
        // fs::last_write_time is 1s-granular on MinGW — ensure the mtime moves.
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        REQUIRE(write_tga(red_tga, 16, 16, solid_pixels(16, 16, 0, 0, 255)),
                "source TGA rewritten (16x16 blue)");

        // Poll ACROSS the settle window rather than once.
        //
        // This assertion used to be a single poll immediately after the write,
        // which cannot pass: the shared AssetWatcher requires a change to hold
        // steady (mtime and size) across a poll before firing, so that a file
        // still being written is never read half-finished. One poll right after
        // a write can only mark it pending. The expectation dated from before
        // the watcher gained settling (WORKFLOW_PLAN 2.4, which replaced three
        // ad-hoc mtime loops — texture reload among them), and the test has been
        // failing since.
        //
        // Polling repeatedly also makes the "exactly once" half meaningful: a
        // long write must fire ONE event at the end, not one per poll while it
        // is in progress. The watcher's own poll interval is 0.25 s and its
        // settle time 0.2 s, so several polls 300 ms apart cover both.
        for (int i = 0; i < 4; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            mgr.poll_hot_reload();
        }
        REQUIRE(reload_events == 1, "reload listener fired exactly once");
        REQUIRE(t1.get() == before, "reload kept the same Texture object");
        REQUIRE(t1->generation() == gen_before + 1, "generation bumped");
        REQUIRE(t1->width() == 16 && t1->mip_levels() == 5,
                "reloaded texture has new dimensions + fresh mip chain");

        // ---- 6. Material with shared defaults + rewrite -------------------
        VkDescriptorSetLayout layout = Material::create_descriptor_set_layout(device.get_device());
        VkDescriptorPool      pool   = Material::create_descriptor_pool(device.get_device(), 4);
        MaterialUniforms mu{};
        auto mat = Material::create(&device, layout, pool, mu,
                                    t1.get(), nullptr, nullptr, nullptr, nullptr,
                                    mgr.white(), mgr.normal(), mgr.black());
        REQUIRE(mat != nullptr, "Material::create with shared defaults");
        REQUIRE(mat->bound_textures()[0] == t1.get(),
                "slot 0 bound to the supplied texture");
        REQUIRE(mat->bound_textures()[4] == mgr.black(),
                "empty emissive slot bound to the SHARED black default");
        device.wait_idle();
        mat->rewrite_textures();               // must not crash / no validation error
        check(true, "rewrite_textures() after reload");
        ++g_checks;
        mat.reset();
        vkDestroyDescriptorPool(device.get_device(), pool, nullptr);
        vkDestroyDescriptorSetLayout(device.get_device(), layout, nullptr);

        // ---- 7. Streamable `.vt` ------------------------------------------
        auto vpx = solid_pixels(64, 64, 200, 180, 40);
        std::vector<uint8_t> vt = cook_virtual_texture(vpx.data(), 64, 64, TexFormat::BC7);
        const std::string vt_path = (tmp / "stream.vt").string();
        REQUIRE(write_blob(vt_path, vt), ".vt written");
        auto stx = mgr.open_streamable(vt_path);
        REQUIRE(stx && stx->is_open(), "streamable .vt opens (mmap)");
        uint32_t tsz = 0;
        const uint8_t* tile = stx->tile(0, 0, 0, tsz);
        REQUIRE(tile != nullptr && tsz > 0, "tile(0,0,0) zero-copy access");
        REQUIRE((reinterpret_cast<uintptr_t>(tile) -
                 reinterpret_cast<uintptr_t>(stx->view().base)) % 4096 == 0,
                "tile offset is page-aligned");
        stx.reset();                           // unmap before cleanup

        device.wait_idle();
    }  // manager/samplers/textures destroyed while the device is still alive

    device.shutdown();
    fs::remove_all(tmp, ec);

    std::printf("texture_check: ALL OK (%d checks)\n", g_checks);
    return 0;
}
