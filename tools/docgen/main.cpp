// ============================================================================
// docgen — generate the component reference from the authorable registry.
//
// The point (issue #10): documentation that CANNOT drift from the code. Every
// authorable component already self-registers with its reflected fields, so the
// reference is derived from the same data the inspector, the save sidecar and
// replication all read. Add a component with one make_authorable<T>() line and
// it documents itself; there is no second place to update and therefore no way
// to forget.
//
// Invoked as `gws docs`. Writes markdown to stdout or to --out <file>.
// ============================================================================
#include "engine/core/ecs/register_all.h"
#include "engine/core/ecs/world.h"
#include "engine/core/reflection/reflection.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <cstdint>

using namespace schizo;

namespace {

// Reflection stores a type id, not a spelling, for each field. Map the ids we
// know back to readable names; anything unrecognised prints its raw id so the
// gap is visible rather than silently rendered as "unknown".
std::string field_type_name(gws::reflect::TypeId id, size_t size) {
    static const std::map<gws::reflect::TypeId, const char*> known = {
        { gws::reflect::type_id_of<bool>(),        "bool"     },
        { gws::reflect::type_id_of<int>(),         "int"      },
        { gws::reflect::type_id_of<unsigned>(),    "unsigned" },
        { gws::reflect::type_id_of<float>(),       "float"    },
        { gws::reflect::type_id_of<double>(),      "double"   },
        { gws::reflect::type_id_of<uint8_t>(),     "uint8"    },
        { gws::reflect::type_id_of<uint16_t>(),    "uint16"   },
        { gws::reflect::type_id_of<uint32_t>(),    "uint32"   },
        { gws::reflect::type_id_of<uint64_t>(),    "uint64"   },
        { gws::reflect::type_id_of<int8_t>(),      "int8"     },
        { gws::reflect::type_id_of<int16_t>(),     "int16"    },
        { gws::reflect::type_id_of<int64_t>(),     "int64"    },
    };
    if (auto it = known.find(id); it != known.end()) return it->second;
    // Registered aggregates (glm::vec3, nested structs) resolve by id.
    if (const auto* ti = gws::reflect::Registry::instance().find(id))
        if (ti->name && *ti->name) return ti->name;
    std::ostringstream os;
    os << "(" << size << " bytes)";
    return os.str();
}

} // namespace

// ---------------------------------------------------------------- script API
//
// The ScriptApi is a plain C struct of function pointers, so unlike components
// it carries no runtime metadata and cannot be walked. It is still a SINGLE
// source of truth though — so the reference is parsed straight from the header
// rather than maintained separately. Same guarantee, different mechanism: add a
// verb to the table and it appears here; it cannot be documented and absent, or
// present and undocumented.
std::string generate_script_api(const std::string& header_path) {
    std::ifstream f(header_path);
    if (!f) return {};

    std::ostringstream md;
    md << "# Script API reference\n\n"
       << "**Generated** by `gws docs` from `" << header_path << "` — do not edit by hand.\n\n"
       << "One shared C table is exposed identically to all three scripting backends (Python, C++ and C#),\n"
       << "so every verb below is callable from any of them. The table is the contract: its layout is\n"
       << "static_asserted, and adding a verb once exposes it everywhere.\n\n";

    std::string line, pending_comment;
    int line_no = 0, pending_line = -99;   // a stand-alone comment only documents
                                           // the line DIRECTLY beneath it
    std::vector<std::pair<std::string, std::string>> verbs;  // signature, comment
    while (std::getline(f, line)) {
        ++line_no;
        // Trailing `// ...` on the previous line documents the next entry.
        const size_t slashes = line.find("//");
        std::string code = (slashes == std::string::npos) ? line : line.substr(0, slashes);
        std::string comment = (slashes == std::string::npos) ? "" : line.substr(slashes + 2);

        // A function-pointer member looks like:  ret (*name)(args);
        const size_t open = code.find("(*");
        if (open != std::string::npos && code.find(')') != std::string::npos) {
            std::string sig = code;
            // trim
            while (!sig.empty() && (sig.front() == ' ' || sig.front() == '\t')) sig.erase(sig.begin());
            while (!sig.empty() && (sig.back() == ' ' || sig.back() == '\t' || sig.back() == '\r')) sig.pop_back();
            if (!sig.empty()) {
                while (!comment.empty() && comment.front() == ' ') comment.erase(comment.begin());
                const bool adjacent = (pending_line == line_no - 1);
                verbs.emplace_back(sig, !comment.empty() ? comment
                                                         : (adjacent ? pending_comment : std::string()));
                pending_comment.clear();
            }
        } else if (!comment.empty() && code.find_first_not_of(" \t\r") == std::string::npos) {
            // A comment on its own line documents whatever follows.
            while (!comment.empty() && comment.front() == ' ') comment.erase(comment.begin());
            pending_comment = comment;
            pending_line = line_no;
        }
    }

    md << "**" << verbs.size() << " verbs** exposed.\n\n"
       << "| signature | notes |\n|---|---|\n";
    for (const auto& v : verbs) {
        std::string sig = v.first;
        if (!sig.empty() && sig.back() == ';') sig.pop_back();
        md << "| `" << sig << "` | " << v.second << " |\n";
    }
    md << "\n";
    return md.str();
}

