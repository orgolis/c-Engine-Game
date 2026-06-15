#pragma once

// Container field serializers for the reflection/serialization system.
// Include this BEFORE the GWS_REFLECT_ block of any type that has
// std::string / std::vector / std::array fields, so the macro captures the
// right serializer (otherwise a non-trivial field gets a null serializer
// and is skipped).

#include "serialization/serialization.h"

#include <array>
#include <string>
#include <vector>

namespace gws::serialize {

// Stream one value: a reflected struct walks its fields; anything else uses
// its FieldSerializer (POD raw, string, nested vector, ...). This is the
// recursion point that makes vector<ReflectedStruct> work.
template <typename E>
inline void write_value(reflect::IByteSink& s, const E& e) {
    if (const reflect::TypeInfo* ti =
            reflect::Registry::instance().find(reflect::type_id_of<E>())) {
        write(s, &e, *ti);
    } else {
        reflect::FieldSerializer fs = reflect::FieldSerializerFor<E>::get();
        if (fs.write) fs.write(s, &e);
    }
}
template <typename E>
inline bool read_value(reflect::IByteSource& s, E& e) {
    if (const reflect::TypeInfo* ti =
            reflect::Registry::instance().find(reflect::type_id_of<E>())) {
        return read(s, &e, *ti);
    }
    reflect::FieldSerializer fs = reflect::FieldSerializerFor<E>::get();
    return fs.read ? fs.read(s, &e) : false;
}

}  // namespace gws::serialize

namespace gws::reflect {

// std::string -> [u32 length][chars]
template <>
struct FieldSerializerFor<std::string> {
    static FieldSerializer get() {
        return FieldSerializer{
            [](IByteSink& s, const void* fp) {
                const auto& str = *static_cast<const std::string*>(fp);
                uint32_t n = static_cast<uint32_t>(str.size());
                s.write(&n, 4);
                if (n) s.write(str.data(), n);
            },
            [](IByteSource& s, void* fp) -> bool {
                auto& str = *static_cast<std::string*>(fp);
                uint32_t n;
                if (!s.read(&n, 4)) return false;
                str.resize(n);
                return n == 0 || s.read(&str[0], n);
            }};
    }
};

// std::vector<E> -> [u32 count][E...]
template <typename E>
struct FieldSerializerFor<std::vector<E>> {
    static FieldSerializer get() {
        return FieldSerializer{
            [](IByteSink& s, const void* fp) {
                const auto& v = *static_cast<const std::vector<E>*>(fp);
                uint32_t n = static_cast<uint32_t>(v.size());
                s.write(&n, 4);
                for (const E& e : v) gws::serialize::write_value<E>(s, e);
            },
            [](IByteSource& s, void* fp) -> bool {
                auto& v = *static_cast<std::vector<E>*>(fp);
                uint32_t n;
                if (!s.read(&n, 4)) return false;
                v.resize(n);
                for (uint32_t i = 0; i < n; ++i)
                    if (!gws::serialize::read_value<E>(s, v[i])) return false;
                return true;
            }};
    }
};

// std::array<E, N> -> [E x N] (fixed count, no length prefix)
template <typename E, size_t N>
struct FieldSerializerFor<std::array<E, N>> {
    static FieldSerializer get() {
        return FieldSerializer{
            [](IByteSink& s, const void* fp) {
                const auto& a = *static_cast<const std::array<E, N>*>(fp);
                for (const E& e : a) gws::serialize::write_value<E>(s, e);
            },
            [](IByteSource& s, void* fp) -> bool {
                auto& a = *static_cast<std::array<E, N>*>(fp);
                for (size_t i = 0; i < N; ++i)
                    if (!gws::serialize::read_value<E>(s, a[i])) return false;
                return true;
            }};
    }
};

}  // namespace gws::reflect
