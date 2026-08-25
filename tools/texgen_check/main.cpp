// ============================================================================
// texgen_check -- the starter textures are real images with the pattern they
// claim.
//
// Two failure modes, neither of which errors:
//
//   * A BMP with the wrong row stride SHEARS diagonally instead of failing to
//     load. Rows are bottom-up and padded to a 4-byte boundary; both are easy
//     to write from memory and get wrong, and 512 is divisible by 4 so a
//     forgotten pad would pass at the default size and break at every odd one.
//   * A generator that emits a FLAT colour produces a perfectly valid image
//     that teaches nothing -- a mirror and a matte surface reflect a flat
//     colour identically, which is the exact thing these exist to show.
//
// So the pattern itself is asserted, not just the header.
// ============================================================================

#include "texture_gen.h"

#include <cstdio>
#include <set>
#include <string>

namespace tg = schizo::editor::texgen;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

uint32_t rd32(const std::string& s, size_t at) {
    return  static_cast<uint8_t>(s[at])
         | (static_cast<uint8_t>(s[at + 1]) << 8)
         | (static_cast<uint8_t>(s[at + 2]) << 16)
         | (static_cast<uint32_t>(static_cast<uint8_t>(s[at + 3])) << 24);
}
uint16_t rd16(const std::string& s, size_t at) {
    return static_cast<uint16_t>(static_cast<uint8_t>(s[at]) |
                                 (static_cast<uint8_t>(s[at + 1]) << 8));
}

}  // namespace

int main() {
    std::printf("=== texgen_check ===\n\n");

    std::printf("BMP header is well-formed:\n");
    {
        const std::string bmp = tg::bmp_for(tg::Pattern::Checker, 64, 8);
        check(bmp.size() > 54, "the file is bigger than its headers");
        check(bmp[0] == 'B' && bmp[1] == 'M', "starts with the BM magic");
        check(rd32(bmp, 2) == bmp.size(), "the recorded file size matches the actual size");
        check(rd32(bmp, 10) == 54u, "pixel data starts after both headers");
        check(rd32(bmp, 14) == 40u, "BITMAPINFOHEADER size is 40");
        check(static_cast<int32_t>(rd32(bmp, 18)) == 64, "width");
        check(static_cast<int32_t>(rd32(bmp, 22)) == 64, "height, positive = bottom-up");
        check(rd16(bmp, 26) == 1, "one colour plane");
        check(rd16(bmp, 28) == 24, "24 bits per pixel");
        check(rd32(bmp, 30) == 0, "uncompressed (BI_RGB)");
        check(bmp.size() == 54u + 64u * 64u * 3u, "64x64 needs no row padding");
    }

    std::printf("\nRow padding, where getting it wrong shears the image:\n");
    {
        // 63 px * 3 bytes = 189, which needs 3 bytes of padding per row. The
        // default 512 would hide this: 512*3 is already a multiple of 4.
        const std::string bmp = tg::bmp_for(tg::Pattern::Checker, 63, 7);
        const size_t stride = 63 * 3 + 3;
        check(bmp.size() == 54 + stride * 63, "an odd width is padded to a 4-byte stride");
        check(rd32(bmp, 34) == stride * 63, "and the recorded image size agrees");
        check(bmp.size() % 4 == 2, "total size = 54 + a multiple of 4");
    }

    std::printf("\nThe checker is actually a checker:\n");
    {
        const tg::Image img = tg::generate(tg::Pattern::Checker, 64, 8);
        check(img.width == 64 && img.height == 64, "the requested size is honoured");
        check(img.pixels.size() == 64u * 64u, "every pixel exists");

        // cell = 8. (0,0) and (8,8) are the same parity; (8,0) is the other.
        const tg::RGB a = img.at(0, 0);
        const tg::RGB b = img.at(8, 0);
        const tg::RGB c = img.at(8, 8);
        check(!(a.r == b.r && a.g == b.g && a.b == b.b),
              "neighbouring cells differ -- this is what a flat fill would fail");
        check(a.r == c.r && a.g == c.g && a.b == c.b,
              "diagonal cells match, which is what makes it a CHECKER and not stripes");

        // Neither shade may be pure black: a black cell has no diffuse response,
        // so half the surface would carry no lighting information at all.
        check(a.r > 10 && b.r > 10, "no cell is pure black");
        check(a.r != b.r, "the two shades are genuinely different");
    }

    std::printf("\nCell count follows the request, not the resolution:\n");
    {
        const tg::Image small = tg::generate(tg::Pattern::Checker, 64, 8);
        const tg::Image big   = tg::generate(tg::Pattern::Checker, 256, 8);
        // Same cell count means the corner of the second cell sits at the same
        // FRACTION across, whatever the pixel size.
        const tg::RGB s = small.at(8, 0);
        const tg::RGB l = big.at(32, 0);
        check(s.r == l.r && s.g == l.g && s.b == l.b,
              "8 cells across looks the same at 64px and at 256px");
    }

    std::printf("\nThe UV chart can distinguish a flip from a rotation:\n");
    {
        const tg::Image img = tg::generate(tg::Pattern::UV, 64, 8);
        // Four quadrants, four different colours: three would make one pair of
        // orientations indistinguishable.
        std::set<std::string> quads;
        auto key = [](tg::RGB c) {
            return std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b);
        };
        quads.insert(key(img.at(20, 20)));
        quads.insert(key(img.at(44, 20)));
        quads.insert(key(img.at(20, 44)));
        quads.insert(key(img.at(44, 44)));
        check(quads.size() == 4, "all four quadrants are different colours");

        const tg::RGB corner = img.at(2, 2);
        check(corner.r > 200 && corner.g > 200 && corner.b > 200,
              "the top-left corner carries a light marker");
        const tg::RGB opposite = img.at(60, 60);
        check(!(opposite.r > 200 && opposite.g > 200 && opposite.b > 200),
              "and the opposite corner does not -- the asymmetry is the whole point");
    }

    std::printf("\nThe grid draws lines rather than filling:\n");
    {
        const tg::Image img = tg::generate(tg::Pattern::Grid, 64, 8);
        const tg::RGB on_line = img.at(0, 0);
        const tg::RGB inside  = img.at(4, 4);
        check(!(on_line.r == inside.r && on_line.g == inside.g && on_line.b == inside.b),
              "a line pixel differs from the field");
        check(inside.r > on_line.r, "the field is lighter than the lines");

        // Lines must be the minority, or it is not a grid, it is a fill.
        size_t line_px = 0;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const tg::RGB c = img.at(x, y);
                if (c.r == on_line.r && c.g == on_line.g && c.b == on_line.b) ++line_px;
            }
        check(line_px * 2 < 64u * 64u, "lines cover less than half the image");
        check(line_px > 0, "and there are some");
    }

    std::printf("\nDegenerate requests are clamped rather than crashing:\n");
    {
        const tg::Image tiny = tg::generate(tg::Pattern::Checker, 0, 0);
        check(tiny.width >= 2 && tiny.height >= 2, "a zero size is clamped to something drawable");
        check(tiny.pixels.size() == static_cast<size_t>(tiny.width) * tiny.height,
              "and the buffer matches the clamped size");
        const tg::Image neg = tg::generate(tg::Pattern::Grid, -10, -3);
        check(neg.width >= 2 && !neg.pixels.empty(), "negatives too");
    }

    std::printf("\n%d/%d checks passed\n", g_pass, g_pass + g_fail);
    if (g_fail) { std::printf("FAILED\n"); return 1; }
    std::printf("OK\n");
    return 0;
}
