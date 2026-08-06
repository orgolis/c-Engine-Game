#pragma once

// ============================================================================
// Prefab / archetype  (F4)  — a template of gameplay components, instantiable
// onto any entity. The reusable spawn primitive: enemies, items, projectiles,
// NPCs are all "instantiate a prefab at a position." Generic over the authorable-
// component registry via reflection — no per-component code.
//
// This is the GAMEPLAY-component core. The editor layer adds the visual/primitive
// part (mesh, color) on top when it spawns an OOP entity, then calls apply().
// ============================================================================

#include "world.h"
#include "authorable_components.h"
#include "component_serialize.h"

#include <string>
#include <utility>
#include <vector>

namespace schizo::ecs {

struct Prefab {
    std::string name;
    // (authorable component name, serialized field bytes), in registry order.
    std::vector<std::pair<std::string, std::vector<uint8_t>>> components;

    // Capture every authorable component currently on `e`.
    static Prefab capture(World& w, Entity e, const std::string& name = "Prefab") {
        Prefab p; p.name = name;
        for (const auto& ct : authorable_components()) {
            if (!ct.has(w, e)) continue;
            if (void* c = ct.get(w, e))
                p.components.emplace_back(ct.name, serialize_authorable(ct, c));
        }
        return p;
    }

    // Add + fill each captured component onto `e` (overwriting any existing).
    void apply(World& w, Entity e) const {
        for (const auto& [cname, bytes] : components) {
            const AuthorableComponent* ct = find_authorable(cname.c_str());
            if (!ct) continue;
            ct->add(w, e);
            if (void* c = ct->get(w, e))
                deserialize_authorable(*ct, c, bytes.data(), bytes.size());
        }
    }

    bool empty() const { return components.empty(); }

    // Text form: "PREFAB <name>\n<component>\t<hex>\n...".
    std::string to_text() const {
        std::string s = "PREFAB " + name + "\n";
        for (const auto& [cname, bytes] : components)
            s += cname + "\t" + to_hex(bytes) + "\n";
        return s;
    }

    static Prefab from_text(const std::string& text) {
        Prefab p;
        size_t start = 0;
        while (start <= text.size()) {
            const size_t nl = text.find('\n', start);
            std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
            start = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            if (line.compare(0, 7, "PREFAB ") == 0) { p.name = line.substr(7); continue; }
            const size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            p.components.emplace_back(line.substr(0, tab), from_hex(line.substr(tab + 1)));
        }
        return p;
    }
};

}  // namespace schizo::ecs
