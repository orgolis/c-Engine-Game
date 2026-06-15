#pragma once

// ====================
// Compression  (Pillar C2 / Master Plan Stage 2.2)
// ====================
//
// One front-end over the vendored codecs: LZ4 (fast, for cooked mesh/anim that
// must load quickly) and Zstd (higher ratio, for cold bulk). The cooker picks a
// codec per asset; the runtime decompresses on load. The uncompressed size is
// stored by the container (the pak entry), not in the codec stream, so decode
// is a single allocation + call.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gws::compress {

enum class Codec : uint32_t { None = 0, LZ4 = 1, Zstd = 2 };

const char* codec_name(Codec c);

// Compress src[0..n). `level` 0 = codec default (LZ4-HC default / Zstd 19).
// Returns the compressed bytes, or empty on failure. Codec::None copies.
std::vector<uint8_t> compress(Codec codec, const void* src, size_t n, int level = 0);

// Decompress src[0..n) into dst (exactly `uncompressed_size` bytes). Returns
// false if the codec errors or the output size doesn't match.
bool decompress(Codec codec, const void* src, size_t n,
                void* dst, size_t uncompressed_size);

// Convenience: allocate + decompress. Empty on failure.
std::vector<uint8_t> decompress(Codec codec, const void* src, size_t n,
                                size_t uncompressed_size);

}  // namespace gws::compress
