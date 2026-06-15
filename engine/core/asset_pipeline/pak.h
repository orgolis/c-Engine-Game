#pragma once

// ====================
// Pak / Bundle  (Pillar C2 / Master Plan Stage 2.2)
// ====================
//
// A cooked bundle: a flat blob of concatenated asset bytes plus a TOC that
// maps AssetId -> {offset, size, content hash, type tag}. The runtime opens
// it once (ideally mmap'd) and gets ZERO-COPY spans into the buffer — no
// per-asset parsing. Writer is offline (the cooker); reader is the runtime.
//
// Layout (little-endian):
//   [u32 magic 'GPAK'][u32 version][u32 count][u64 toc_offset]
//   [blob bytes ...]
//   [TOC @ toc_offset: count x { u64 id, u32 type, u64 offset, u64 size, u64 hash }]
//
// Compression is per-asset (v2): each entry carries a `codec` (LZ4/Zstd/none)
// and the `uncompressed_size`. `get()` returns the stored (possibly compressed)
// bytes zero-copy; `read()` returns the usable (decompressed) bytes.

#include "asset_id.h"
#include "compression/compression.h"

#include <cstring>
#include <unordered_map>
#include <vector>

namespace schizo::assets {

constexpr uint32_t kPakMagic   = 0x4B415047u;  // 'GPAK'
constexpr uint32_t kPakVersion = 2u;           // 2 = per-entry compression

struct PakEntry {
    AssetId  id                = invalid_asset;
    uint32_t type_tag          = 0;
    uint64_t offset            = 0;   // absolute byte offset into the bundle
    uint64_t size              = 0;   // STORED size (compressed if codec != none)
    uint64_t hash              = 0;   // content_hash of the STORED bytes
    uint32_t codec             = 0;   // gws::compress::Codec
    uint64_t uncompressed_size = 0;   // original size (== size when codec none)
};

class PakWriter {
public:
    // Append a cooked asset, optionally compressed. type_tag lets the runtime
    // know how to interpret the bytes (mesh / texture / material / scene / ...).
    void add(AssetId id, uint32_t type_tag, const void* data, size_t size,
             gws::compress::Codec codec = gws::compress::Codec::None) {
        PakEntry e;
        e.id                = id;
        e.type_tag          = type_tag;
        e.uncompressed_size = size;
        e.codec             = static_cast<uint32_t>(codec);

        std::vector<uint8_t> stored;
        const uint8_t* p = static_cast<const uint8_t*>(data);
        if (codec == gws::compress::Codec::None) {
            stored.assign(p, p + size);
        } else {
            stored = gws::compress::compress(codec, data, size);
            if (stored.empty() && size > 0) {     // compression failed -> raw
                e.codec = static_cast<uint32_t>(gws::compress::Codec::None);
                stored.assign(p, p + size);
            }
        }
        e.offset = blob_.size();                  // relative; made absolute at finalize
        e.size   = stored.size();
        e.hash   = content_hash(stored.data(), stored.size());
        blob_.insert(blob_.end(), stored.begin(), stored.end());
        entries_.push_back(e);
    }
    void add(AssetId id, uint32_t type_tag, const std::vector<uint8_t>& bytes,
             gws::compress::Codec codec = gws::compress::Codec::None) {
        add(id, type_tag, bytes.data(), bytes.size(), codec);
    }

    size_t asset_count() const { return entries_.size(); }

