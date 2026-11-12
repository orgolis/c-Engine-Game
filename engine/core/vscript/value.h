#pragma once

// ============================================================================
// vscript/value.h -- the typed values that flow along a logic graph's wires.
// ============================================================================
//
// A Type says what a pin carries; a Value is one such thing at run time.
//
// A Value is 32 bytes: the type, 16 bytes of inline payload (bool, integer,
// number, up to four floats, an entity id) and one pointer to a heap "box" for
// what does not fit inline (strings, arrays, transforms, structs). Boxes are
// reference counted without atomics, because a VM and everything it touches
// belong to one thread.
//
// Reference rules, chosen to match what a Blueprint author expects:
//   - copying a Value shares its box. Reading an array variable and calling
//     "Add" on the result therefore changes the variable.
//   - writing a variable copies deeply (clone()), so two variables never alias.
//   - a StringBox is never modified after it is made, so sharing one is safe.

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace schizo::vs {

enum class Kind : uint8_t {
    None = 0,   // no value: an unresolved pin, or "nothing" at run time
    Exec,       // a flow pin. Never a run-time value.
    Bool,
    Int,        // 64-bit signed
    Float,      // 64-bit
    String,
    Vec2,
    Vec3,
    Vec4,
    Quat,
    Color,      // linear RGBA, stored like a Vec4
    Transform,  // position, rotation, scale (boxed)
    Entity,     // scene entity id, 0 = none
    Asset,      // asset path; Type::sub names the asset kind
    Enum,       // integer value; Type::sub names the enum
    Struct,     // boxed fields; Type::sub names the struct
    Any,        // wildcard, in node definitions only: resolved per node instance
};

struct Type {
    Kind     kind  = Kind::None;
    bool     array = false;
    uint16_t sub   = 0;   // enum / struct / asset-kind id, 0 when not applicable

    constexpr Type() = default;
    constexpr Type(Kind k, bool is_array = false, uint16_t s = 0) : kind(k), array(is_array), sub(s) {}

    friend constexpr bool operator==(Type a, Type b) {
        return a.kind == b.kind && a.array == b.array && a.sub == b.sub;
    }
    friend constexpr bool operator!=(Type a, Type b) { return !(a == b); }

    constexpr Type element() const { return Type(kind, false, sub); }
    constexpr Type as_array() const { return Type(kind, true, sub); }
    constexpr bool is_exec() const { return kind == Kind::Exec; }
    constexpr bool is_none() const { return kind == Kind::None; }
    constexpr bool is_any() const { return kind == Kind::Any; }
};

inline constexpr Type kExec{Kind::Exec};
inline constexpr Type kBool{Kind::Bool};
inline constexpr Type kInt{Kind::Int};
inline constexpr Type kFloat{Kind::Float};
inline constexpr Type kString{Kind::String};
inline constexpr Type kVec2{Kind::Vec2};
inline constexpr Type kVec3{Kind::Vec3};
inline constexpr Type kVec4{Kind::Vec4};
inline constexpr Type kQuat{Kind::Quat};
inline constexpr Type kColor{Kind::Color};
inline constexpr Type kTransform{Kind::Transform};
inline constexpr Type kEntity{Kind::Entity};
inline constexpr Type kAny{Kind::Any};

/// "float", "vec3[]", "entity" ... the spelling used in files and messages.
std::string type_name(Type t);
/// Inverse of type_name. Returns false and leaves `out` alone on an unknown name.
bool parse_type(std::string_view text, Type& out);

// ---------------------------------------------------------------------------
// Boxes: the heap part of a value.
// ---------------------------------------------------------------------------
struct Box {
    uint32_t refs = 0;
    virtual ~Box() = default;
};

template <class T>
class Ref {
public:
    Ref() = default;
    explicit Ref(T* p) : p_(p) { if (p_) ++p_->refs; }
    Ref(const Ref& o) : p_(o.p_) { if (p_) ++p_->refs; }
    Ref(Ref&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    Ref& operator=(Ref o) noexcept { std::swap(p_, o.p_); return *this; }
    ~Ref() { if (p_ && --p_->refs == 0) delete p_; }

    T*   get() const { return p_; }
    T*   operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    void reset() { *this = Ref(); }

private:
    T* p_ = nullptr;
};

struct TransformData {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

class Value;

struct StringBox final : Box {
    std::string s;
    explicit StringBox(std::string v) : s(std::move(v)) {}
};
struct ArrayBox final : Box {
    std::vector<Value> items;
};
struct TransformBox final : Box {
    TransformData t;
};
struct StructBox final : Box {
    std::vector<Value> fields;
};

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------
class Value {
public:
    Type type;
    union Payload {
        int64_t  i;       // Bool (0/1), Int, Enum
        double   f;       // Float
        float    v[4];    // Vec2/3/4, Quat (x,y,z,w), Color
        uint32_t entity;  // Entity
    } u;
    Ref<Box> box;

    Value() { std::memset(&u, 0, sizeof u); }

    // ---- constructors -------------------------------------------------
    static Value boolean(bool b) { Value v; v.type = kBool; v.u.i = b ? 1 : 0; return v; }
    static Value integer(int64_t x) { Value v; v.type = kInt; v.u.i = x; return v; }
    static Value number(double x) { Value v; v.type = kFloat; v.u.f = x; return v; }
    static Value string(std::string s);
    static Value vec2(const glm::vec2& a) { Value v; v.type = kVec2; v.u.v[0] = a.x; v.u.v[1] = a.y; return v; }
    static Value vec3(const glm::vec3& a) {
        Value v; v.type = kVec3; v.u.v[0] = a.x; v.u.v[1] = a.y; v.u.v[2] = a.z; return v;
    }
    static Value vec4(const glm::vec4& a) {
        Value v; v.type = kVec4; v.u.v[0] = a.x; v.u.v[1] = a.y; v.u.v[2] = a.z; v.u.v[3] = a.w; return v;
    }
    static Value quat(const glm::quat& q) {
        Value v; v.type = kQuat; v.u.v[0] = q.x; v.u.v[1] = q.y; v.u.v[2] = q.z; v.u.v[3] = q.w; return v;
    }
    static Value color(const glm::vec4& c) { Value v = vec4(c); v.type = kColor; return v; }
    static Value transform(const TransformData& t);
    static Value entity(uint32_t id) { Value v; v.type = kEntity; v.u.entity = id; return v; }
    static Value asset(uint16_t sub, std::string path);
    static Value enumeration(uint16_t sub, int64_t x) {
        Value v; v.type = Type(Kind::Enum, false, sub); v.u.i = x; return v;
    }
    /// An array of `element` holding `items` (each item must already be of that type).
    static Value array(Type element, std::vector<Value> items = {});
    /// The value a pin or variable of type `t` holds before anything is written
    /// to it: false, 0, "", zero vectors, the identity rotation and transform,
    /// no entity, an empty array.
    static Value default_of(Type t);

    // ---- readers (no conversion: the compiler has made the type right) ----
    bool    as_bool() const { return u.i != 0; }
    int64_t as_int() const { return u.i; }
    double  as_float() const { return u.f; }
    /// Text of a String or Asset value; empty for anything else.
    const std::string& str() const;
    glm::vec2 as_vec2() const { return {u.v[0], u.v[1]}; }
    glm::vec3 as_vec3() const { return {u.v[0], u.v[1], u.v[2]}; }
    glm::vec4 as_vec4() const { return {u.v[0], u.v[1], u.v[2], u.v[3]}; }
    glm::quat as_quat() const { return glm::quat(u.v[3], u.v[0], u.v[1], u.v[2]); }
    uint32_t  as_entity() const { return u.entity; }
    TransformData as_transform() const;

    /// The items of an array value. An array Value always has a box; calling
    /// this on a non-array returns a shared empty vector.
    std::vector<Value>&       items();
    const std::vector<Value>& items() const;
    bool is_array() const { return type.array; }
    /// The fields of a struct value, in the order its definition lists them.
    /// A fresh struct has none yet: a field that is not there reads as its
    /// type's default. On a non-struct this is a shared empty vector.
    std::vector<Value>&       fields();
    const std::vector<Value>& fields() const;
};

static_assert(sizeof(Value) == 32, "a Value is 32 bytes: type, 16 bytes inline, one box pointer");

/// A copy that shares nothing mutable with the original: arrays, structs and
/// transforms are duplicated, recursively. Used when a variable is written.
Value clone(const Value& v);

/// Deep equality. Values of different types are unequal, except that Int and
/// Float compare numerically.
bool equals(const Value& a, const Value& b);

/// Convert `v` to `target`. Supported: identity; Bool/Int/Float among each
/// other; Enum to Int; anything to String (display text). Returns false when
/// there is no such conversion, and `out` is then default_of(target).
bool convert(const Value& v, Type target, Value& out);
/// Whether convert() can succeed for these types, and whether it loses
/// information (Float to Int) so that the compiler can warn.
bool can_convert(Type from, Type to, bool* lossy = nullptr);

/// What "Print" shows: true, 42, 1.5, text, (1, 2, 3), [a, b].
std::string display_string(const Value& v);

/// File form of a value, and its inverse. For a String the literal is the text
/// itself; escaping for the file format is the caller's business.
std::string literal_text(const Value& v);
bool        parse_literal(Type type, std::string_view text, Value& out);

/// Shortest text that reads back as exactly this number; always has a '.',
/// an exponent, or is inf/nan, so it cannot be mistaken for an integer.
std::string number_text(double x);

}  // namespace schizo::vs
