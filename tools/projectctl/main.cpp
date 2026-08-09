// ============================================================================
// projectctl — read and modify a project's gameplay state from outside the
// editor. Exposed as `gws project <subcommand>` (issue #8).
//
// This is the interface that makes the engine agent-drivable. The insight it
// rests on: our project state is TEXT, and every authorable component already
// self-describes through the registry — so an external tool can enumerate and
// edit gameplay data without linking the editor or opening a window. Unity and
// Unreal assistants cannot do this because their project state is opaque binary.
//
// Safety by construction:
//   * operates on FILES, so every change is a diff and shows up in git
//   * --save is explicit; without it nothing is written and the command is a
//     pure query
//   * entities are addressed by SaveId, the stable game-assigned key, not by a
//     runtime handle that changes between loads
//
// Subcommands:
//   list    --load <file> [--json]
//   add     --load <file> --save <file> --entity <id> --component <name>
//   remove  --load <file> --save <file> --entity <id> --component <name>
//   set     --load <file> --save <file> --entity <id> --component <name>
//           --field <name> --value <number>
// ============================================================================
#include "engine/core/ecs/register_all.h"
#include "engine/core/ecs/gameplay_savegame.h"
#include "engine/core/ecs/world.h"
#include "engine/core/reflection/reflection.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace schizo;

namespace {

const char* opt(int argc, char** argv, const char* n, const char* d = nullptr) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], n) == 0) return argv[i + 1];
    return d;
}
bool flag(int argc, char** argv, const char* n) {
    for (int i = 1; i < argc; ++i) if (std::strcmp(argv[i], n) == 0) return true;
    return false;
}
std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
    return o;
}

int fail(const char* msg) { std::fprintf(stderr, "projectctl: %s\n", msg); return 1; }

// Entities are addressed by SaveId — the stable key the save format uses —
// rather than a runtime entity handle, which differs between loads.
ecs::Entity find_by_save_id(ecs::World& w, uint64_t id) {
    ecs::Entity found = ecs::null_entity;
    w.each<ecs::SaveId>([&](ecs::Entity e, ecs::SaveId& s) {
        if (s.value == id) found = e;
    });
    return found;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: projectctl <list|add|remove|set> --load <file> [...]\n");
        return 2;
    }
    const std::string sub  = argv[1];
    const bool        json = flag(argc, argv, "--json");
    const char*       in   = opt(argc, argv, "--load");
    const char*       out  = opt(argc, argv, "--save");

    ecs::register_all_components();
    ecs::World world;

    if (!in) return fail("--load <file> is required");
    if (!fs::exists(in)) return fail("no such file (--load)");
    if (!ecs::load_game_file(world, in)) return fail("failed to parse the save file");

    const auto& registry = ecs::authorable_components();

    // ------------------------------------------------------------------ list
    if (sub == "list") {
        struct Row { uint64_t id; std::vector<std::string> comps; };
        std::vector<Row> rows;
        world.each<ecs::SaveId>([&](ecs::Entity e, ecs::SaveId& s) {
            Row r; r.id = s.value;
            for (const auto& c : registry)
                if (c.has && c.has(world, e)) r.comps.emplace_back(c.name);
            rows.push_back(std::move(r));
        });
        if (json) {
            std::printf("{\"entities\":[");
            for (size_t i = 0; i < rows.size(); ++i) {
                std::printf("%s{\"save_id\":%llu,\"components\":[", i ? "," : "",
                            static_cast<unsigned long long>(rows[i].id));
                for (size_t j = 0; j < rows[i].comps.size(); ++j)
                    std::printf("%s\"%s\"", j ? "," : "", esc(rows[i].comps[j]).c_str());
                std::printf("]}");
            }
            std::printf("]}\n");
        } else {
            std::printf("%zu entities in %s\n", rows.size(), in);
            for (const auto& r : rows) {
                std::printf("  #%llu:", static_cast<unsigned long long>(r.id));
                for (const auto& c : r.comps) std::printf(" %s;", c.c_str());
                std::printf("\n");
            }
        }
        return 0;
    }

    // Mutating subcommands from here on.
    const char* comp_name = opt(argc, argv, "--component");
    const char* ent_arg   = opt(argc, argv, "--entity");
    if (!comp_name) return fail("--component <name> is required");
    if (!ent_arg)   return fail("--entity <save-id> is required");
    if (!out)       return fail("--save <file> is required for a mutating command "
                                "(nothing is written without it, on purpose)");

    const uint64_t save_id = std::strtoull(ent_arg, nullptr, 10);
    const ecs::Entity e = find_by_save_id(world, save_id);
    if (e == ecs::null_entity) return fail("no entity with that save id");

    const ecs::AuthorableComponent* ct = ecs::find_authorable(comp_name);
    if (!ct) {
        std::fprintf(stderr, "projectctl: unknown component '%s'. Run `gws docs` for the list.\n",
                     comp_name);
        return 1;
    }

    if (sub == "add") {
        if (ct->has(world, e)) { std::printf("already present: %s\n", ct->name); }
        else { ct->add(world, e); std::printf("added %s to #%s\n", ct->name, ent_arg); }
    } else if (sub == "remove") {
        if (!ct->has(world, e)) { std::printf("not present: %s\n", ct->name); }
        else { ct->remove(world, e); std::printf("removed %s from #%s\n", ct->name, ent_arg); }
    } else if (sub == "set") {
        const char* field = opt(argc, argv, "--field");
        const char* value = opt(argc, argv, "--value");
        if (!field || !value) return fail("--field and --value are required for `set`");
        if (!ct->type) {
            std::fprintf(stderr,
                "projectctl: '%s' uses custom serialization and has no reflected fields,\n"
                "so individual fields cannot be addressed. This affects most data-driven\n"
                "components; see the component reference (gws docs).\n", ct->name);
            return 1;
        }
        if (!ct->has(world, e)) return fail("entity does not have that component");
        void* obj = ct->get(world, e);
        bool done = false;
        for (const auto& f : ct->type->fields) {
            if (std::strcmp(f.name, field) != 0) continue;
            // Numeric fields only: enough for tuning values, which is what an
            // agent or a script actually adjusts. Strings and containers need
            // the richer protocol tracked on the epic.
            auto* p = static_cast<char*>(obj) + f.offset;
            if      (f.size == sizeof(float))   *reinterpret_cast<float*>(p)   = std::strtof(value, nullptr);
            else if (f.size == sizeof(double))  *reinterpret_cast<double*>(p)  = std::strtod(value, nullptr);
            else if (f.size == sizeof(int32_t)) *reinterpret_cast<int32_t*>(p) = static_cast<int32_t>(std::strtol(value, nullptr, 10));
            else { return fail("unsupported field width for `set`"); }
            done = true;
            break;
        }
        if (!done) return fail("no such field on that component");
        std::printf("set %s.%s = %s on #%s\n", ct->name, field, value, ent_arg);
    } else {
        return fail("unknown subcommand (expected list, add, remove or set)");
    }

    if (!ecs::save_game_file(world, out)) return fail("failed to write the save file");
    std::printf("wrote %s\n", out);
    return 0;
}