    std::vector<uint8_t> finalize() const {
        std::vector<uint8_t> out;
        auto put = [&](const void* d, size_t n) {
            const uint8_t* p = static_cast<const uint8_t*>(d);
            out.insert(out.end(), p, p + n);
        };
        const uint32_t count = static_cast<uint32_t>(entries_.size());
        put(&kPakMagic, 4);
        put(&kPakVersion, 4);
        put(&count, 4);
        const size_t toc_off_field = out.size();
        uint64_t toc_offset = 0;
        put(&toc_offset, 8);                  // placeholder, patched below

        const uint64_t blob_start = out.size();
        out.insert(out.end(), blob_.begin(), blob_.end());

        toc_offset = out.size();
        std::memcpy(&out[toc_off_field], &toc_offset, 8);
        for (PakEntry e : entries_) {
            const uint64_t abs = blob_start + e.offset;
            put(&e.id, 8); put(&e.type_tag, 4); put(&abs, 8); put(&e.size, 8);
            put(&e.hash, 8); put(&e.codec, 4); put(&e.uncompressed_size, 8);
        }
        return out;
    }

private:
    std::vector<PakEntry> entries_;
    std::vector<uint8_t>  blob_;
};

class PakReader {
public:
    // Parse the TOC of an in-memory bundle. The buffer must outlive the
    // reader (get() returns zero-copy spans into it).
    bool open(const void* data, size_t n) {
        data_ = static_cast<const uint8_t*>(data);
        size_ = n;
        toc_.clear();
        if (n < 20) return false;
        uint32_t magic, version, count;
        uint64_t toc_offset;
        std::memcpy(&magic, data_ + 0, 4);
        std::memcpy(&version, data_ + 4, 4);
        std::memcpy(&count, data_ + 8, 4);
        std::memcpy(&toc_offset, data_ + 12, 8);
        if (magic != kPakMagic) return false;
        // v1 = 36-byte entries (no compression); v2 = 48-byte (codec + usize).
        const size_t entry_size = (version >= 2u) ? 48u : 36u;
        if (toc_offset + static_cast<uint64_t>(count) * entry_size > n) return false;
        const uint8_t* p = data_ + toc_offset;
        for (uint32_t i = 0; i < count; ++i, p += entry_size) {
            PakEntry e;
            std::memcpy(&e.id,       p + 0,  8);
            std::memcpy(&e.type_tag, p + 8,  4);
            std::memcpy(&e.offset,   p + 12, 8);
            std::memcpy(&e.size,     p + 20, 8);
            std::memcpy(&e.hash,     p + 28, 8);
            if (version >= 2u) {
                std::memcpy(&e.codec,             p + 36, 4);
                std::memcpy(&e.uncompressed_size, p + 40, 8);
            } else {
                e.codec = 0;
                e.uncompressed_size = e.size;
            }
            if (e.offset + e.size > n) return false;       // corrupt TOC guard
            toc_.emplace(e.id, e);
        }
        return true;
    }

    bool has(AssetId id) const { return toc_.find(id) != toc_.end(); }

    // Zero-copy span into the opened buffer (the STORED bytes — compressed when
    // codec != none); nullptr if not present. Use read() for usable bytes.
    const uint8_t* get(AssetId id, size_t& out_size) const {
        auto it = toc_.find(id);
        if (it == toc_.end()) { out_size = 0; return nullptr; }
        out_size = it->second.size;
        return data_ + it->second.offset;
    }

    // Usable (decompressed) bytes for an asset. For uncompressed entries this
    // copies the span; for compressed entries it decompresses. Empty if absent
    // or on decode failure.
    std::vector<uint8_t> read(AssetId id) const {
        auto it = toc_.find(id);
        if (it == toc_.end()) return {};
        const PakEntry& e = it->second;
        const uint8_t* p = data_ + e.offset;
        const auto codec = static_cast<gws::compress::Codec>(e.codec);
        if (codec == gws::compress::Codec::None)
            return std::vector<uint8_t>(p, p + e.size);
        return gws::compress::decompress(codec, p, e.size, e.uncompressed_size);
    }
    const PakEntry* entry(AssetId id) const {
        auto it = toc_.find(id);
        return it != toc_.end() ? &it->second : nullptr;
    }
    // Recompute the content hash and compare — integrity check.
    bool verify(AssetId id) const {
        size_t sz;
        const uint8_t* p = get(id, sz);
        return p && content_hash(p, sz) == toc_.at(id).hash;
    }

    size_t count() const { return toc_.size(); }
    const std::unordered_map<AssetId, PakEntry>& toc() const { return toc_; }

private:
    const uint8_t* data_ = nullptr;
    size_t         size_ = 0;
    std::unordered_map<AssetId, PakEntry> toc_;
};

}  // namespace schizo::assets
