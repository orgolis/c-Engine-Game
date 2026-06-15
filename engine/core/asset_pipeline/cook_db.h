#pragma once

// ====================
// Incremental Cook DB  (Pillar C2 / Master Plan Stage 2.2)
// ====================
//
// The "what's already cooked" brain that makes the cooker incremental — its
// acceptance criterion is "change one asset, re-cook ONLY that one". Maps a
// source path -> {content hash, cooked AssetId, type, cooker version}. A cook
// is skipped when the source hash AND the cooker version both match the record.
// Persisted via the reflection-driven archive (Stage 0.5) so it survives runs.
//
//   CookDb db; db.load(blob.data(), blob.size());
//   if (db.needs_cook(path, content_hash(src,n), kCookerVersion)) {
//       ... cook ... ; db.record(path, hash, cooked_id, tag, kCookerVersion);
//   }
//   auto out = db.save();

#include "asset_id.h"
#include "serialization/serialization_traits.h"   // std::string / std::vector fields
#include "serialization/serialization.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace schizo::assets {

struct CookRecord {
    std::string source_path;
    uint64_t    source_hash    = 0;   // content_hash() of the source bytes
    uint64_t    cooked_id      = 0;   // AssetId of the cooked output
    uint32_t    type_tag       = 0;
    uint32_t    cooker_version = 0;   // cook-logic version that produced it
    uint64_t    dep_hash       = 0;   // combined content hash of `deps` at cook time
    std::vector<std::string> deps;    // dependency source paths (.mtl / .bin / textures)
};

struct CookManifest {
    std::vector<CookRecord> records;
};

}  // namespace schizo::assets

GWS_REFLECT_BEGIN(schizo::assets::CookRecord)
    GWS_REFLECT_FIELD(source_path)
    GWS_REFLECT_FIELD(source_hash)
    GWS_REFLECT_FIELD(cooked_id)
    GWS_REFLECT_FIELD(type_tag)
    GWS_REFLECT_FIELD(cooker_version)
    GWS_REFLECT_FIELD(dep_hash)
    GWS_REFLECT_FIELD(deps)
GWS_REFLECT_END()

GWS_REFLECT_BEGIN(schizo::assets::CookManifest)
    GWS_REFLECT_FIELD(records)
GWS_REFLECT_END()

namespace schizo::assets {

class CookDb {
public:
    // The element type must be REGISTERED (not just the container's owner) so
    // the vector serializer takes the reflected path for each CookRecord rather
    // than the null POD fallback. (Same reason scene_cook pre-reflects its
    // element types.) Idempotent.
    static void ensure_registered() {
        gws::reflect::reflect<CookRecord>();
        gws::reflect::reflect<CookManifest>();
    }

    // Load a previously-saved manifest blob. An empty blob is a valid empty db.
    bool load(const void* data, size_t n) {
        ensure_registered();
        manifest_.records.clear();
        index_.clear();
        bool ok = true;
        if (n != 0) ok = gws::serialize::load(data, n, manifest_);
        rebuild_index();
        return ok;
    }
    std::vector<uint8_t> save() const {
        ensure_registered();
        return gws::serialize::save(manifest_);
    }

    const CookRecord* find(const std::string& source_path) const {
        auto it = index_.find(source_path);
        return it == index_.end() ? nullptr : &manifest_.records[it->second];
    }

    // True when the source must be (re)cooked: never cooked, content changed,
    // the cooker logic version changed, OR a dependency changed (dep_hash is
    // the caller's freshly-recomputed combined hash of this record's `deps`).
    bool needs_cook(const std::string& source_path, uint64_t source_hash,
                    uint32_t cooker_version, uint64_t dep_hash = 0) const {
        const CookRecord* r = find(source_path);
        return !r || r->source_hash != source_hash
                  || r->cooker_version != cooker_version
                  || r->dep_hash != dep_hash;
    }

    // Insert or update the record for a successful cook, including the
    // dependency set + the combined hash of those dependencies at cook time.
    void record(const std::string& source_path, uint64_t source_hash,
                AssetId cooked_id, uint32_t type_tag, uint32_t cooker_version,
                std::vector<std::string> deps = {}, uint64_t dep_hash = 0) {
        auto it = index_.find(source_path);
        if (it != index_.end()) {
            CookRecord& r = manifest_.records[it->second];
            r.source_hash    = source_hash;
            r.cooked_id      = cooked_id;
            r.type_tag       = type_tag;
            r.cooker_version = cooker_version;
            r.dep_hash       = dep_hash;
            r.deps           = std::move(deps);
        } else {
            index_.emplace(source_path, manifest_.records.size());
            manifest_.records.push_back(
                CookRecord{source_path, source_hash, cooked_id, type_tag,
                           cooker_version, dep_hash, std::move(deps)});
        }
    }

    size_t              size()     const { return manifest_.records.size(); }
    const CookManifest& manifest() const { return manifest_; }

private:
    void rebuild_index() {
        index_.clear();
        for (size_t i = 0; i < manifest_.records.size(); ++i)
            index_.emplace(manifest_.records[i].source_path, i);
    }

    CookManifest                            manifest_;
    std::unordered_map<std::string, size_t> index_;
};

}  // namespace schizo::assets
