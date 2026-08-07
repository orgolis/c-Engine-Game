#pragma once

// ============================================================================
// G9 · Persistence — save / load game state (SaveId-keyed) + world flags.
// ============================================================================
//
// A SaveGame captures every entity carrying a SaveId (a stable, game-assigned
// key) as a Prefab of its authorable gameplay components (attributes, inventory,
// quests, equipment, …) and restores them by matching SaveId on load — so player
// progression and world state persist across sessions. WorldFlags is a key→int
// store for world state (chests opened, bosses killed, quest flags). Reuses the
// F4 Prefab (de)serialization, so any authorable component is saved for free.

#include "world.h"
#include "prefab.h"
#include "authorable_components.h"
#include "component_serialize.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace schizo::ecs {

// A stable, game-assigned key so a saved entity can be matched on load.
struct SaveId { uint64_t value = 0; };

struct SaveGame {
    struct Entry { uint64_t key; Prefab prefab; };
    std::vector<Entry> entries;

    static SaveGame capture(World& w) {
        SaveGame sg;
        w.each<SaveId>([&](Entity e, SaveId& s) {
            sg.entries.push_back({s.value, Prefab::capture(w, e, "Save")});
        });
        return sg;
    }

    // Restore onto entities matched by SaveId; create + key any missing ones.
    void apply(World& w) const {
        std::vector<std::pair<uint64_t, Entity>> existing;
        w.each<SaveId>([&](Entity e, SaveId& s) { existing.push_back({s.value, e}); });
        for (const auto& en : entries) {
            Entity e = null_entity;
            for (const auto& x : existing) if (x.first == en.key) { e = x.second; break; }
            if (e == null_entity) { e = w.create(); w.add<SaveId>(e, SaveId{en.key}); existing.push_back({en.key, e}); }
            en.prefab.apply(w, e);
        }
    }

    std::string to_text() const {
        std::string s = "SAVEGAME " + std::to_string(entries.size()) + "\n";
        for (const auto& en : entries) {
            s += "ENTITY " + std::to_string(en.key) + "\n";
            s += en.prefab.to_text();   // "PREFAB ...\n<comp>\t<hex>\n..."
        }
        return s;
    }
    static SaveGame from_text(const std::string& text) {
        SaveGame sg;
        std::istringstream in(text);
        std::string line, block;
        uint64_t key = 0; bool have = false;
        auto flush = [&] {
            if (have) { Entry en; en.key = key; en.prefab = Prefab::from_text(block); sg.entries.push_back(std::move(en)); }
            block.clear(); have = false;
        };
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.compare(0, 7, "ENTITY ") == 0) { flush(); key = std::strtoull(line.c_str() + 7, nullptr, 10); have = true; }
            else if (line.compare(0, 9, "SAVEGAME ") == 0) continue;
            else if (have) { block += line; block += "\n"; }
        }
        flush();
        return sg;
    }
};

inline bool save_game_file(World& w, const std::string& path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << SaveGame::capture(w).to_text();
    return static_cast<bool>(f);
}
inline bool load_game_file(World& w, const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    SaveGame::from_text(ss.str()).apply(w);
    return true;
}

// ---------------- world flags (chests opened, bosses killed, quest flags) ----------------
struct WorldFlags { std::vector<std::pair<std::string, int>> flags; };
inline int world_flag(const WorldFlags& wf, const std::string& key) {
    for (const auto& f : wf.flags) if (f.first == key) return f.second;
    return 0;
}
inline void set_world_flag(WorldFlags& wf, const std::string& key, int value) {
    for (auto& f : wf.flags) if (f.first == key) { f.second = value; return; }
    wf.flags.push_back({key, value});
}

// ---------------- custom serialization ----------------
inline void serialize_saveid(const void* o, std::vector<uint8_t>& out) {
    detail::put_bytes(out, &static_cast<const SaveId*>(o)->value, 8);
}
inline void deserialize_saveid(void* o, const uint8_t* d, size_t n) {
    const uint8_t* p = d; detail::get_bytes(p, d + n, &static_cast<SaveId*>(o)->value, 8);
}
inline void serialize_world_flags(const void* obj, std::vector<uint8_t>& out) {
    const auto& wf = *static_cast<const WorldFlags*>(obj);
    uint32_t n = static_cast<uint32_t>(wf.flags.size()); detail::put_bytes(out, &n, 4);
    for (const auto& f : wf.flags) { detail::put_str(out, f.first); detail::put_i32(out, f.second); }
}
inline void deserialize_world_flags(void* obj, const uint8_t* data, size_t n) {
    auto& wf = *static_cast<WorldFlags*>(obj); wf.flags.clear();
    const uint8_t* p = data; const uint8_t* e = data + n;
    uint32_t nf = 0; if (!detail::get_bytes(p, e, &nf, 4)) return;
    for (uint32_t i = 0; i < nf; ++i) {
        std::string k; int32_t v = 0;
        if (!detail::get_str(p, e, k) || !detail::get_i32(p, e, v)) return;
        wf.flags.push_back({k, v});
    }
}
inline void register_persistence_components() {
    register_authorable(make_authorable_custom<SaveId>("Save Id", &serialize_saveid, &deserialize_saveid));
    register_authorable(make_authorable_custom<WorldFlags>("World Flags", &serialize_world_flags, &deserialize_world_flags));
}

}  // namespace schizo::ecs
