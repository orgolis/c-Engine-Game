/**
 * @file vulkan_texture_manager.h
 * @brief Runtime texture asset manager (Master Plan Stage 2 — texture handling).
 *
 * The "handling" layer above `Texture`:
 *   - Deduplicates by content-addressed AssetId (asset_id_from_path): repeated
 *     loads of the same path share ONE GPU texture (ref-counted via shared_ptr;
 *     the cache holds a weak_ptr so unused textures free themselves).
 *   - Owns engine-wide shared default textures (white / flat-normal / black),
 *     so materials stop minting their own 1x1 fallbacks.
 *   - Consumes cooked `.ctex` (BC-compressed + mipped) with no CPU decode when
 *     present; otherwise decodes the source image via stb and generates mips.
 *   - Owns the shared SamplerCache (wrap/filter/anisotropy dedup).
 *   - Hot reload: watches source-image mtimes and reloads changed textures in
 *     place, notifying listeners so they can re-write descriptor sets.
 */

#pragma once

#include "vulkan_sampler_cache.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>

#include "assets/asset_watcher.h"
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace gws::renderer::gpu {

class VulkanDevice;
class Texture;
class StreamableTexture;

/// Per-texture import policy. `sampler` drives the shared VkSampler; `srgb`
/// selects the color space (albedo/emissive true, normal/MR/AO false).
struct TextureImportSettings {
    bool        srgb          = false;
    bool        gen_mips      = true;
    bool        prefer_cooked = true;  // use a `<stem>.ctex` sibling if present
    SamplerDesc sampler{};
};

class TextureManager {
public:
    /// Called after a texture is hot-reloaded in place. Args: its AssetId and
    /// the (same-identity) Texture whose view()/generation() changed.
    using ReloadListener = std::function<void(uint64_t, Texture*)>;

    explicit TextureManager(VulkanDevice* device);
    ~TextureManager();

    TextureManager(const TextureManager&)            = delete;
    TextureManager& operator=(const TextureManager&) = delete;

    // Engine-wide shared defaults (created once; live with the manager).
    const Texture* white()  const { return default_white_.get(); }
    const Texture* normal() const { return default_normal_.get(); }
    const Texture* black()  const { return default_black_.get(); }

    /// Get-or-load a texture by source path (dedup by AssetId). Prefers a cooked
    /// `<stem>.ctex` sibling when `settings.prefer_cooked`. Returns the shared
    /// default white on any failure (never null), so callers can bind it safely.
    std::shared_ptr<Texture> load(const std::string& path,
                                  const TextureImportSettings& settings = {});

    /// Load a cooked `.ctex` blob directly (already BC-compressed + mipped),
    /// borrowing a cache sampler. `srgb` selects the BC VkFormat's colour-space
    /// decode variant. Returns default white on failure.
    std::shared_ptr<Texture> load_cooked(const std::string& ctex_path,
                                         const SamplerDesc& sampler,
                                         bool srgb = false);

    /// Directory holding cooked output. When set, `load("<dir>/foo.png")` first
    /// looks for `<cooked_root>/foo.ctex` (the assetcook output) before decoding
    /// the source. Empty (default) = only check a `<stem>.ctex` source sibling.
    void set_cooked_root(std::string dir) { cooked_root_ = std::move(dir); }

    /// Open a cooked tiled virtual texture (`.vt`) for streaming: memory-mapped,
    /// zero-copy per-tile access. The GPU-residency system (page table, feedback,
    /// atlas, async upload) is Stage 8 — this is the on-ramp. Returns nullptr on
    /// failure. Defined in vulkan_virtual_texture.cpp to keep <windows.h> out of
    /// the manager's own translation unit.
    std::shared_ptr<StreamableTexture> open_streamable(const std::string& vt_path);

    /// Shared sampler for `desc` (deduplicated).
    VkSampler     sampler(const SamplerDesc& desc) { return sampler_cache_.get(desc); }
    SamplerCache& sampler_cache()                  { return sampler_cache_; }

    /// Poll watched source files; reload changed ones in place and notify
    /// listeners. Safe to call every frame — the shared AssetWatcher throttles
    /// its own filesystem scans, and debounces so a texture still being written
    /// by an external tool is not decoded half-finished.
    void poll_hot_reload();
    void add_reload_listener(ReloadListener cb) { listeners_.push_back(std::move(cb)); }

    // Diagnostics / tests.
    size_t live_texture_count() const;
    size_t sampler_count()      const { return sampler_cache_.size(); }

private:
    struct Entry {
        std::weak_ptr<Texture>          tex;
        std::string                     path;            // source (or .ctex) path
        TextureImportSettings           settings;
        bool                            hot_reloadable = false;  // source-image path
        uint64_t                        watch_token = 0;         // AssetWatcher
    };

    // Decode a source image via stb and upload (mips + shared sampler).
    std::shared_ptr<Texture> load_source_(const std::string& path,
                                          const TextureImportSettings& settings,
                                          std::filesystem::file_time_type& out_mtime);

    VulkanDevice*                       device_ = nullptr;
    SamplerCache                        sampler_cache_;
    std::shared_ptr<Texture>            default_white_;
    std::shared_ptr<Texture>            default_normal_;
    std::shared_ptr<Texture>            default_black_;
    std::unordered_map<uint64_t, Entry> cache_;    // AssetId -> entry
    // One shared watcher rather than a private mtime loop: settling behaviour,
    // poll throttling and the "file appeared later" case then match every other
    // hot-reloadable asset type instead of being reimplemented here.
    gws::assets::AssetWatcher           watcher_;
    std::vector<uint64_t>               dirty_;   // asset ids the watcher flagged
    std::vector<ReloadListener>         listeners_;
    std::string                         cooked_root_;
    mutable std::mutex                  mutex_;
};

} // namespace gws::renderer::gpu
