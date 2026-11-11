#include "vscript/value.h"

#include <charconv>
#include <cstdint>
#include <cmath>
#include <cstdlib>

namespace schizo::vs {

namespace {

struct KindName { Kind kind; const char* name; };
constexpr KindName kKindNames[] = {
    {Kind::None, "none"},     {Kind::Exec, "exec"},   {Kind::Bool, "bool"},
    {Kind::Int, "int"},       {Kind::Float, "float"}, {Kind::String, "string"},
    {Kind::Vec2, "vec2"},     {Kind::Vec3, "vec3"},   {Kind::Vec4, "vec4"},
    {Kind::Quat, "quat"},     {Kind::Color, "color"}, {Kind::Transform, "transform"},
    {Kind::Entity, "entity"}, {Kind::Asset, "asset"}, {Kind::Enum, "enum"},
    {Kind::Struct, "struct"}, {Kind::Any, "any"},
};

int component_count(Kind k) {
    switch (k) {
        case Kind::Vec2: return 2;
        case Kind::Vec3: return 3;
        case Kind::Vec4:
        case Kind::Quat:
        case Kind::Color: return 4;
        default: return 0;
    }
}

std::string float_text(float x) {
    char buf[48];
    const auto r = std::to_chars(buf, buf + sizeof buf, x);
    std::string s(buf, r.ptr);
    if (s.find_first_of(".eEni") == std::string::npos) s += ".0";
    return s;
}

bool parse_double(std::string_view t, double& out) {
    while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.remove_prefix(1);
    while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.remove_suffix(1);
    if (t.empty()) return false;
    if (t.front() == '+') t.remove_prefix(1);
    const auto r = std::from_chars(t.data(), t.data() + t.size(), out);
    return r.ec == std::errc() && r.ptr == t.data() + t.size();
}

bool parse_int(std::string_view t, int64_t& out) {
    while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.remove_prefix(1);
    while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.remove_suffix(1);
    if (t.empty()) return false;
    if (t.front() == '+') t.remove_prefix(1);
    const auto r = std::from_chars(t.data(), t.data() + t.size(), out);
    return r.ec == std::errc() && r.ptr == t.data() + t.size();
}

/// Split on `sep`, keeping empty fields.
std::vector<std::string_view> split(std::string_view t, char sep) {
    std::vector<std::string_view> out;
    size_t start = 0;
    while (true) {
        const size_t p = t.find(sep, start);
        if (p == std::string_view::npos) {
            out.push_back(t.substr(start));
            break;
        }
        out.push_back(t.substr(start, p - start));
        start = p + 1;
    }
    return out;
}

bool parse_floats(std::string_view t, float* dst, int n) {
    const auto parts = split(t, ',');
    if (static_cast<int>(parts.size()) != n) return false;
    for (int i = 0; i < n; ++i) {
        double d = 0.0;
        if (!parse_double(parts[i], d)) return false;
        dst[i] = static_cast<float>(d);
    }
    return true;
}

std::string join_floats(const float* v, int n) {
    std::string s;
    for (int i = 0; i < n; ++i) {
        if (i) s += ',';
        s += float_text(v[i]);
    }
    return s;
}

const std::string kEmptyString;
std::vector<Value>& empty_items() {
    static std::vector<Value> e;
    e.clear();   // a caller that wrote into the shared vector must not leak into the next
    return e;
}

}  // namespace

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------
std::string type_name(Type t) {
    std::string s = "none";
    for (const auto& kn : kKindNames)
        if (kn.kind == t.kind) { s = kn.name; break; }
    if (t.sub != 0) s += "#" + std::to_string(t.sub);
    if (t.array) s += "[]";
    return s;
}

bool parse_type(std::string_view text, Type& out) {
    Type t;
    if (text.size() >= 2 && text.substr(text.size() - 2) == "[]") {
        t.array = true;
        text.remove_suffix(2);
    }
    const size_t hash = text.find('#');
    if (hash != std::string_view::npos) {
        int64_t sub = 0;
        if (!parse_int(text.substr(hash + 1), sub) || sub < 0 || sub > 65535) return false;
        t.sub = static_cast<uint16_t>(sub);
        text = text.substr(0, hash);
    }
    for (const auto& kn : kKindNames) {
        if (text == kn.name) {
            t.kind = kn.kind;
            out = t;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Constructors and readers
// ---------------------------------------------------------------------------
Value Value::string(std::string s) {
    Value v;
    v.type = kString;
    v.box = Ref<Box>(new StringBox(std::move(s)));
    return v;
}

Value Value::transform(const TransformData& t) {
    Value v;
    v.type = kTransform;
    auto* b = new TransformBox();
    b->t = t;
    v.box = Ref<Box>(b);
    return v;
}

Value Value::asset(uint16_t sub, std::string path) {
    Value v;
    v.type = Type(Kind::Asset, false, sub);
    v.box = Ref<Box>(new StringBox(std::move(path)));
    return v;
}

Value Value::array(Type element, std::vector<Value> items) {
    Value v;
    v.type = element.as_array();
    auto* b = new ArrayBox();
    b->items = std::move(items);
    v.box = Ref<Box>(b);
    return v;
}

Value Value::default_of(Type t) {
    if (t.array) return array(t.element());
    switch (t.kind) {
        case Kind::Bool: return boolean(false);
        case Kind::Int: return integer(0);
        case Kind::Float: return number(0.0);
        case Kind::String: return string(std::string());
        case Kind::Vec2: return vec2(glm::vec2(0.0f));
        case Kind::Vec3: return vec3(glm::vec3(0.0f));
        case Kind::Vec4: return vec4(glm::vec4(0.0f));
        case Kind::Quat: return quat(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        case Kind::Color: return color(glm::vec4(1.0f));
        case Kind::Transform: return transform(TransformData{});
        case Kind::Entity: return entity(0);
        case Kind::Asset: return asset(t.sub, std::string());
        case Kind::Enum: return enumeration(t.sub, 0);
        case Kind::Struct: {
            Value v;
            v.type = t;
            v.box = Ref<Box>(new StructBox());
            return v;
        }
        default: {
            Value v;
            v.type = t;
            return v;
        }
    }
}

const std::string& Value::str() const {
    if (!type.array && (type.kind == Kind::String || type.kind == Kind::Asset) && box)
        return static_cast<const StringBox*>(box.get())->s;
    return kEmptyString;
}

TransformData Value::as_transform() const {
    if (!type.array && type.kind == Kind::Transform && box)
        return static_cast<const TransformBox*>(box.get())->t;
    return TransformData{};
}

std::vector<Value>& Value::items() {
    if (type.array && box) return static_cast<ArrayBox*>(box.get())->items;
    return empty_items();
}

const std::vector<Value>& Value::items() const {
    if (type.array && box) return static_cast<const ArrayBox*>(box.get())->items;
    return empty_items();
}

// ---------------------------------------------------------------------------
// clone / equals / convert
// ---------------------------------------------------------------------------
Value clone(const Value& v) {
    if (v.type.array) {
        std::vector<Value> items;
        items.reserve(v.items().size());
        for (const Value& it : v.items()) items.push_back(clone(it));
        return Value::array(v.type.element(), std::move(items));
    }
    if (v.type.kind == Kind::Transform) return Value::transform(v.as_transform());
    if (v.type.kind == Kind::Struct) {
        Value out;
        out.type = v.type;
        auto* b = new StructBox();
        if (v.box)
            for (const Value& f : static_cast<const StructBox*>(v.box.get())->fields) b->fields.push_back(clone(f));
        out.box = Ref<Box>(b);
        return out;
    }
    return v;   // inline payload, or an immutable string box
}

bool equals(const Value& a, const Value& b) {
    if (a.type != b.type) {
        // Int and Float compare as numbers; nothing else crosses types.
        if (!a.type.array && !b.type.array) {
            const bool an = a.type.kind == Kind::Int || a.type.kind == Kind::Float;
            const bool bn = b.type.kind == Kind::Int || b.type.kind == Kind::Float;
            if (an && bn) {
                const double x = a.type.kind == Kind::Int ? static_cast<double>(a.u.i) : a.u.f;
                const double y = b.type.kind == Kind::Int ? static_cast<double>(b.u.i) : b.u.f;
                return x == y;
            }
        }
        return false;
    }
    if (a.type.array) {
        const auto& x = a.items();
        const auto& y = b.items();
        if (x.size() != y.size()) return false;
        for (size_t i = 0; i < x.size(); ++i)
            if (!equals(x[i], y[i])) return false;
        return true;
    }
    switch (a.type.kind) {
        case Kind::None: return true;
        case Kind::Bool: return (a.u.i != 0) == (b.u.i != 0);
        case Kind::Int:
        case Kind::Enum: return a.u.i == b.u.i;
        case Kind::Float: return a.u.f == b.u.f;
        case Kind::String:
        case Kind::Asset: return a.str() == b.str();
        case Kind::Vec2:
        case Kind::Vec3:
        case Kind::Vec4:
        case Kind::Quat:
        case Kind::Color: {
            const int n = component_count(a.type.kind);
            for (int i = 0; i < n; ++i)
                if (a.u.v[i] != b.u.v[i]) return false;
            return true;
        }
        case Kind::Entity: return a.u.entity == b.u.entity;
        case Kind::Transform: {
            const TransformData x = a.as_transform(), y = b.as_transform();
            return x.position == y.position && x.rotation == y.rotation && x.scale == y.scale;
        }
        case Kind::Struct: {
            const auto* x = static_cast<const StructBox*>(a.box.get());
            const auto* y = static_cast<const StructBox*>(b.box.get());
            if (!x || !y) return x == y;
            if (x->fields.size() != y->fields.size()) return false;
            for (size_t i = 0; i < x->fields.size(); ++i)
                if (!equals(x->fields[i], y->fields[i])) return false;
            return true;
        }
        default: return false;
    }
}

bool can_convert(Type from, Type to, bool* lossy) {
    if (lossy) *lossy = false;
    if (from == to) return true;
    if (from.is_exec() || to.is_exec() || from.is_none() || to.is_none()) return false;
    if (to == kString) return true;                 // anything can be shown as text
    if (from.array || to.array) return false;       // arrays convert to nothing but text
    switch (from.kind) {
        case Kind::Bool: return to.kind == Kind::Int || to.kind == Kind::Float;
        case Kind::Int: return to.kind == Kind::Float;
        case Kind::Float:
            if (to.kind == Kind::Int) {
                if (lossy) *lossy = true;
                return true;
            }
            return false;
        case Kind::Enum: return to.kind == Kind::Int;
        case Kind::Color: return to.kind == Kind::Vec4;
        case Kind::Vec4: return to.kind == Kind::Color;
        default: return false;
    }
}

bool convert(const Value& v, Type target, Value& out) {
    if (v.type == target) {
        out = v;
        return true;
    }
    if (!can_convert(v.type, target)) {
        out = Value::default_of(target);
        return false;
    }
    if (target == kString) {
        out = Value::string(display_string(v));
        return true;
    }
    switch (target.kind) {
        case Kind::Int:
            if (v.type.kind == Kind::Float) {
                const double d = std::trunc(v.u.f);
                // Out-of-range and NaN become 0 instead of undefined behaviour.
                out = Value::integer(std::isfinite(d) && std::fabs(d) < 9.2e18 ? static_cast<int64_t>(d) : 0);
            } else {
                out = Value::integer(v.u.i);   // Bool, Enum
            }
            return true;
        case Kind::Float: out = Value::number(static_cast<double>(v.u.i)); return true;   // Bool, Int
        case Kind::Vec4: out = Value::vec4(v.as_vec4()); return true;
        case Kind::Color: out = Value::color(v.as_vec4()); return true;
        default: break;
    }
    out = Value::default_of(target);
    return false;
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------
std::string number_text(double x) {
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, x);
    std::string s(buf, r.ptr);
    if (s.find_first_of(".eEni") == std::string::npos) s += ".0";
    return s;
}

std::string display_string(const Value& v) {
    if (v.type.array) {
        std::string s = "[";
        bool first = true;
        for (const Value& it : v.items()) {
            if (!first) s += ", ";
            first = false;
            s += display_string(it);
        }
        return s + "]";
    }
    switch (v.type.kind) {
        case Kind::None: return "none";
        case Kind::Bool: return v.u.i ? "true" : "false";
        case Kind::Int:
        case Kind::Enum: return std::to_string(v.u.i);
        case Kind::Float: return number_text(v.u.f);
        case Kind::String:
        case Kind::Asset: return v.str();
        case Kind::Vec2:
        case Kind::Vec3:
        case Kind::Vec4:
        case Kind::Quat:
        case Kind::Color: {
            const int n = component_count(v.type.kind);
            std::string s = "(";
            for (int i = 0; i < n; ++i) {
                if (i) s += ", ";
                s += float_text(v.u.v[i]);
            }
            return s + ")";
        }
        case Kind::Entity: return v.u.entity ? "entity#" + std::to_string(v.u.entity) : std::string("no entity");
        case Kind::Transform: {
            const TransformData t = v.as_transform();
            return "position " + display_string(Value::vec3(t.position)) + " rotation " +
                   display_string(Value::quat(t.rotation)) + " scale " + display_string(Value::vec3(t.scale));
        }
        case Kind::Struct: return "struct";
        default: return std::string();
    }
}

std::string literal_text(const Value& v) {
    if (v.type.array) {
        std::string s;
        bool first = true;
        for (const Value& it : v.items()) {
            if (!first) s += '|';
            first = false;
            const std::string e = literal_text(it);
            if (e.empty()) {
                s += "\\e";
                continue;
            }
            for (char c : e) {
                if (c == '\\' || c == '|') s += '\\';
                s += c;
            }
        }
        return s;
    }
    switch (v.type.kind) {
        case Kind::Bool: return v.u.i ? "true" : "false";
        case Kind::Int:
        case Kind::Enum: return std::to_string(v.u.i);
        case Kind::Float: return number_text(v.u.f);
        case Kind::String:
        case Kind::Asset: return v.str();
        case Kind::Vec2:
        case Kind::Vec3:
        case Kind::Vec4:
        case Kind::Quat:
        case Kind::Color: return join_floats(v.u.v, component_count(v.type.kind));
        case Kind::Entity: return std::to_string(v.u.entity);
        case Kind::Transform: {
            const TransformData t = v.as_transform();
            const float q[4] = {t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w};
            return join_floats(&t.position.x, 3) + ";" + join_floats(q, 4) + ";" + join_floats(&t.scale.x, 3);
        }
        default: return std::string();
    }
}

bool parse_literal(Type type, std::string_view text, Value& out) {
    if (type.array) {
        std::vector<Value> items;
        if (!text.empty()) {
            // Split on unescaped '|', undoing the escapes literal_text wrote.
            std::string cur;
            bool        explicit_empty = false;
            auto flush = [&]() -> bool {
                Value item;
                if (!parse_literal(type.element(), explicit_empty ? std::string_view() : std::string_view(cur), item))
                    return false;
                items.push_back(std::move(item));
                cur.clear();
                explicit_empty = false;
                return true;
            };
            for (size_t i = 0; i < text.size(); ++i) {
                const char c = text[i];
                if (c == '\\' && i + 1 < text.size()) {
                    const char n = text[++i];
                    if (n == 'e') explicit_empty = true;
                    else cur += n;
                } else if (c == '|') {
                    if (!flush()) return false;
                } else {
                    cur += c;
                }
            }
            if (!flush()) return false;
        }
        out = Value::array(type.element(), std::move(items));
        return true;
    }
    switch (type.kind) {
        case Kind::Bool:
            if (text == "true" || text == "1") { out = Value::boolean(true); return true; }
            if (text == "false" || text == "0" || text.empty()) { out = Value::boolean(false); return true; }
            return false;
        case Kind::Int:
        case Kind::Enum: {
            int64_t x = 0;
            if (!text.empty() && !parse_int(text, x)) return false;
            out = type.kind == Kind::Int ? Value::integer(x) : Value::enumeration(type.sub, x);
            return true;
        }
        case Kind::Float: {
            double x = 0.0;
            if (!text.empty() && !parse_double(text, x)) return false;
            out = Value::number(x);
            return true;
        }
        case Kind::String: out = Value::string(std::string(text)); return true;
        case Kind::Asset: out = Value::asset(type.sub, std::string(text)); return true;
        case Kind::Vec2:
        case Kind::Vec3:
        case Kind::Vec4:
        case Kind::Quat:
        case Kind::Color: {
            Value v = Value::default_of(type);
            if (!text.empty() && !parse_floats(text, v.u.v, component_count(type.kind))) return false;
            out = v;
            return true;
        }
        case Kind::Entity: {
            int64_t x = 0;
            if (!text.empty() && !parse_int(text, x)) return false;
            out = Value::entity(static_cast<uint32_t>(x));
            return true;
        }
        case Kind::Transform: {
            TransformData t;
            if (!text.empty()) {
                const auto parts = split(text, ';');
                float q[4] = {0, 0, 0, 1};
                if (parts.size() != 3 || !parse_floats(parts[0], &t.position.x, 3) || !parse_floats(parts[1], q, 4) ||
                    !parse_floats(parts[2], &t.scale.x, 3))
                    return false;
                t.rotation = glm::quat(q[3], q[0], q[1], q[2]);
            }
            out = Value::transform(t);
            return true;
        }
        default:
            out = Value::default_of(type);
            return text.empty();
    }
}

}  // namespace schizo::vs
