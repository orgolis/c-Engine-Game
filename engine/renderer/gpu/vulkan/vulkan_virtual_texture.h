/**
 * @file vulkan_virtual_texture.h
 * @brief Renderer-side hook for cooked tiled virtual textures (`.vt`).
 *        (Master Plan Stage 2.3 format → Stage 8 runtime.)
 *
 * The cooker (assetcook) emits a `.vt`: BC-compressed, mipped, then sliced into
 * fixed-size *page-aligned* tiles with a tile directory (see CookedVTView in
 * texture_format.h). `StreamableTexture` memory-maps that file and exposes
 * ZERO-COPY access to any single tile — the OS pages data in on demand, so a
 * streamer can touch one tile without faulting the whole texture.
 *
 * What is LIVE here: the format, the mmap, and per-tile zero-copy addressing.
 * What is Stage 8 (not here): the runtime virtual-texture *system* — the
 * page-table indirection texture the shader samples, the GPU feedback pass that
 * reports needed tiles, the physical atlas, and the async tile uploader. This
 * class is the on-ramp those will build on.
 *
 * NOTE: this header pulls in <windows.h> (via pak_file.h's MappedFile) on
 * Windows. Include it only from .cpp files, never from another header, to keep
 * windows.h out of the wider translation units.
 */

#pragma once

#include "pak_file.h"        // schizo::assets::MappedFile (zero-copy mmap, RAII)
#include "texture_format.h"  // schizo::assets::CookedVTView / CookedVTHeader

#include <cstdint>
#include <string>

namespace gws::renderer::gpu {

class StreamableTexture {
public:
    StreamableTexture() = default;

    /// Memory-map a cooked `.vt` and open a view over it. Returns false (and
    /// stays closed) on any failure.
    bool open(const std::string& vt_path) {
        if (!file_.open(vt_path)) return false;
        if (!view_.open(file_.data(), file_.size())) { file_.close(); return false; }
        return true;
    }

    bool is_open() const { return view_.header != nullptr; }

    uint32_t width()       const { return view_.header ? view_.header->width       : 0; }
    uint32_t height()      const { return view_.header ? view_.header->height      : 0; }
    uint32_t mip_count()   const { return view_.header ? view_.header->mip_count   : 0; }
    uint32_t tile_texels() const { return view_.header ? view_.header->tile_texels : 0; }
    uint32_t tile_count()  const { return view_.header ? view_.header->tile_count  : 0; }

    /// Zero-copy bytes of one tile's BC blocks, straight from the mapping (no
    /// copy, no fault of other tiles). nullptr if out of range; `out_size` is
    /// the tile's byte size. A future streamer hands these to a staging upload.
    const uint8_t* tile(uint32_t mip, uint32_t tx, uint32_t ty, uint32_t& out_size) const {
        return view_.tile(mip, tx, ty, out_size);
    }

    const schizo::assets::CookedVTView& view() const { return view_; }

private:
    schizo::assets::MappedFile   file_;
    schizo::assets::CookedVTView view_;
};

} // namespace gws::renderer::gpu
