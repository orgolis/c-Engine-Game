#pragma once

// ============================================================
// ECS World Snapshot  (Master Plan Stage 0.5 step 5 — save-game / net base)
// ============================================================
//
// Serialize an entire ecs::World — every entity plus a chosen set of component
// types — onto the reflection-driven binary archive (Stage 0.4/0.5). This is
// the single primitive behind BOTH save-games and Stage 7 networking (a server
// authoritative ECS snapshot / delta).
//
// Why our own (not entt::snapshot): we reuse the SAME field-id-keyed, versioned
// archive as every other serialized type, so component layout can evolve
// (add/remove/reorder fields, version upgrades) and snapshots stay loadable.
//
//   std::vector<uint8_t> buf;
//   gws::serialize::BinaryWriter w(buf);
//   ecs::save_world<Transform, Velocity, Name>(w, world);
//   ...
//   gws::serialize::BinaryReader r(buf.data(), buf.size());
//   ecs::load_world<Transform, Velocity, Name>(r, fresh_world);
//
// Format:
//   [u32 magic][u32 version][u64 entity_count][u32 component_type_count]
//   per component type:
//     [u64 component_type_id][u64 instance_count][u64 block_byte_len]
//     per instance: [u32 save_index][ component via gws::serialize::write ]
// `block_byte_len` lets a loader SKIP a component type it doesn't know (loading
// an old/new save with a different component set is safe).

#include "world.h"
#include "components.h"
#include "serialization/serialization.h"
#include "reflection/reflection.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace schizo::ecs {

inline constexpr uint32_t kWorldSnapshotMagic   = 0x57534345u; // 'E','C','S','W'
inline constexpr uint32_t kWorldSnapshotVersion = 1u;

namespace detail {

inline uint32_t entity_key(Entity e) {
    // entt::entity is a scoped enum over entt::id_type (uint32_t); the packed
    // index+version value is a stable per-live-entity key.
    return static_cast<uint32_t>(e);
}

// First-seen union of entities owning any of the saved component types, each
// assigned a stable save index. Cross-entity references (future Parent-style
// components) can persist as save indices and remap on load.
template <typename C>
inline void collect_one(World& world, std::vector<Entity>& out,
                        std::unordered_map<uint32_t, uint32_t>& index_of) {
    for (Entity e : world.view<C>()) {
        const uint32_t k = entity_key(e);
        if (index_of.find(k) == index_of.end()) {
            index_of.emplace(k, static_cast<uint32_t>(out.size()));
            out.push_back(e);
        }
    }
}

template <typename C>
inline void save_block(gws::serialize::BinaryWriter& w, World& world,
                       const std::unordered_map<uint32_t, uint32_t>& index_of) {
    const gws::reflect::TypeInfo& ti = *gws::reflect::reflect<C>();

    // Serialize the instances into a temp body first so we can length-prefix.
    std::vector<uint8_t> body;
    gws::serialize::BinaryWriter bw(body);
    uint64_t n = 0;
    for (Entity e : world.view<C>()) {
        const uint32_t idx = index_of.at(entity_key(e));
        bw.pod(idx);
        gws::serialize::write(bw, &world.get<C>(e), ti);
        ++n;
    }

    const uint64_t id  = ti.id;
    const uint64_t len = static_cast<uint64_t>(body.size());
    w.pod(id);
    w.pod(n);
    w.pod(len);
    if (len) w.write(body.data(), body.size());
}

using LoaderFn =
    std::function<void(gws::serialize::BinaryReader&, uint32_t, World&,
                       std::vector<Entity>&)>;

template <typename C>
inline void register_loader(std::unordered_map<uint64_t, LoaderFn>& loaders) {
    const uint64_t id = gws::reflect::reflect<C>()->id;
    loaders[id] = [](gws::serialize::BinaryReader& r, uint32_t save_idx,
                     World& world, std::vector<Entity>& ents) {
        C comp{};
        gws::serialize::read(r, &comp, *gws::reflect::reflect<C>());
        if (save_idx < ents.size()) world.add<C>(ents[save_idx], comp);
    };
}

}  // namespace detail

// Write `world` (the listed component types of every entity) to `w`.
template <typename... Components>
inline void save_world(gws::serialize::BinaryWriter& w, World& world) {
    std::vector<Entity> entities;
    std::unordered_map<uint32_t, uint32_t> index_of;
    (detail::collect_one<Components>(world, entities, index_of), ...);

    const uint32_t magic = kWorldSnapshotMagic;
    const uint32_t ver   = kWorldSnapshotVersion;
    const uint64_t ecount = entities.size();
    const uint32_t tcount = sizeof...(Components);
    w.pod(magic);
    w.pod(ver);
    w.pod(ecount);
    w.pod(tcount);

    (detail::save_block<Components>(w, world, index_of), ...);
}

// Load a snapshot into `world` (should be empty). Returns false on a bad/short
// blob. Component types not in <Components...> are skipped (block_byte_len).
template <typename... Components>
inline bool load_world(gws::serialize::BinaryReader& r, World& world) {
    uint32_t magic = 0, ver = 0;
    if (!r.pod(magic) || magic != kWorldSnapshotMagic) return false;
    if (!r.pod(ver)) return false;

    uint64_t entity_count = 0;
    uint32_t type_count   = 0;
    if (!r.pod(entity_count) || !r.pod(type_count)) return false;

    std::vector<Entity> ents;
    ents.reserve(static_cast<size_t>(entity_count));
    for (uint64_t i = 0; i < entity_count; ++i) ents.push_back(world.create());

    std::unordered_map<uint64_t, detail::LoaderFn> loaders;
    (detail::register_loader<Components>(loaders), ...);

    for (uint32_t t = 0; t < type_count; ++t) {
        uint64_t id = 0, n = 0, block_len = 0;
        if (!r.pod(id) || !r.pod(n) || !r.pod(block_len)) return false;

        auto it = loaders.find(id);
        if (it == loaders.end()) {              // unknown component type -> skip
            if (!r.skip(static_cast<size_t>(block_len))) return false;
            continue;
        }
        for (uint64_t k = 0; k < n; ++k) {
            uint32_t save_idx = 0;
            if (!r.pod(save_idx)) return false;
            it->second(r, save_idx, world, ents);
        }
    }
    return true;
}

}  // namespace schizo::ecs
