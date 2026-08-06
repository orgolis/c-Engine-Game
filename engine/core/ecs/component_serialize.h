#pragma once

// ============================================================================
// Generic component (de)serialization — reflection-driven, no per-component code.
// Shared by gameplay-component save/load (F3) and prefabs (F4). POD components
// stream field-by-field via the reflection FieldSerializer (explicit per field,
// not a raw memcpy of the whole struct), then hex-encode for text storage.
// ============================================================================

#include "reflection/reflection.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace schizo::ecs {

namespace detail {
struct VecSink : gws::reflect::IByteSink {
    std::vector<uint8_t>* b = nullptr;
    void write(const void* d, size_t n) override {
        const uint8_t* p = static_cast<const uint8_t*>(d);
        b->insert(b->end(), p, p + n);
    }
};
struct SpanSource : gws::reflect::IByteSource {
    const uint8_t* p = nullptr;
    const uint8_t* e = nullptr;
    bool read(void* out, size_t n) override {
        if (p + n > e) return false;
        std::memcpy(out, p, n); p += n; return true;
    }
    bool skip(size_t n) override { if (p + n > e) return false; p += n; return true; }
};
}  // namespace detail

// Serialize a component instance's reflected fields to a byte buffer.
inline std::vector<uint8_t> serialize_component(const void* obj, const gws::reflect::TypeInfo& ti) {
    std::vector<uint8_t> out;
    detail::VecSink sink; sink.b = &out;
    for (const auto& f : ti.fields)
        if (f.serializer.write) f.serializer.write(sink, static_cast<const char*>(obj) + f.offset);
    return out;
}

// Overwrite a component instance's reflected fields from bytes.
inline void deserialize_component(void* obj, const gws::reflect::TypeInfo& ti,
                                  const uint8_t* data, size_t n) {
    detail::SpanSource src; src.p = data; src.e = data + n;
    for (const auto& f : ti.fields)
        if (f.serializer.read) { if (!f.serializer.read(src, static_cast<char*>(obj) + f.offset)) return; }
}

inline std::string to_hex(const std::vector<uint8_t>& b) {
    static const char* H = "0123456789abcdef";
    std::string s; s.reserve(b.size() * 2);
    for (uint8_t c : b) { s += H[c >> 4]; s += H[c & 15]; }
    return s;
}

inline std::vector<uint8_t> from_hex(const std::string& s) {
    auto v = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    std::vector<uint8_t> b; b.reserve(s.size() / 2);
    for (size_t i = 0; i + 1 < s.size(); i += 2)
        b.push_back(static_cast<uint8_t>((v(s[i]) << 4) | v(s[i + 1])));
    return b;
}

}  // namespace schizo::ecs
