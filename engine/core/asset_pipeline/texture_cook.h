#pragma once

// ====================
// Texture Cook — block compression + mips  (Pillar C2 / Master Plan Stage 2.2)
// ====================
//
// Turns an RGBA8 image into a GPU-native block-compressed, mipped texture the
// runtime uploads with NO CPU decode (BC formats are sampled directly by the
// GPU). BC1 (opaque/1-bit alpha, 4bpp), BC3 (RGBA, 8bpp), BC5 (2-channel, for
// normal maps), BC7 (high-quality RGBA, 8bpp). Encoders: bc7enc (BC7) + rgbcx
// (BC1/3/5). Offline only — include from the cooker, not the runtime.
//
//   auto blob = cook_texture(rgba, w, h, TexFormat::BC7);  // mipped BC7
//   CookedTextureView v; v.open(blob.data(), blob.size());

#include "texture_format.h"   // TexFormat / CookedTextureHeader / CookedTextureView (runtime-shared)

#include "bc7enc.h"
#include "rgbcx.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace schizo::assets {

namespace detail {

inline void ensure_encoders() {
    static bool s = [] {
        bc7enc_compress_block_init();
        rgbcx::init(rgbcx::bc1_approx_mode::cBC1Ideal);
        return true;
    }();
    (void)s;
}

// Box-filter 2x2 downsample of an RGBA8 image (dims halved, min 1).
inline std::vector<uint8_t> downsample(const std::vector<uint8_t>& src,
                                       uint32_t w, uint32_t h,
                                       uint32_t& ow, uint32_t& oh) {
    ow = std::max(1u, w / 2);
    oh = std::max(1u, h / 2);
    std::vector<uint8_t> dst(static_cast<size_t>(ow) * oh * 4);
    for (uint32_t y = 0; y < oh; ++y)
        for (uint32_t x = 0; x < ow; ++x) {
            const uint32_t x0 = std::min(x * 2, w - 1), x1 = std::min(x * 2 + 1, w - 1);
            const uint32_t y0 = std::min(y * 2, h - 1), y1 = std::min(y * 2 + 1, h - 1);
            for (int c = 0; c < 4; ++c) {
                const uint32_t s = src[(y0 * w + x0) * 4 + c] + src[(y0 * w + x1) * 4 + c] +
                                   src[(y1 * w + x0) * 4 + c] + src[(y1 * w + x1) * 4 + c];
                dst[(y * ow + x) * 4 + c] = static_cast<uint8_t>(s / 4);
            }
        }
    return dst;
}

// Compress one RGBA8 mip into BC blocks, appended to `out`.
inline void compress_mip(const uint8_t* rgba, uint32_t w, uint32_t h,
                         TexFormat fmt, std::vector<uint8_t>& out) {
    const uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
    bc7enc_compress_block_params p;
    bc7enc_compress_block_params_init(&p);
    uint8_t block[64];  // 4x4 RGBA
    for (uint32_t by = 0; by < bh; ++by)
        for (uint32_t bx = 0; bx < bw; ++bx) {
            for (uint32_t ry = 0; ry < 4; ++ry)
                for (uint32_t rx = 0; rx < 4; ++rx) {
                    const uint32_t px = std::min(bx * 4 + rx, w - 1);
                    const uint32_t py = std::min(by * 4 + ry, h - 1);
                    const uint8_t* s = rgba + (static_cast<size_t>(py) * w + px) * 4;
                    uint8_t* d = block + (ry * 4 + rx) * 4;
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
                }
            uint8_t enc[16];
            switch (fmt) {
                case TexFormat::BC7:
                    bc7enc_compress_block(enc, block, &p);
                    out.insert(out.end(), enc, enc + 16); break;
                case TexFormat::BC1:
                    rgbcx::encode_bc1(rgbcx::MAX_LEVEL, enc, block, true, false);
                    out.insert(out.end(), enc, enc + 8); break;
                case TexFormat::BC3:
                    rgbcx::encode_bc3(rgbcx::MAX_LEVEL, enc, block);
                    out.insert(out.end(), enc, enc + 16); break;
                case TexFormat::BC5:
                    rgbcx::encode_bc5(enc, block, 0, 1, 4);
                    out.insert(out.end(), enc, enc + 16); break;
            }
        }
}

}  // namespace detail

// Cook an RGBA8 image into a BC-compressed, mipped texture blob.
inline std::vector<uint8_t> cook_texture(const uint8_t* rgba, uint32_t w, uint32_t h,
                                         TexFormat fmt, bool gen_mips = true) {
    detail::ensure_encoders();

    std::vector<std::vector<uint8_t>> mips;
    mips.emplace_back(rgba, rgba + static_cast<size_t>(w) * h * 4);
    uint32_t mw = w, mh = h;
    while (gen_mips && (mw > 1 || mh > 1)) {
        uint32_t nw, nh;
        mips.push_back(detail::downsample(mips.back(), mw, mh, nw, nh));
        mw = nw; mh = nh;
    }

    CookedTextureHeader hdr{kCookedTexMagic, kCookedTexVersion,
                            static_cast<uint32_t>(fmt), w, h,
                            static_cast<uint32_t>(mips.size())};
    std::vector<uint8_t> out;
    const uint8_t* hp = reinterpret_cast<const uint8_t*>(&hdr);
    out.insert(out.end(), hp, hp + sizeof(hdr));

    mw = w; mh = h;
    for (auto& mip : mips) {
        detail::compress_mip(mip.data(), mw, mh, fmt, out);
        mw = std::max(1u, mw / 2);
        mh = std::max(1u, mh / 2);
    }
    return out;
}

