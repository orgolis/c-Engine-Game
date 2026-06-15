#pragma once

// ====================
// Serialization — reflection-driven  (Pillar A4 / Master Plan Stage 0.5)
// ====================
//
// Walk a type's fields via reflection (Stage 0.4) and emit/consume each via
// type-erased per-field stream functions — no hand-written per-type code.
// BINARY back-end: tagged, little-endian, FIELD-ID keyed (add/remove/reorder
// safe), versioned with per-type upgrade fns.
//
//   auto blob = gws::serialize::save(comp);   // std::vector<uint8_t>
//   MyComp c{};  gws::serialize::load(blob, c);
//
// POD + nested-reflected fields work out of the box. For std::string /
// std::vector / std::array fields, also include "serialization_traits.h"
// (before the type's GWS_REFLECT_ block) so the container serializers are
// captured. A human-readable TextArchive sharing this field-walk is a
// follow-up.

#include "reflection/reflection.h"

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace gws::serialize {

using reflect::FieldInfo;
using reflect::IByteSink;
using reflect::IByteSource;
using reflect::Registry;
using reflect::TypeId;
using reflect::TypeInfo;

// Stable per-field key — FNV of the field name (order-independent).
inline TypeId field_id(const FieldInfo& f) {
    return reflect::fnv1a(std::string_view(f.name));
}
inline const FieldInfo* find_field_by_id(const TypeInfo& ti, TypeId fid) {
    for (const FieldInfo& f : ti.fields)
        if (field_id(f) == fid) return &f;
    return nullptr;
}

// ---- concrete binary sink/source ----
class BinaryWriter : public IByteSink {
public:
    explicit BinaryWriter(std::vector<uint8_t>& out) : buf_(out) {}
    void write(const void* p, size_t n) override {
        const auto* b = static_cast<const uint8_t*>(p);
        buf_.insert(buf_.end(), b, b + n);
    }
    template <typename T> void pod(const T& v) { write(&v, sizeof(T)); }
private:
    std::vector<uint8_t>& buf_;
};

class BinaryReader : public IByteSource {
public:
    BinaryReader(const void* data, size_t n)
        : p_(static_cast<const uint8_t*>(data)), end_(p_ + n) {}
    bool read(void* out, size_t n) override {
        if (p_ + n > end_) return false;
        std::memcpy(out, p_, n);
        p_ += n;
        return true;
    }
    bool skip(size_t n) override {
        if (p_ + n > end_) return false;
        p_ += n;
        return true;
    }
    template <typename T> bool pod(T& v) { return read(&v, sizeof(T)); }
    size_t remaining() const { return static_cast<size_t>(end_ - p_); }
private:
    const uint8_t* p_;
    const uint8_t* end_;
};

// ---- versioning / upgrade registry ----
using UpgradeFn = void (*)(void* obj, uint32_t from_version);

class UpgradeRegistry {
public:
    static UpgradeRegistry& instance() { static UpgradeRegistry u; return u; }
    void register_upgrade(TypeId id, UpgradeFn fn) { map_[id] = fn; }
    UpgradeFn find(TypeId id) const {
        auto it = map_.find(id);
        return it != map_.end() ? it->second : nullptr;
    }
private:
    std::unordered_map<TypeId, UpgradeFn> map_;
};

// ---- reflection-driven write/read (abstract sink/source) ----
//
// Layout per object: [type_id u64][version u32][field_count u32]
//   then per field:   [field_id u64][byte_len u32][bytes...]
// Field bytes: nested reflected -> recursively written object; otherwise the
// field's type-erased serializer (POD raw, string, vector, ...).

inline void write(IByteSink& s, const void* obj, const TypeInfo& ti) {
    const uint64_t id    = ti.id;
    const uint32_t ver   = ti.version;
    const uint32_t count = static_cast<uint32_t>(ti.fields.size());
    s.write(&id, 8); s.write(&ver, 4); s.write(&count, 4);

    for (const FieldInfo& f : ti.fields) {
        const uint64_t fid = field_id(f);
        s.write(&fid, 8);
        const void* fp = static_cast<const char*>(obj) + f.offset;

        // Serialize the field into a temp buffer to get its byte length
        // (length-prefix enables skip-on-unknown-field for forward compat).
        std::vector<uint8_t> sub;
        BinaryWriter sw(sub);
        if (const TypeInfo* fti = Registry::instance().find(f.type_id)) {
            write(sw, fp, *fti);                 // nested reflected
        } else if (f.serializer.write) {
            f.serializer.write(sw, fp);          // POD / string / vector / ...
        } else {
            sw.write(fp, f.size);                // last-resort raw blob
        }
        const uint32_t len = static_cast<uint32_t>(sub.size());
        s.write(&len, 4);
        if (len) s.write(sub.data(), len);
    }
}

inline bool read(IByteSource& s, void* obj, const TypeInfo& ti) {
    uint64_t stored_id;
    uint32_t stored_version, field_count;
    if (!s.read(&stored_id, 8) || !s.read(&stored_version, 4) ||
        !s.read(&field_count, 4))
        return false;

    for (uint32_t i = 0; i < field_count; ++i) {
        uint64_t fid;
        uint32_t flen;
        if (!s.read(&fid, 8) || !s.read(&flen, 4)) return false;

        const FieldInfo* f = find_field_by_id(ti, fid);
        if (!f) { if (!s.skip(flen)) return false; continue; }  // removed field

        std::vector<uint8_t> sub(flen);
        if (flen && !s.read(sub.data(), flen)) return false;
        BinaryReader sr(sub.data(), flen);
        void* fp = static_cast<char*>(obj) + f->offset;

        if (const TypeInfo* fti = Registry::instance().find(f->type_id)) {
            read(sr, fp, *fti);                  // nested reflected
        } else if (f->serializer.read) {
            f->serializer.read(sr, fp);          // POD / string / vector / ...
        } else if (flen == f->size) {
            std::memcpy(fp, sub.data(), flen);   // raw blob, same size
        }                                        // else: size changed -> keep default
    }

    if (stored_version != ti.version) {
        if (UpgradeFn fn = UpgradeRegistry::instance().find(ti.id))
            fn(obj, stored_version);
    }
    return true;
}

// ---- typed convenience ----
template <typename T>
std::vector<uint8_t> save(const T& obj) {
    std::vector<uint8_t> buf;
    BinaryWriter w(buf);
    write(w, &obj, *reflect::reflect<T>());
    return buf;
}
template <typename T>
bool load(const void* data, size_t size, T& out) {
    BinaryReader r(data, size);
    return read(r, &out, *reflect::reflect<T>());
}
template <typename T>
bool load(const std::vector<uint8_t>& blob, T& out) {
    return load(blob.data(), blob.size(), out);
}

}  // namespace gws::serialize
