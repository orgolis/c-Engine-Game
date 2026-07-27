/**
 * @file vulkan_virtual_texture.cpp
 * @brief TextureManager::open_streamable — isolated so <windows.h> (pulled by
 *        StreamableTexture → pak_file.h's MappedFile) stays out of the manager's
 *        main translation unit.
 */

#include "vulkan_texture_manager.h"
#include "vulkan_virtual_texture.h"

#include <spdlog/spdlog.h>

namespace gws::renderer::gpu {

std::shared_ptr<StreamableTexture> TextureManager::open_streamable(const std::string& vt_path) {
    auto st = std::make_shared<StreamableTexture>();
    if (!st->open(vt_path)) {
        spdlog::warn("TextureManager: cannot open streamable virtual texture '{}'", vt_path);
        return nullptr;
    }
    spdlog::info("TextureManager: opened streamable VT '{}' ({}x{}, {} mips, {} tiles @ {}px)",
                 vt_path, st->width(), st->height(), st->mip_count(),
                 st->tile_count(), st->tile_texels());
    return st;
}

} // namespace gws::renderer::gpu
