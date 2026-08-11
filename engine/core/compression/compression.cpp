#include "compression/compression.h"

#include "lz4.h"
#include "lz4hc.h"
#include "zstd.h"

#include <climits>
#include <cstring>
#include <cstdint>

namespace gws::compress {

const char* codec_name(Codec c) {
    switch (c) {
        case Codec::None: return "none";
        case Codec::LZ4:  return "lz4";
        case Codec::Zstd: return "zstd";
    }
    return "?";
}

std::vector<uint8_t> compress(Codec codec, const void* src, size_t n, int level) {
    if (codec == Codec::None || n == 0) {
        std::vector<uint8_t> out(n);
        if (n) std::memcpy(out.data(), src, n);
        return out;
    }

    if (codec == Codec::LZ4) {
        if (n > static_cast<size_t>(INT_MAX)) return {};  // block format limit
        const int bound = LZ4_compressBound(static_cast<int>(n));
        std::vector<uint8_t> out(static_cast<size_t>(bound));
        const int lvl = level > 0 ? level : LZ4HC_CLEVEL_DEFAULT;
        const int got = LZ4_compress_HC(static_cast<const char*>(src),
                                        reinterpret_cast<char*>(out.data()),
                                        static_cast<int>(n), bound, lvl);
        if (got <= 0) return {};
        out.resize(static_cast<size_t>(got));
        return out;
    }

    if (codec == Codec::Zstd) {
        const size_t bound = ZSTD_compressBound(n);
        std::vector<uint8_t> out(bound);
        const int lvl = level > 0 ? level : 19;
        const size_t got = ZSTD_compress(out.data(), bound, src, n, lvl);
        if (ZSTD_isError(got)) return {};
        out.resize(got);
        return out;
    }

    return {};
}

bool decompress(Codec codec, const void* src, size_t n,
                void* dst, size_t uncompressed_size) {
    if (codec == Codec::None) {
        if (n != uncompressed_size) return false;
        if (n) std::memcpy(dst, src, n);
        return true;
    }

    if (codec == Codec::LZ4) {
        if (n > static_cast<size_t>(INT_MAX) ||
            uncompressed_size > static_cast<size_t>(INT_MAX)) return false;
        const int got = LZ4_decompress_safe(
            static_cast<const char*>(src), reinterpret_cast<char*>(dst),
            static_cast<int>(n), static_cast<int>(uncompressed_size));
        return got == static_cast<int>(uncompressed_size);
    }

    if (codec == Codec::Zstd) {
        const size_t got = ZSTD_decompress(dst, uncompressed_size, src, n);
        return !ZSTD_isError(got) && got == uncompressed_size;
    }

    return false;
}

std::vector<uint8_t> decompress(Codec codec, const void* src, size_t n,
                                size_t uncompressed_size) {
    std::vector<uint8_t> out(uncompressed_size);
    if (!decompress(codec, src, n, out.data(), uncompressed_size)) return {};
    return out;
}

}  // namespace gws::compress