// Cook an RGBA8 image into a *tiled, page-aligned* virtual texture (Stage 2.3
// stream-ready format): BC-compress + mip, then slice each mip into
// `tile_texels`-sized tiles (one BC-block sub-rectangle each) laid out so every
// tile starts on a `page_align` boundary. A streamer can later upload one tile
// at a time straight from the mapped bundle. Read back with CookedVTView.
inline std::vector<uint8_t> cook_virtual_texture(const uint8_t* rgba, uint32_t w, uint32_t h,
                                                 TexFormat fmt, uint32_t tile_texels = 128,
                                                 uint32_t page_align = 4096, bool gen_mips = true) {
    detail::ensure_encoders();
    const uint32_t bb  = bc_block_bytes(fmt);
    const uint32_t bpt = std::max(1u, tile_texels / 4);   // BC blocks per tile side

    // 1) RGBA mip chain (same as cook_texture).
    std::vector<std::vector<uint8_t>> mips;
    mips.emplace_back(rgba, rgba + static_cast<size_t>(w) * h * 4);
    uint32_t mw = w, mh = h;
    while (gen_mips && (mw > 1 || mh > 1)) {
        uint32_t nw, nh;
        mips.push_back(detail::downsample(mips.back(), mw, mh, nw, nh));
        mw = nw; mh = nh;
    }
    const uint32_t mip_count = static_cast<uint32_t>(mips.size());

    // 2) BC-compress each mip into its own block buffer; enumerate tiles.
    struct TileCopy { uint32_t mip, sbx, sby, tbw, tbh; };
    std::vector<std::vector<uint8_t>> mip_bc(mip_count);
    std::vector<CookedVTMip>  mipdesc(mip_count);
    std::vector<CookedVTTile> tiles;
    std::vector<TileCopy>     copies;

    mw = w; mh = h;
    for (uint32_t mi = 0; mi < mip_count; ++mi) {
        detail::compress_mip(mips[mi].data(), mw, mh, fmt, mip_bc[mi]);
        const uint32_t bw = (mw + 3) / 4, bh = (mh + 3) / 4;
        const uint32_t tnx = (bw + bpt - 1) / bpt, tny = (bh + bpt - 1) / bpt;
        mipdesc[mi] = CookedVTMip{ mw, mh, bw, bh, tnx, tny, static_cast<uint32_t>(tiles.size()), 0 };
        for (uint32_t ty = 0; ty < tny; ++ty)
            for (uint32_t tx = 0; tx < tnx; ++tx) {
                const uint32_t sbx = tx * bpt, sby = ty * bpt;
                const uint32_t tbw = std::min(bpt, bw - sbx), tbh = std::min(bpt, bh - sby);
                CookedVTTile t{}; t.mip = mi; t.tx = tx; t.ty = ty; t.size = tbw * tbh * bb;
                tiles.push_back(t);
                copies.push_back(TileCopy{ mi, sbx, sby, tbw, tbh });
            }
        mw = std::max(1u, mw / 2); mh = std::max(1u, mh / 2);
    }
    const uint32_t tile_count = static_cast<uint32_t>(tiles.size());

    // 3) page-aligned offsets.
    auto align_up = [&](uint64_t x) { return (x + page_align - 1) / page_align * page_align; };
    const uint64_t table_end = sizeof(CookedVTHeader)
                             + static_cast<uint64_t>(mip_count)  * sizeof(CookedVTMip)
                             + static_cast<uint64_t>(tile_count) * sizeof(CookedVTTile);
    uint64_t off = align_up(table_end);
    for (uint32_t i = 0; i < tile_count; ++i) { tiles[i].offset = off; off = align_up(off + tiles[i].size); }

    // 4) assemble: [header][mips][tiles][page-aligned tile data].
    std::vector<uint8_t> out(off, 0);
    CookedVTHeader vth{};
    vth.magic = kCookedVTMagic; vth.version = kCookedVTVersion; vth.format = static_cast<uint32_t>(fmt);
    vth.width = w; vth.height = h; vth.mip_count = mip_count;
    vth.tile_texels = tile_texels; vth.page_align = page_align; vth.tile_count = tile_count;
    std::memcpy(out.data(), &vth, sizeof(vth));
    std::memcpy(out.data() + sizeof(vth), mipdesc.data(), mip_count * sizeof(CookedVTMip));
    std::memcpy(out.data() + sizeof(vth) + mip_count * sizeof(CookedVTMip),
                tiles.data(), tile_count * sizeof(CookedVTTile));
    for (uint32_t i = 0; i < tile_count; ++i) {
        const TileCopy& c = copies[i];
        const uint32_t bw = mipdesc[c.mip].blocks_x;
        const uint8_t* src = mip_bc[c.mip].data();
        uint8_t* dst = out.data() + tiles[i].offset;
        for (uint32_t ly = 0; ly < c.tbh; ++ly)
            for (uint32_t lx = 0; lx < c.tbw; ++lx)
                std::memcpy(dst + (ly * c.tbw + lx) * bb,
                            src + ((static_cast<size_t>(c.sby + ly) * bw) + (c.sbx + lx)) * bb, bb);
    }
    return out;
}

}  // namespace schizo::assets
