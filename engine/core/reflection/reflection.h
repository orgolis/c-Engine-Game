#pragma once

// ====================
// Core Reflection — RTTI++  (Pillar A3 / Master Plan Stage 0.4)
// ====================
//
// Enough runtime metadata to walk any registered type generically: read/
// write a field by offset, enumerate fields for an inspector, serialize by
// iterating fields, and diff fields for network deltas. The single source
// of truth for editor UI (K), serialization (A4), ECS (D) and replication (H).
//
// Design: offset-based (no std::any / std::function per field), keyed by a
// stable compile-time TypeId (FNV-1a of the ctti type name). Types register
// lazily on first reflect<T>() — no static-init-order hazards.
//
//   struct Transform { glm::vec3 position; glm::quat rotation; float scale; };
//   GWS_REFLECT_BEGIN(Transform)
//       GWS_REFLECT_FIELD(position)
//       GWS_REFLECT_FIELD(rotation)
//       GWS_REFLECT_FIELD(scale)
//   GWS_REFLECT_END()
//
//   const TypeInfo* ti = gws::reflect::reflect<Transform>();
//   gws::reflect::for_each_field(&t, *ti, [](const FieldInfo& f, void* p){ ... });
//
// Components must be standard-layout (POD) for offsetof — which the ECS
// plan requires anyway.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace gws::reflect {

using TypeId = uint64_t;

// ---- compile-time type name + stable id (ctti) ----
namespace detail {
template <typename T>
constexpr std::string_view raw_name() {
#if defined(__clang__) || defined(__GNUC__)
    return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
    return __FUNCSIG__;
#else
    return "unsupported_ctti";
#endif
}
}  // namespace detail

// Extracts just the `T` portion from the compiler's decorated function name.
template <typename T>
constexpr std::string_view type_name() {
    constexpr std::string_view p = detail::raw_name<T>();
#if defined(__clang__)
    constexpr std::string_view pre = "[T = ";
    constexpr char close = ']';
#elif defined(__GNUC__)
    constexpr std::string_view pre = "with T = ";
    constexpr char close = ';';  // g++ appends "; std::string_view = ..."
#elif defined(_MSC_VER)
    constexpr std::string_view pre = "raw_name<";
    constexpr char close = '>';
#else
    constexpr std::string_view pre = "";
    constexpr char close = '\0';
#endif
    const size_t s = p.find(pre);
    if (s == std::string_view::npos) return p;
    const size_t start = s + pre.size();
    size_t end = p.find(close, start);
    const size_t bracket = p.find(']', start);  // also stop at the outer ']'
    if (bracket != std::string_view::npos && (end == std::string_view::npos || bracket < end))
        end = bracket;
    if (end == std::string_view::npos) end = p.size();
    // Trim a trailing space if present.
    while (end > start && p[end - 1] == ' ') --end;
    return p.substr(start, end - start);
}

