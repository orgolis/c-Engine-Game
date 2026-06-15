#pragma once

// Content addressing for the asset pipeline (Pillar C2 / Master Plan Stage 2).
// Every source asset has a stable AssetId; its cooked bytes carry a content
// hash so the cooker can be incremental (re-cook only when source/deps change)
// and the runtime can verify integrity.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace schizo::assets {

using AssetId = uint64_t;
inline constexpr AssetId invalid_asset = 0;

inline uint64_t fnv1a64(const void* data, size_t n) {
    uint64_t h = 1469598103934665603ull;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

// Stable id from the source asset's (project-relative) path. Low bit forced
// set so a valid id is never 0 (== invalid_asset).
inline AssetId asset_id_from_path(std::string_view path) {
    return fnv1a64(path.data(), path.size()) | 1ull;
}

// Hash of cooked bytes — used for incremental cook + integrity checks.
inline uint64_t content_hash(const void* data, size_t n) { return fnv1a64(data, n); }

}  // namespace schizo::assets
