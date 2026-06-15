#pragma once

// ====================
// Cooked texture format — runtime-readable header  (Master Plan Stage 2.2)
// ====================
//
// The format definitions + zero-copy reader for a cooked BC texture blob,
// split out of texture_cook.h so the RUNTIME can read cooked textures WITHOUT
// pulling in the offline BC encoders (bc7enc/rgbcx). The cooker includes
// texture_cook.h (which includes this); the runtime includes only this.

#include <algorithm>
#include <cstdint>

namespace schizo::assets {

enum class TexFormat : uint32_t { BC1 = 1, BC3 = 3, BC5 = 5, BC7 = 7 };

inline constexpr uint32_t kCookedTexMagic   = 0x58455443u;  // 'C','T','E','X'
inline constexpr uint32_t kCookedTexVersion = 1u;

struct CookedTextureHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t format;     // TexFormat
    uint32_t width;
    uint32_t height;
    uint32_t mip_count;
};

inline uint32_t bc_block_bytes(TexFormat f) { return f == TexFormat::BC1 ? 8u : 16u; }
inline uint32_t mip_blocks(uint32_t w, uint32_t h) { return ((w + 3) / 4) * ((h + 3) / 4); }

// Zero-copy view over a cooked-texture blob (all mips concatenated).
struct CookedTextureView {
    const CookedTextureHeader* header = nullptr;
    const uint8_t*             blocks = nullptr;

    bool open(const void* data, size_t n) {
        if (n < sizeof(CookedTextureHeader)) return false;
        header = static_cast<const CookedTextureHeader*>(data);
        if (header->magic != kCookedTexMagic) return false;
        blocks = static_cast<const uint8_t*>(data) + sizeof(CookedTextureHeader);
        size_t need = sizeof(CookedTextureHeader);
        const uint32_t bb = bc_block_bytes(static_cast<TexFormat>(header->format));
        uint32_t mw = header->width, mh = header->height;
        for (uint32_t i = 0; i < header->mip_count; ++i) {
            need += static_cast<size_t>(mip_blocks(mw, mh)) * bb;
            mw = std::max(1u, mw / 2); mh = std::max(1u, mh / 2);
        }
        return need <= n;
    }

    // Byte size of mip level `i` (full BC blocks).
    uint32_t mip_byte_size(uint32_t i) const {
        const uint32_t bb = bc_block_bytes(static_cast<TexFormat>(header->format));
        uint32_t mw = header->width, mh = header->height;
        for (uint32_t m = 0; m < i; ++m) { mw = std::max(1u, mw / 2); mh = std::max(1u, mh / 2); }
        return mip_blocks(mw, mh) * bb;
    }
};

// ====================
// Virtual / streamable texture format  (Pillar C3 / Master Plan Stage 2.3)
// ====================
//
// The cooked texture, BC-compressed and then sliced into fixed-size, *page-
// aligned* tiles (one BC-block sub-rectangle each), with a per-mip grid + a tile
// directory. This is the "asset format is tiled, page-aligned so cooked data is
// stream-ready" deliverable: a future streamer (Stage 8) can mmap the bundle and
// DMA/upload ONE tile at a time — constant VRAM regardless of texture size —
// without parsing or touching the other tiles. The runtime virtual-texture
// SYSTEM (page-table indirection texture, GPU feedback pass, physical atlas,
// async streamer) is Stage 8; here only the format + zero-copy reader are fixed.
//
//   auto blob = cook_virtual_texture(rgba, w, h, TexFormat::BC7);  // tiled
//   CookedVTView v; v.open(blob.data(), blob.size());
//   uint32_t sz; const uint8_t* page = v.tile(/*mip*/0, /*tx*/3, /*ty*/2, sz);

inline constexpr uint32_t kCookedVTMagic   = 0x58545643u;  // 'C','V','T','X'
inline constexpr uint32_t kCookedVTVersion = 1u;

struct CookedVTHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t format;        // TexFormat
    uint32_t width;         // base texel dims
    uint32_t height;
    uint32_t mip_count;
    uint32_t tile_texels;   // tile side in texels (e.g. 128)
    uint32_t page_align;    // byte alignment of every tile's data (e.g. 4096)
    uint32_t tile_count;    // total tiles across all mips
    uint32_t _pad;
    // followed by: CookedVTMip[mip_count], CookedVTTile[tile_count], then the
    // page-aligned tile data.
};

struct CookedVTMip {
    uint32_t width, height;     // texel dims of this mip
    uint32_t blocks_x, blocks_y;// BC-block grid of this mip
    uint32_t tiles_x, tiles_y;  // tile grid of this mip
    uint32_t first_tile;        // index of this mip's first tile in the tile table
    uint32_t _pad;
};

struct CookedVTTile {
    uint64_t offset;   // absolute, page-aligned byte offset into the blob
    uint32_t size;     // tile byte size (its BC blocks)
    uint32_t mip;
    uint32_t tx, ty;   // tile coord within its mip's grid
};

// Zero-copy reader: addresses any tile by (mip, tx, ty) without touching others.
struct CookedVTView {
    const CookedVTHeader* header = nullptr;
    const CookedVTMip*    mips   = nullptr;
    const CookedVTTile*   tiles  = nullptr;
    const uint8_t*        base   = nullptr;   // blob start (tile offsets are absolute)
    size_t                size   = 0;

    bool open(const void* data, size_t n) {
        if (n < sizeof(CookedVTHeader)) return false;
        header = static_cast<const CookedVTHeader*>(data);
        if (header->magic != kCookedVTMagic) return false;
        const uint8_t* p = static_cast<const uint8_t*>(data);
        size_t need = sizeof(CookedVTHeader)
                    + static_cast<size_t>(header->mip_count)  * sizeof(CookedVTMip)
                    + static_cast<size_t>(header->tile_count) * sizeof(CookedVTTile);
        if (need > n) return false;
        mips  = reinterpret_cast<const CookedVTMip*>(p + sizeof(CookedVTHeader));
        tiles = reinterpret_cast<const CookedVTTile*>(
            p + sizeof(CookedVTHeader) + static_cast<size_t>(header->mip_count) * sizeof(CookedVTMip));
        base  = p;
        size  = n;
        return true;
    }

    // Page-aligned bytes of one tile (zero-copy span into the blob); nullptr if
    // out of range or the offset/size would run past the buffer.
    const uint8_t* tile(uint32_t mip, uint32_t tx, uint32_t ty, uint32_t& out_size) const {
        out_size = 0;
        if (mip >= header->mip_count) return nullptr;
        const CookedVTMip& m = mips[mip];
        if (tx >= m.tiles_x || ty >= m.tiles_y) return nullptr;
        const uint32_t idx = m.first_tile + ty * m.tiles_x + tx;
        if (idx >= header->tile_count) return nullptr;
        const CookedVTTile& t = tiles[idx];
        if (t.offset + t.size > size) return nullptr;
        out_size = t.size;
        return base + t.offset;
    }
};

}  // namespace schizo::assets
