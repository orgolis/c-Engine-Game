#pragma once

// ============================================================================
// vscript/registry.h -- every kind of node there is, and what pins each has.
// ============================================================================
//
// A NodeDef describes one kind of node. Three sorts:
//   - native: a C++ function. Registry::fn() takes any callable and derives
//     the pins from its signature, so exposing an engine function to graphs is
//     one line. "Pure" natives have no exec pins and are evaluated when a
//     consumer needs their value.
//   - builtin: flow control and the nodes that touch variables, functions and
//     events. The compiler emits code for these directly.
//   - event: an entry point the host fires by name ("Begin Play", "Tick").
//
// shape_of() answers "what pins does THIS node have", which for some nodes
// depends on the graph: a function call's pins are the function's signature,
// a Sequence has as many outputs as the author added.

#include "vscript/graph.h"
#include "vscript/value.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace schizo::vs {

struct Context;   // vm.h: what a native function is handed when it runs

/// A native node's implementation. `args` are the input values in pin order,
/// `out` the output slots in pin order (already holding type defaults).
using NativeFn = std::function<void(Context&, Value* args, Value* out)>;

enum class Builtin : uint16_t {
    None = 0,
    // flow
    Branch, Sequence, ForLoop, ForLoopBreak, ForEach, ForEachBreak, While,
    DoOnce, DoN, FlipFlop, Gate, MultiGate, SwitchInt, SwitchString,
    Select, SelectBool, Delay, RetriggerableDelay, Reroute,
    // data
    VarGet, VarSet, LocalGet, LocalSet, MakeArray, Self,
    // functions and events
    FnEntry, FnReturn, FnCall, Event, CustomEvent, CallEvent,
    // other entities' scripts
    SendEvent, RemoteGet, RemoteSet,
};

struct PinDef {
    std::string name;
    Type        type;            // Kind::Exec for flow, Kind::Any for a wildcard
    bool        input = true;
    Value       def;             // literal used when an input is left unwired
};

struct Overload {
    Type     type;               // the wildcard type this implementation is for
    NativeFn fn;
    uint16_t op = 0;             // a VM opcode that does the same thing, or 0
};

struct NodeDef {
    std::string id;              // stable: saved in files
    std::string title;           // shown on the node
    std::string category;        // palette path, "Math|Float"
    std::string tooltip;
    std::string keywords;        // extra search terms
    std::vector<PinDef> pins;    // inputs and outputs, in display order
    bool     pure = false;       // no exec pins
    bool     latent = false;     // finishes later (Delay); not allowed in functions
    bool     compact = false;    // draw as an operator ("+") without pin labels
    bool     named = false;      // an event told apart by Node::name (which key, which bus event)
    std::string name_label;      // what that name is called in the editor ("Key", "Event")
    Builtin  builtin = Builtin::None;
    NativeFn fn;                 // native nodes
    uint16_t op = 0;             // VM opcode equivalent of `fn`, or 0
    std::vector<Overload> overloads;   // wildcard natives: chosen by the resolved type
    Type     default_any;        // wildcard nodes: the type assumed when nothing is wired