int main(int argc, char** argv) {
    std::string out_path;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--out") == 0) out_path = argv[i + 1];

    // --script-api <header> [--script-out <file>] emits the scripting reference.
    std::string script_header, script_out;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--script-api") == 0) script_header = argv[i + 1];
        if (std::strcmp(argv[i], "--script-out") == 0) script_out    = argv[i + 1];
    }
    if (!script_header.empty()) {
        const std::string text = generate_script_api(script_header);
        if (text.empty()) {
            std::fprintf(stderr, "docgen: cannot read %s\n", script_header.c_str());
            return 1;
        }
        if (script_out.empty()) {
            std::fwrite(text.data(), 1, text.size(), stdout);
        } else {
            std::ofstream sf(script_out, std::ios::binary);
            if (!sf) { std::fprintf(stderr, "docgen: cannot write %s\n", script_out.c_str()); return 1; }
            sf << text;
            std::fprintf(stderr, "docgen: wrote %s\n", script_out.c_str());
        }
        if (out_path.empty()) return 0;   // script-only run
    }

    // Populating the registry is the whole job — one call, shared with the
    // editor and the checks, so this cannot document a different set than the
    // engine actually has.
    ecs::register_all_components();

    const auto& comps = ecs::authorable_components();

    std::ostringstream md;
    md << "# Component reference\n\n"
       << "**Generated** by `gws docs` from the authorable component registry — do not edit by hand.\n\n"
       << "Every component below self-registers with one `make_authorable<T>(\"Name\")` line, which is what\n"
       << "gives it inspector UI, `.gameplay` sidecar persistence and replication at the same time. This page is\n"
       << "derived from that same registration, so it cannot describe a component the engine does not have,\n"
       << "and it cannot miss one the engine does.\n\n"
       << "**" << comps.size() << " components** are registered.\n\n";

    // Contents
    md << "## Contents\n\n";
    for (const auto& c : comps) {
        std::string anchor = c.name;
        for (auto& ch : anchor) ch = (ch == ' ') ? '-' : static_cast<char>(std::tolower(ch));
        md << "- [" << c.name << "](#" << anchor << ")\n";
    }
    md << "\n---\n\n";

    size_t reflected = 0, custom = 0;
    for (const auto& c : comps) {
        md << "## " << c.name << "\n\n";

        if (!c.type && !c.cpp_type.empty())
            md << "`" << std::string(c.cpp_type) << "` — " << c.cpp_size << " bytes\n\n";

        if (!c.type) {
            // Data-driven components (AttributeSet, Inventory, Quest Log, …)
            // hold dynamic contents — vectors and strings — which the POD
            // reflection path cannot describe, so they carry no TypeInfo and
            // define their own serializer instead. Say so plainly rather than
            // rendering an empty field table that implies they have no data.
            ++custom;
            md << "_Custom serialization._ Holds dynamic contents rather than a fixed set of reflected fields,\n"
               << "so its layout is defined by its own serializer. Field-level documentation for these needs\n"
               << "reflection support for containers.\n\n";
            continue;
        }

        ++reflected;
        const auto& ti = *c.type;
        md << "`" << ti.name << "` — " << ti.size << " bytes, alignment " << ti.align
           << ", version " << ti.version << "\n\n";

        if (ti.fields.empty()) {
            md << "_No reflected fields._\n\n";
            continue;
        }
        md << "| field | type | offset | size |\n|---|---|---|---|\n";
        for (const auto& f : ti.fields) {
            md << "| `" << f.name << "` | " << field_type_name(f.type_id, f.size)
               << " | " << f.offset << " | " << f.size << " |\n";
        }
        md << "\n";
    }

    md << "---\n\n"
       << "_" << reflected << " components document their fields through reflection; "
       << custom << " use custom serialization._\n";

    const std::string text = md.str();
    if (out_path.empty()) {
        std::fwrite(text.data(), 1, text.size(), stdout);
    } else {
        std::ofstream f(out_path, std::ios::binary);
        if (!f) {
            std::fprintf(stderr, "docgen: cannot write %s\n", out_path.c_str());
            return 1;
        }
        f << text;
        std::fprintf(stderr, "docgen: wrote %s (%zu components)\n", out_path.c_str(), comps.size());
    }
    return 0;
}
