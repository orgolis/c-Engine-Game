/**
 * @file vulkan_texture_manager.cpp
 * @brief TextureManager implementation: AssetId dedup, shared defaults, cooked
 *        `.ctex` consumption, stb fallback, and in-place hot reload.
 */

#include "vulkan_texture_manager.h"
#include "vulkan_texture.h"
#include "vulkan_device.h"

// Asset pipeline (header-only, runtime-safe): content-addressed ids + the
// cooked-texture format/reader. The renderer target adds
// engine/core/asset_pipeline to its private include dirs for these.
#include "asset_id.h"
#include "texture_format.h"

#include <stb_image.h>   // declarations only; impl lives in vulkan_texture.cpp
#include <spdlog/spdlog.h>

#include <fstream>

namespace gws::renderer::gpu {

namespace fs = std::filesystem;

namespace {

// UTF-8 → std::filesystem::path (narrow-string fs::path goes through the ANSI
// codepage on Windows and fails for non-ASCII filenames).
fs::path u8p(const std::string& s) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

// Project-relative, separator-normalized path → stable AssetId.
uint64_t id_for(const std::string& path) {
    const std::string norm = fs::path(path).lexically_normal().generic_string();
    return schizo::assets::asset_id_from_path(norm);
}

// The cooked sibling for a source image: `<dir>/<stem>.ctex`.
std::string cooked_sibling(const std::string& path) {
    fs::path p(path);
    p.replace_extension(".ctex");
    return p.generic_string();
}

// Resolve a path independent of the launch working directory: scene/material
// data stores repo-root-relative paths, but the editor is sometimes launched
// from build-editor/bin. Try as given, then walk up a few levels.
std::string resolve_disk_path(const std::string& path) {
    std::error_code ec;
    if (path.empty() || fs::exists(u8p(path), ec)) return path;
    if (u8p(path).is_absolute()) return path;
    std::string prefix;
    for (int up = 1; up <= 4; ++up) {
        prefix += "../";
        const std::string candidate = prefix + path;
        if (fs::exists(u8p(candidate), ec)) return candidate;
    }
    return path;
}

// Cooked BC TexFormat → Vulkan format (sRGB selects the color-space variant;
// BC5 is always linear as it stores two-channel data, i.e. normals).
VkFormat bc_vkformat(schizo::assets::TexFormat f, bool srgb) {
    using TF = schizo::assets::TexFormat;
    switch (f) {
        case TF::BC1: return srgb ? VK_FORMAT_BC1_RGBA_SRGB_BLOCK : VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case TF::BC3: return srgb ? VK_FORMAT_BC3_SRGB_BLOCK      : VK_FORMAT_BC3_UNORM_BLOCK;
        case TF::BC5: return VK_FORMAT_BC5_UNORM_BLOCK;
        case TF::BC7: return srgb ? VK_FORMAT_BC7_SRGB_BLOCK      : VK_FORMAT_BC7_UNORM_BLOCK;
    }
    return VK_FORMAT_BC7_UNORM_BLOCK;
}

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(u8p(path), std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize n = f.tellg();
    if (n <= 0) return false;
    f.seekg(0);
    out.resize(static_cast<size_t>(n));
    return static_cast<bool>(f.read(reinterpret_cast<char*>(out.data()), n));
}

} // namespace

TextureManager::TextureManager(VulkanDevice* device)
    : device_(device), sampler_cache_(device) {
    default_white_  = Texture::create_default_white(device);
    default_normal_ = Texture::create_default_normal(device);
    default_black_  = Texture::create_default_black(device);
    if (!default_white_ || !default_normal_ || !default_black_) {
        spdlog::error("TextureManager: failed to create default textures");
    }
}

TextureManager::~TextureManager() = default;

std::shared_ptr<Texture> TextureManager::load_source_(const std::string& path,
                                                      const TextureImportSettings& settings,
                                                      fs::file_time_type& out_mtime) {
    const VkSampler s = sampler_cache_.get(settings.sampler);
    std::shared_ptr<Texture> tex =
        Texture::create_from_file(device_, path, settings.srgb, settings.gen_mips, s);
    if (tex) {
        std::error_code ec;
        out_mtime = fs::last_write_time(u8p(path), ec);
    }
    return tex;
}

std::shared_ptr<Texture> TextureManager::load_cooked(const std::string& ctex_path,
                                                     const SamplerDesc& sampler,
                                                     bool srgb) {
    std::vector<uint8_t> bytes;
    if (!read_file(ctex_path, bytes)) {
        spdlog::warn("TextureManager: cannot read cooked texture '{}'", ctex_path);
        return default_white_;
    }
    schizo::assets::CookedTextureView view;
    if (!view.open(bytes.data(), bytes.size())) {
        spdlog::warn("TextureManager: '{}' is not a valid cooked texture", ctex_path);
        return default_white_;
    }
    const auto  fmt      = static_cast<schizo::assets::TexFormat>(view.header->format);
    // BC blocks are stored raw; `srgb` picks the VkFormat decode variant so the
    // GPU applies sRGB->linear for colour textures (albedo/emissive). The caller
    // knows the slot's colour space (glTF loader classifies per usage).
    const VkFormat vkfmt = bc_vkformat(fmt, srgb);
    const VkSampler s    = sampler_cache_.get(sampler);
    const uint8_t* blocks     = view.blocks;
    const size_t   block_size = bytes.size() - sizeof(schizo::assets::CookedTextureHeader);

    std::shared_ptr<Texture> tex = Texture::create_compressed(
        device_, vkfmt, view.header->width, view.header->height,
        view.header->mip_count, blocks, block_size, s);
    if (!tex) {
        spdlog::warn("TextureManager: GPU rejected cooked '{}', using default", ctex_path);
        return default_white_;
    }
    return tex;
}

std::shared_ptr<Texture> TextureManager::load(const std::string& path,
                                              const TextureImportSettings& settings) {
    const uint64_t id = id_for(path);

    std::lock_guard<std::mutex> lk(mutex_);
    if (auto it = cache_.find(id); it != cache_.end()) {
        if (auto sp = it->second.tex.lock()) return sp;   // cache hit, still alive
    }

    // CWD-independent + UTF-8-safe resolution of the on-disk location. The
    // cache stays keyed on the caller's original path.
    const std::string disk = resolve_disk_path(path);

    Entry entry;
    entry.path     = disk;
    entry.settings = settings;

    std::shared_ptr<Texture> tex;

    // Resolve a cooked BC blob (no CPU decode): first `<cooked_root>/<stem>.ctex`
    // (assetcook output), then a `<stem>.ctex` sibling next to the source.
    std::string ctex;
    if (settings.prefer_cooked) {
        std::error_code ec;
        if (!cooked_root_.empty()) {
            const std::string c =
                (fs::path(cooked_root_) / u8p(disk).stem()).generic_string() + ".ctex";
            if (fs::exists(u8p(c), ec)) ctex = c;
        }
        if (ctex.empty()) {
            const std::string c = cooked_sibling(disk);
            if (fs::exists(u8p(c), ec)) ctex = c;
        }
    }

    std::error_code exist_ec;
    if (!ctex.empty()) {
        tex = load_cooked(ctex, settings.sampler, settings.srgb);
        entry.path           = ctex;
        entry.hot_reloadable = false;   // BC blob: can't reload_from_pixels
        std::error_code ec;
        entry.mtime = fs::last_write_time(u8p(ctex), ec);
    } else if (fs::exists(u8p(disk), exist_ec)) {
        tex = load_source_(disk, settings, entry.mtime);
        entry.hot_reloadable = static_cast<bool>(tex);
    } else {
        spdlog::warn("TextureManager: texture not found '{}', using default white", path);
        return default_white_;
    }

    if (!tex) return default_white_;

    entry.tex = tex;
    cache_[id] = std::move(entry);
    return tex;
}

void TextureManager::poll_hot_reload() {
    std::vector<std::pair<uint64_t, Texture*>> reloaded;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        for (auto& [id, e] : cache_) {
            if (!e.hot_reloadable) continue;
            auto sp = e.tex.lock();
            if (!sp) continue;

            std::error_code ec;
            const auto now = fs::last_write_time(u8p(e.path), ec);
            if (ec || now == e.mtime) continue;
            e.mtime = now;

            // Decode from bytes (UTF-8-safe; stbi_load's narrow fopen fails
            // for non-ASCII filenames on Windows).
            std::vector<uint8_t> bytes;
            if (!read_file(e.path, bytes)) {
                spdlog::warn("TextureManager: hot-reload read failed '{}'", e.path);
                continue;
            }
            int w = 0, h = 0, comp = 0;
            stbi_uc* pixels = stbi_load_from_memory(bytes.data(),
                                                    static_cast<int>(bytes.size()),
                                                    &w, &h, &comp, STBI_rgb_alpha);
            if (!pixels) {
                spdlog::warn("TextureManager: hot-reload decode failed '{}'", e.path);
                continue;
            }
            device_->wait_idle();  // no in-flight use of the old image
            const bool ok = sp->reload_from_pixels(pixels, static_cast<uint32_t>(w),
                                                   static_cast<uint32_t>(h),
                                                   e.settings.srgb, e.settings.gen_mips);
            stbi_image_free(pixels);
            if (ok) {
                spdlog::info("TextureManager: hot-reloaded '{}' (gen {})", e.path, sp->generation());
                reloaded.emplace_back(id, sp.get());
            }
        }
    }
    // Notify outside the lock (listeners may re-enter the manager).
    for (auto& [id, tex] : reloaded)
        for (auto& cb : listeners_) cb(id, tex);
}

size_t TextureManager::live_texture_count() const {
    std::lock_guard<std::mutex> lk(mutex_);
    size_t n = 0;
    for (auto& [id, e] : cache_) (void)id, n += (e.tex.use_count() > 0 ? 1 : 0);
    return n;
}

} // namespace gws::renderer::gpu