    NodeDef& in(std::string name, Type type, Value def = Value()) {
        PinDef p;
        p.name = std::move(name);
        p.type = type;
        p.input = true;
        p.def = def.type == type && !type.is_any() ? std::move(def) : Value::default_of(type);
        pins.push_back(std::move(p));
        return *this;
    }
    NodeDef& out(std::string name, Type type) {
        PinDef p;
        p.name = std::move(name);
        p.type = type;
        p.input = false;
        pins.push_back(std::move(p));
        return *this;
    }
    NodeDef& tip(std::string t) { tooltip = std::move(t); return *this; }
    NodeDef& keys(std::string k) { keywords = std::move(k); return *this; }
    NodeDef& as_compact() { compact = true; return *this; }
    NodeDef& overload(Type t, NativeFn f, uint16_t opcode = 0) {
        overloads.push_back(Overload{t, std::move(f), opcode});
        return *this;
    }
    const Overload* find_overload(Type t) const {
        for (const auto& o : overloads)
            if (o.type == t) return &o;
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// Marshalling: C++ types <-> Values, for Registry::fn().
// ---------------------------------------------------------------------------
/// Wrap a uint32 scene entity id so it binds to an Entity pin instead of Int.
struct EntityId {
    uint32_t id = 0;
};
/// Wrap an RGBA colour so it binds to a Color pin instead of Vec4.
struct ColorValue {
    glm::vec4 rgba{1.0f};
};

namespace detail {

template <class T, class = void>
struct Marshal;   // no definition: an unsupported type is a compile error at the registration

template <> struct Marshal<bool> {
    static Type  type() { return kBool; }
    static bool  from(const Value& v) { return v.as_bool(); }
    static Value to(bool x) { return Value::boolean(x); }
};
template <class T>
struct Marshal<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>> {
    static Type  type() { return kInt; }
    static T     from(const Value& v) { return static_cast<T>(v.as_int()); }
    static Value to(T x) { return Value::integer(static_cast<int64_t>(x)); }
};
template <class T>
struct Marshal<T, std::enable_if_t<std::is_floating_point_v<T>>> {
    static Type  type() { return kFloat; }
    static T     from(const Value& v) { return static_cast<T>(v.as_float()); }
    static Value to(T x) { return Value::number(static_cast<double>(x)); }
};
template <> struct Marshal<std::string> {
    static Type        type() { return kString; }
    static std::string from(const Value& v) { return v.str(); }
    static Value       to(std::string x) { return Value::string(std::move(x)); }
};
template <> struct Marshal<glm::vec2> {
    static Type      type() { return kVec2; }
    static glm::vec2 from(const Value& v) { return v.as_vec2(); }
    static Value     to(const glm::vec2& x) { return Value::vec2(x); }
};
template <> struct Marshal<glm::vec3> {
    static Type      type() { return kVec3; }
    static glm::vec3 from(const Value& v) { return v.as_vec3(); }
    static Value     to(const glm::vec3& x) { return Value::vec3(x); }
};
template <> struct Marshal<glm::vec4> {
    static Type      type() { return kVec4; }
    static glm::vec4 from(const Value& v) { return v.as_vec4(); }
    static Value     to(const glm::vec4& x) { return Value::vec4(x); }
};
template <> struct Marshal<glm::quat> {
    static Type      type() { return kQuat; }
    static glm::quat from(const Value& v) { return v.as_quat(); }
    static Value     to(const glm::quat& x) { return Value::quat(x); }
};
template <> struct Marshal<ColorValue> {
    static Type       type() { return kColor; }
    static ColorValue from(const Value& v) { return ColorValue{v.as_vec4()}; }
    static Value      to(const ColorValue& x) { return Value::color(x.rgba); }
};
template <> struct Marshal<TransformData> {
    static Type          type() { return kTransform; }
    static TransformData from(const Value& v) { return v.as_transform(); }
    static Value         to(const TransformData& x) { return Value::transform(x); }
};
template <> struct Marshal<EntityId> {
    static Type     type() { return kEntity; }
    static EntityId from(const Value& v) { return EntityId{v.as_entity()}; }
    static Value    to(EntityId x) { return Value::entity(x.id); }
};
template <class T> struct Marshal<std::vector<T>> {
    static Type type() { return Marshal<T>::type().as_array(); }
    static std::vector<T> from(const Value& v) {
        std::vector<T> out;
        out.reserve(v.items().size());
        for (const Value& it : v.items()) out.push_back(Marshal<T>::from(it));
        return out;
    }
    static Value to(const std::vector<T>& x) {
        std::vector<Value> items;
        items.reserve(x.size());
        for (const T& it : x) items.push_back(Marshal<T>::to(it));
        return Value::array(Marshal<T>::type(), std::move(items));
    }
};

template <class F> struct callable : callable<decltype(&std::remove_reference_t<F>::operator())> {};
template <class C, class R, class... A> struct callable<R (C::*)(A...) const> {
    using result = R;
    using args = std::tuple<A...>;
};
template <class C, class R, class... A> struct callable<R (C::*)(A...)> {
    using result = R;
    using args = std::tuple<A...>;
};
template <class R, class... A> struct callable<R (*)(A...)> {
    using result = R;
    using args = std::tuple<A...>;
};

template <class T> struct is_tuple : std::false_type {};
template <class... T> struct is_tuple<std::tuple<T...>> : std::true_type {};

template <class R> void store_result(Value* out, R&& r) {
    using D = std::decay_t<R>;
    if constexpr (is_tuple<D>::value) {
        std::apply(
            [&](auto&&... parts) {
                size_t i = 0;
                ((out[i++] = Marshal<std::decay_t<decltype(parts)>>::to(std::forward<decltype(parts)>(parts))), ...);
            },
            std::forward<R>(r));
    } else {
        out[0] = Marshal<D>::to(std::forward<R>(r));
    }
}

template <class R> struct result_types {
    static void append(std::vector<Type>& types) { types.push_back(Marshal<std::decay_t<R>>::type()); }
};
template <> struct result_types<void> {
    static void append(std::vector<Type>&) {}
};
template <class... T> struct result_types<std::tuple<T...>> {
    static void append(std::vector<Type>& types) { (types.push_back(Marshal<std::decay_t<T>>::type()), ...); }
};

// Without a Context parameter.
template <class F, class R, class... A, size_t... I>
NativeFn make_native(F f, std::tuple<A...>*, R*, std::index_sequence<I...>, std::false_type) {
    return [f = std::move(f)](Context&, Value* args, Value* out) {
        (void)args;
        (void)out;
        if constexpr (std::is_void_v<R>) {
            f(Marshal<std::decay_t<A>>::from(args[I])...);
        } else {
            store_result(out, f(Marshal<std::decay_t<A>>::from(args[I])...));
        }
    };
}
// With a leading Context& parameter.
template <class F, class R, class C, class... A, size_t... I>
NativeFn make_native(F f, std::tuple<C, A...>*, R*, std::index_sequence<I...>, std::true_type) {
    return [f = std::move(f)](Context& ctx, Value* args, Value* out) {
        (void)args;
        (void)out;
        if constexpr (std::is_void_v<R>) {
            f(ctx, Marshal<std::decay_t<A>>::from(args[I])...);
        } else {
            store_result(out, f(ctx, Marshal<std::decay_t<A>>::from(args[I])...));
        }
    };
}

template <class... A> void append_arg_types(std::vector<Type>& types, std::tuple<A...>*) {
    (types.push_back(Marshal<std::decay_t<A>>::type()), ...);
}
template <class C, class... A> void append_arg_types_skip_first(std::vector<Type>& types, std::tuple<C, A...>*) {
    (types.push_back(Marshal<std::decay_t<A>>::type()), ...);
}

template <class Tuple> struct first_is_context : std::false_type {};
template <class First, class... Rest>
struct first_is_context<std::tuple<First, Rest...>> : std::is_same<std::decay_t<First>, Context> {};

}  // namespace detail

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------
class Registry {
public:
    Registry() = default;
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    /// Add a definition. A second definition with the same id replaces the first.
    NodeDef& add(NodeDef def);

    /// A node that is a C++ function with exec pins ("In" / "Then") around it.
    /// `names` gives the input pin names followed by the output pin names.
    template <class F>
    NodeDef& fn(std::string id, std::string category, std::string title, F f,
                std::initializer_list<const char*> names = {}) {
        return bind(std::move(id), std::move(category), std::move(title), std::move(f), names, false);
    }
    /// A node that is a C++ function with no exec pins: it has no side effects
    /// and is evaluated when a consumer needs its value.
    template <class F>
    NodeDef& pure(std::string id, std::string category, std::string title, F f,
                  std::initializer_list<const char*> names = {}) {
        return bind(std::move(id), std::move(category), std::move(title), std::move(f), names, true);
    }

    const NodeDef* find(std::string_view id) const;
    const std::vector<std::unique_ptr<NodeDef>>& all() const { return defs_; }

private:
    template <class F>
    NodeDef& bind(std::string id, std::string category, std::string title, F f,
                  std::initializer_list<const char*> names, bool is_pure) {
        using Tr = detail::callable<std::decay_t<F>>;
        using Args = typename Tr::args;
        using R = typename Tr::result;
        using HasCtx = typename detail::first_is_context<Args>::type;

        std::vector<Type> in_types, out_types;
        if constexpr (HasCtx::value) detail::append_arg_types_skip_first(in_types, static_cast<Args*>(nullptr));
        else detail::append_arg_types(in_types, static_cast<Args*>(nullptr));
        detail::result_types<R>::append(out_types);

        NodeDef d;
        d.id = std::move(id);
        d.category = std::move(category);
        d.title = std::move(title);
        d.pure = is_pure;
        if (!is_pure) d.in("In", kExec);
        std::vector<const char*> nm(names.begin(), names.end());
        size_t n = 0;
        for (size_t i = 0; i < in_types.size(); ++i, ++n)
            d.in(n < nm.size() ? nm[n] : default_name(i, true, in_types.size()), in_types[i]);
        if (!is_pure) d.out("Then", kExec);
        for (size_t i = 0; i < out_types.size(); ++i, ++n)
            d.out(n < nm.size() ? nm[n] : default_name(i, false, out_types.size()), out_types[i]);

        constexpr size_t arity = std::tuple_size_v<Args> - (HasCtx::value ? 1 : 0);
        d.fn = detail::make_native(std::move(f), static_cast<Args*>(nullptr), static_cast<R*>(nullptr),
                                   std::make_index_sequence<arity>{}, HasCtx{});
        return add(std::move(d));
    }
    static std::string default_name(size_t i, bool input, size_t count);

    std::vector<std::unique_ptr<NodeDef>>   defs_;
    std::unordered_map<std::string, size_t> by_id_;
};

// ---------------------------------------------------------------------------
// Shapes: the concrete pins of one node in one graph.
// ---------------------------------------------------------------------------
struct Pin {
    std::string name;
    Type        type;
    bool        input = true;
    Value       def;              // inputs: the literal used when unwired
    bool        wildcard = false; // came from a Kind::Any pin of the definition
};

struct Shape {
    const NodeDef*   def = nullptr;
    std::vector<Pin> pins;
    bool             pure = false;

    int find(const std::string& name, bool input) const {
        for (size_t i = 0; i < pins.size(); ++i)
            if (pins[i].input == input && pins[i].name == name) return static_cast<int>(i);
        return -1;
    }
    bool has_wildcard() const {
        for (const auto& p : pins)
            if (p.wildcard) return true;
        return false;
    }
};

/// The pins of `node`. Wildcard pins take `node.targ` when it is set and stay
/// Kind::Any otherwise. On failure (unknown definition, missing variable or
/// function) returns false and sets `error`.
bool shape_of(const Registry& reg, const Script& script, const Graph& graph, const Node& node, Shape& out,
              std::string* error = nullptr);

/// Registers flow control, variables, functions, events, math, strings and
/// arrays: everything that does not need an engine. Hosts add their own on top.
void register_standard_library(Registry& reg);

}  // namespace schizo::vs