constexpr TypeId fnv1a(std::string_view s) {
    uint64_t h = 1469598103934665603ull;
    for (char c : s) {
        h ^= static_cast<uint8_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

template <typename T>
constexpr TypeId type_id_of() { return fnv1a(type_name<T>()); }

// ---- metadata ----
struct FieldAttributes {
    bool  networked     = true;    // replicated unless NotNetworked clears it
    bool  editor_hidden = false;
    bool  has_range     = false;
    float range_min     = 0.0f;
    float range_max     = 0.0f;
};

// ---- generic byte stream (decouples reflection from any concrete sink) ----
// A serialization back-end (binary/text) implements these; reflection only
// knows the abstract interface, so it stays independent of serialization.
struct IByteSink {
    virtual ~IByteSink() = default;
    virtual void write(const void* data, size_t n) = 0;
};
struct IByteSource {
    virtual ~IByteSource() = default;
    virtual bool read(void* out, size_t n) = 0;
    virtual bool skip(size_t n)            = 0;
};

// Per-field type-erased stream functions, captured from the field's static
// C++ type by the GWS_REFLECT_FIELD macro.
using FieldWriteFn = void (*)(IByteSink&, const void* field);
using FieldReadFn  = bool (*)(IByteSource&, void* field);
struct FieldSerializer {
    FieldWriteFn write = nullptr;
    FieldReadFn  read  = nullptr;
};

template <typename T>
inline void pod_write(IByteSink& s, const void* fp) { s.write(fp, sizeof(T)); }
template <typename T>
inline bool pod_read(IByteSource& s, void* fp) { return s.read(fp, sizeof(T)); }

// Customization point. Default: trivially-copyable types stream as raw POD.
// Non-trivial types (std::string, std::vector, ...) get specializations in
// the serialization traits header; reflected nested structs are handled by
// recursion in the serializer (so they leave this null).
template <typename T>
struct FieldSerializerFor {
    static FieldSerializer get() {
        if constexpr (std::is_trivially_copyable_v<T>)
            return FieldSerializer{ &pod_write<T>, &pod_read<T> };
        else
            return FieldSerializer{ nullptr, nullptr };
    }
};

struct FieldInfo {
    const char*     name       = "";
    TypeId          type_id    = 0;   // id of the field's own type
    size_t          offset     = 0;
    size_t          size       = 0;
    FieldAttributes attr{};
    FieldSerializer serializer{};     // type-erased stream fns (or null)
};

struct TypeInfo {
    const char*            name    = "";
    TypeId                 id      = 0;
    size_t                 size    = 0;
    size_t                 align   = 0;
    uint32_t               version = 1;  // for serialization upgrades (0.5)
    std::vector<FieldInfo> fields;
};

// ---- registry ----
class Registry {
public:
    static Registry& instance() { static Registry r; return r; }

    // Idempotent: re-registering an existing id returns the stored TypeInfo.
    const TypeInfo* register_type(TypeInfo&& info) {
        auto it = by_id_.find(info.id);
        if (it != by_id_.end()) return it->second.get();
        const TypeId id = info.id;
        std::string  nm(info.name);
        auto owned     = std::make_unique<TypeInfo>(std::move(info));
        const TypeInfo* raw = owned.get();
        by_id_.emplace(id, std::move(owned));
        by_name_.emplace(std::move(nm), raw);
        return raw;
    }
    const TypeInfo* find(TypeId id) const {
        auto it = by_id_.find(id);
        return it != by_id_.end() ? it->second.get() : nullptr;
    }
    const TypeInfo* find(std::string_view name) const {
        auto it = by_name_.find(std::string(name));
        return it != by_name_.end() ? it->second : nullptr;
    }
    template <typename Fn>
    void for_each_type(Fn&& fn) const { for (auto& kv : by_id_) fn(*kv.second); }
    size_t type_count() const { return by_id_.size(); }

private:
    Registry() = default;
    std::unordered_map<TypeId, std::unique_ptr<TypeInfo>> by_id_;
    std::unordered_map<std::string, const TypeInfo*>      by_name_;
};

// ---- builder (specialized by the GWS_REFLECT_* macros) ----
template <typename T> struct TypeBuilder;  // primary intentionally undefined

// Lazily builds + registers T's TypeInfo exactly once (thread-safe static
// init), returning the singleton pointer. Call once per type at startup to
// make it findable by name (e.g. for deserialization).
template <typename T>
const TypeInfo* reflect() {
    static const TypeInfo* info = TypeBuilder<T>::build();
    return info;
}

// ---- generic field visiting ----
template <typename Visitor>
void for_each_field(void* obj, const TypeInfo& ti, Visitor&& v) {
    for (const FieldInfo& f : ti.fields)
        v(f, static_cast<void*>(static_cast<char*>(obj) + f.offset));
}
template <typename Visitor>
void for_each_field(const void* obj, const TypeInfo& ti, Visitor&& v) {
    for (const FieldInfo& f : ti.fields)
        v(f, static_cast<const void*>(static_cast<const char*>(obj) + f.offset));
}

template <typename T>
T& field_ref(void* obj, const FieldInfo& f) {
    return *reinterpret_cast<T*>(static_cast<char*>(obj) + f.offset);
}
template <typename T>
const T& field_ref(const void* obj, const FieldInfo& f) {
    return *reinterpret_cast<const T*>(static_cast<const char*>(obj) + f.offset);
}

}  // namespace gws::reflect

// ============================================================
// Registration macros — use at NAMESPACE/global scope after the type.
// ============================================================

#define GWS_REFLECT_BEGIN(T)                                                   \
    namespace gws { namespace reflect {                                        \
    template <> struct TypeBuilder<T> {                                        \
        using Self = T;                                                        \
        static const TypeInfo* build() {                                       \
            TypeInfo info;                                                     \
            info.name    = #T;                                                 \
            info.id      = type_id_of<T>();                                    \
            info.size    = sizeof(T);                                          \
            info.align   = alignof(T);                                         \
            info.version = 1;

#define GWS_REFLECT_VERSION(v) info.version = (v);

#define GWS_REFLECT_FIELD(FIELD)                                               \
            {                                                                  \
                FieldInfo f;                                                   \
                f.name       = #FIELD;                                         \
                f.type_id    = type_id_of<decltype(Self::FIELD)>();            \
                f.offset     = offsetof(Self, FIELD);                         \
                f.size       = sizeof(decltype(Self::FIELD));                  \
                f.serializer = FieldSerializerFor<decltype(Self::FIELD)>::get();\
                info.fields.push_back(f);                                      \
            }

// Like GWS_REFLECT_FIELD but lets you set FieldAttributes inline, e.g.:
//   GWS_REFLECT_FIELD_ATTR(health, f.attr.has_range = true; f.attr.range_max = 100;)
#define GWS_REFLECT_FIELD_ATTR(FIELD, ATTR_STMTS)                              \
            {                                                                  \
                FieldInfo f;                                                   \
                f.name       = #FIELD;                                         \
                f.type_id    = type_id_of<decltype(Self::FIELD)>();            \
                f.offset     = offsetof(Self, FIELD);                         \
                f.size       = sizeof(decltype(Self::FIELD));                  \
                f.serializer = FieldSerializerFor<decltype(Self::FIELD)>::get();\
                ATTR_STMTS                                                     \
                info.fields.push_back(f);                                      \
            }

#define GWS_REFLECT_END()                                                      \
            return Registry::instance().register_type(std::move(info));        \
        }                                                                      \
    };                                                                         \
    }} /* namespace reflect, gws */
