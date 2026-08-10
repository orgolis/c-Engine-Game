#pragma once
// ============================================================================
// reflected_text_io — write and read a reflected struct as KEY=value text.
//
// The scene format is text on purpose: scenes live in version control and a
// binary blob turns every edit into an unreviewable diff. Core serialization is
// binary, so the two cannot simply be joined; what CAN be shared is the
// reflection metadata. This drives the existing text format from it.
//
// What that buys: adding a field to a reflected scene component used to mean
// four hand edits -- a writer line, a parser line, a field on the intermediate
// struct, and an apply step -- with a silent failure mode, since a component
// that saves but does not load looks fine until someone reopens their scene.
// Through here it means zero.
//
// Keys are `PREFIX.field_name`, taken from the reflected name rather than
// invented, so the file says what the struct says.
//
// std::string fields are NOT reflected (offset reflection needs trivially
// copyable data), so components carrying paths or names still write those
// themselves. That is a real limit and worth stating plainly rather than
// leaving to be discovered: this handles the numeric and boolean fields, which
// is where the field-by-field tedium actually was.
// ============================================================================

#include "reflection/reflection.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdlib>
#include <ostream>
#include <sstream>
#include <string>

namespace schizo::scene {

namespace detail {

template <class T>
inline bool field_is(const gws::reflect::FieldInfo& f) {
    return f.type_id == gws::reflect::type_id_of<T>();
}

// Access goes through gws::reflect::field_ref, which already provides both a
// mutable and a const overload. I wrote my own pair first and they were
// ambiguous against it via ADL -- the compiler pointing out that the wheel was
// already here.
using gws::reflect::field_ref;

}  // namespace detail

/// Write every reflected field of `obj` as `PREFIX.name=value`, one per line.
/// Fields of a type this does not know are skipped rather than written wrongly
/// -- a missing line loads as the default, whereas a malformed one can take the
/// rest of the file with it.
inline void write_reflected(std::ostream& out, const std::string& prefix,
                            const void* obj, const gws::reflect::TypeInfo& ti) {
    using namespace detail;
    for (const gws::reflect::FieldInfo& f : ti.fields) {
        // Formatted first, emitted second: an unknown type must produce no line
        // at all, and deciding that after the key is already in the stream
        // means trying to un-write it.
        std::ostringstream v;
        if      (field_is<bool>(f))     v << (field_ref<bool>(obj, f) ? '1' : '0');
        else if (field_is<int>(f))      v << field_ref<int>(obj, f);
        else if (field_is<uint32_t>(f)) v << field_ref<uint32_t>(obj, f);
        else if (field_is<float>(f))    v << field_ref<float>(obj, f);
        else if (field_is<glm::vec2>(f)) {
            const glm::vec2& a = field_ref<glm::vec2>(obj, f);
            v << a.x << ' ' << a.y;
        } else if (field_is<glm::vec3>(f)) {
            const glm::vec3& a = field_ref<glm::vec3>(obj, f);
            v << a.x << ' ' << a.y << ' ' << a.z;
        } else if (field_is<glm::vec4>(f)) {
            const glm::vec4& a = field_ref<glm::vec4>(obj, f);
            v << a.x << ' ' << a.y << ' ' << a.z << ' ' << a.w;
        } else if (field_is<glm::quat>(f)) {
            const glm::quat& q = field_ref<glm::quat>(obj, f);
            v << q.x << ' ' << q.y << ' ' << q.z << ' ' << q.w;
        } else {
            continue;                       // unknown field type: no line
        }
        out << prefix << '.' << f.name << '=' << v.str() << '\n';
    }
}

/// Apply one `PREFIX.name=value` line to `obj`. Returns false when the line is
/// not for this prefix or names no field of this type, so the caller can fall
/// through to its other parsers -- which is what keeps old scene files loading.
inline bool apply_reflected_line(const std::string& prefix, const std::string& line,
                                 void* obj, const gws::reflect::TypeInfo& ti) {
    using namespace detail;
    if (line.size() <= prefix.size() + 1) return false;
    if (line.compare(0, prefix.size(), prefix) != 0) return false;
    if (line[prefix.size()] != '.') return false;

    const size_t eq = line.find('=', prefix.size());
    if (eq == std::string::npos) return false;

    const std::string key = line.substr(prefix.size() + 1, eq - prefix.size() - 1);
    const std::string val = line.substr(eq + 1);

    for (const gws::reflect::FieldInfo& f : ti.fields) {
        if (key != f.name) continue;

        // Multi-component values read back through the same whitespace layout
        // they were written with.
        auto floats = [&](float* dst, int n) {
            const char* p = val.c_str();
            char* end = nullptr;
            for (int i = 0; i < n; ++i) {
                dst[i] = std::strtof(p, &end);
                if (end == p) return false;
                p = end;
            }
            return true;
        };

        if      (field_is<bool>(f))     field_ref<bool>(obj, f)     = (val == "1" || val == "true");
        else if (field_is<int>(f))      field_ref<int>(obj, f)      = std::atoi(val.c_str());
        else if (field_is<uint32_t>(f)) field_ref<uint32_t>(obj, f) =
                                            static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
        else if (field_is<float>(f))    field_ref<float>(obj, f)    = std::strtof(val.c_str(), nullptr);
        else if (field_is<glm::vec2>(f)) floats(&field_ref<glm::vec2>(obj, f).x, 2);
        else if (field_is<glm::vec3>(f)) floats(&field_ref<glm::vec3>(obj, f).x, 3);
        else if (field_is<glm::vec4>(f)) floats(&field_ref<glm::vec4>(obj, f).x, 4);
        else if (field_is<glm::quat>(f)) floats(&field_ref<glm::quat>(obj, f).x, 4);
        else return false;                  // known name, unknown type: not ours

        return true;
    }
    return false;
}

}  // namespace schizo::scene
