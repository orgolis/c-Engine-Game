#pragma once

// ============================================================================
// Starter textures — generated, because a fresh project has none.
//
// The engine can apply a texture to anything and always could; what a new
// project lacks is a texture to apply. Telling someone to go and find a PNG
// before they can see whether their material reflects is a poor answer when a
// checkerboard is forty lines of code.
//
// These are the three that earn their place for LOOKING at a surface:
//
//   * Checker — the only pattern that makes a reflection legible. A flat colour
//     reflects a flat colour, so a mirror and a matte surface look identical.
//   * Grid — thin lines on a flat field, for reading scale and tiling on ground.
//   * UV — coloured quadrants with a marked origin, which is the only one of the
//     three that shows a FLIPPED or rotated UV set rather than hiding it.
//
// Written as 24-bit BMP: no encoder, no dependency, and stb_image (what the
// engine loads through) reads it. The pixels are what matters here, not the
// container.
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace schizo::editor::texgen {

struct RGB { uint8_t r, g, b; };

enum class Pattern { Checker, Grid, UV };

/// Raw image, row-major from the TOP, RGB.
struct Image {
    int              width  = 0;
    int              height = 0;
    std::vector<RGB> pixels;

    RGB& at(int x, int y) { return pixels[static_cast<size_t>(y) * width + x]; }
    const RGB& at(int x, int y) const { return pixels[static_cast<size_t>(y) * width + x]; }
};

/// `cells` is the number of squares across, not their pixel size, so the result
/// looks the same whatever resolution is asked for.
inline Image generate(Pattern p, int size = 512, int cells = 8) {
    if (size < 2) size = 2;
    if (cells < 1) cells = 1;

    Image img;
    img.width  = size;
    img.height = size;
    img.pixels.resize(static_cast<size_t>(size) * size, RGB{0, 0, 0});

    const int cell = size / cells > 0 ? size / cells : 1;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            RGB c{200, 200, 200};
            switch (p) {
                case Pattern::Checker: {
                    const bool odd = ((x / cell) + (y / cell)) % 2 != 0;
                    // Not black-and-white: pure black has no diffuse response at
                    // all, so half the surface would carry no lighting
                    // information and the texture would teach you nothing about
                    // the material under it.
                    c = odd ? RGB{60, 60, 65} : RGB{210, 210, 205};
                    break;
                }
                case Pattern::Grid: {
                    const int lx = x % cell, ly = y % cell;
                    const bool line = lx < 2 || ly < 2;
                    c = line ? RGB{70, 90, 110} : RGB{190, 195, 200};
                    break;
                }
                case Pattern::UV: {
                    const bool right = x >= size / 2;
                    const bool lower = y >= size / 2;
                    if (!right && !lower)      c = RGB{200,  60,  60};   // origin quadrant
                    else if (right && !lower)  c = RGB{ 60, 170,  80};
                    else if (!right && lower)  c = RGB{ 60, 110, 200};
                    else                       c = RGB{200, 190,  70};
                    // A white marker in the top-left corner ONLY. Without an
                    // asymmetric mark the four quadrants cannot distinguish a
                    // rotation from a flip.
                    if (x < cell && y < cell) c = RGB{245, 245, 245};
                    // Thin cell lines over the top so tiling stays countable.
                    if (x % cell < 1 || y % cell < 1) c = RGB{30, 30, 30};
                    break;
                }
            }
            img.at(x, y) = c;
        }
    }
    return img;
}

/// Encode as a 24-bit BMP. Rows are BOTTOM-UP and padded to 4 bytes, which is
/// the format's rule and the two things an encoder written from memory gets
/// wrong -- a wrong stride shears the image diagonally rather than failing.
inline std::string to_bmp(const Image& img) {
    const int row_bytes = img.width * 3;
    const int padding   = (4 - (row_bytes % 4)) % 4;
    const int stride    = row_bytes + padding;
    const uint32_t pixel_bytes = static_cast<uint32_t>(stride) * img.height;
    const uint32_t offset      = 14 + 40;
    const uint32_t file_size   = offset + pixel_bytes;

    std::string out;
    out.reserve(file_size);

    auto u16 = [&out](uint16_t v) {
        out.push_back(static_cast<char>(v & 0xFF));
        out.push_back(static_cast<char>((v >> 8) & 0xFF));
    };
    auto u32 = [&out](uint32_t v) {
        out.push_back(static_cast<char>(v & 0xFF));
        out.push_back(static_cast<char>((v >> 8) & 0xFF));
        out.push_back(static_cast<char>((v >> 16) & 0xFF));
        out.push_back(static_cast<char>((v >> 24) & 0xFF));
    };
    auto i32 = [&u32](int32_t v) { u32(static_cast<uint32_t>(v)); };

    // BITMAPFILEHEADER
    out.push_back('B'); out.push_back('M');
    u32(file_size);
    u16(0); u16(0);
    u32(offset);

    // BITMAPINFOHEADER
    u32(40);
    i32(img.width);
    i32(img.height);      // positive = bottom-up
    u16(1);               // planes
    u16(24);              // bits per pixel
    u32(0);               // BI_RGB, uncompressed
    u32(pixel_bytes);
    i32(2835); i32(2835); // ~72 DPI
    u32(0); u32(0);

    for (int y = img.height - 1; y >= 0; --y) {
        for (int x = 0; x < img.width; ++x) {
            const RGB& c = img.at(x, y);
            out.push_back(static_cast<char>(c.b));   // BMP is BGR
            out.push_back(static_cast<char>(c.g));
            out.push_back(static_cast<char>(c.r));
        }
        for (int i = 0; i < padding; ++i) out.push_back('\0');
    }
    return out;
}

inline std::string bmp_for(Pattern p, int size = 512, int cells = 8) {
    return to_bmp(generate(p, size, cells));
}

} // namespace schizo::editor::texgen
