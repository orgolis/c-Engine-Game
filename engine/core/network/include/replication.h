#pragma once

// ====================
// Replication — ECS state sync over the Transport  (Stage 7 step 2)
// ====================
//
// The server snapshots every replicated entity (a NetId + its replicated
// components) and broadcasts it; clients apply snapshots to their own ECS world,
// keyed by NetId (server/client entt handles differ). Component bytes come from
// the reflection-driven archive (gws::serialize) — the single source of truth,
// so adding a replicated field needs no hand-written packet code.
//
// This is header-only and "bring your own ECS + serialization": the consumer
// links `network` (transport), `gws_ecs`, and `gws_serialization`, and calls
// `schizo::ecs::register_core_components()` once so the components reflect.
//
// First milestone: full-state snapshots of Transform. Delta-encoding + more
// component types + interpolation are follow-ups (the format already carries a
// tick + per-entity component length so those slot in).

#include "transport.h"

#include "ecs/world.h"
#include "ecs/components.h"
#include "serialization/serialization.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine::network {

inline constexpr uint32_t kSnapshotMagic   = 0x50414E53u;  // 'SNAP'
inline constexpr uint32_t kNetIdReplicated = 1u << 0;      // NetId::flags bit0

namespace detail {
template <typename T>
void put(std::vector<uint8_t>& b, const T& v) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
    b.insert(b.end(), p, p + sizeof(T));
}
template <typename T>
bool get(const uint8_t*& p, const uint8_t* end, T& out) {
    if (p + sizeof(T) > end) return false;
    std::memcpy(&out, p, sizeof(T));
    p += sizeof(T);
    return true;
}
}  // namespace detail

// Serialize all replicated entities (NetId flagged + Transform) into one buffer.
inline std::vector<uint8_t> write_snapshot(schizo::ecs::World& world, uint64_t tick) {
    std::vector<uint8_t> buf;
    detail::put(buf, kSnapshotMagic);
    detail::put(buf, tick);
    const size_t count_pos = buf.size();
    uint32_t count = 0;
    detail::put(buf, count);
    world.each<schizo::ecs::NetId, schizo::ecs::Transform>(
        [&](schizo::ecs::Entity, schizo::ecs::NetId& nid, schizo::ecs::Transform& t) {
            if (!(nid.flags & kNetIdReplicated)) return;
            detail::put(buf, nid.value);
            std::vector<uint8_t> tb = gws::serialize::save(t);
            detail::put(buf, static_cast<uint32_t>(tb.size()));
            buf.insert(buf.end(), tb.begin(), tb.end());
            ++count;
        });
    std::memcpy(&buf[count_pos], &count, sizeof(count));
    return buf;
}

// Interest-management (AOI) snapshot: like write_snapshot() but includes only
// entities for which keep(nid, transform) is true, followed by a trailing list
// of NetIds the receiver should DESPAWN (they left this client's interest set).
// The entity section is byte-for-byte a write_snapshot(); the removed list is
// appended, so a reader that stops after the entities just ignores it, and
// apply_snapshot() below consumes it when present — keeping the plain-snapshot
// path (editor NetSession / ReplicationServer) unchanged.
inline std::vector<uint8_t> write_snapshot_filtered(
        schizo::ecs::World& world, uint64_t tick,
        const std::function<bool(const schizo::ecs::NetId&, const schizo::ecs::Transform&)>& keep,
        const std::vector<uint64_t>& removed) {
    std::vector<uint8_t> buf;
    detail::put(buf, kSnapshotMagic);
    detail::put(buf, tick);
    const size_t count_pos = buf.size();
    uint32_t count = 0;
    detail::put(buf, count);
    world.each<schizo::ecs::NetId, schizo::ecs::Transform>(
        [&](schizo::ecs::Entity, schizo::ecs::NetId& nid, schizo::ecs::Transform& t) {
            if (!(nid.flags & kNetIdReplicated)) return;
            if (keep && !keep(nid, t)) return;
            detail::put(buf, nid.value);
            std::vector<uint8_t> tb = gws::serialize::save(t);
            detail::put(buf, static_cast<uint32_t>(tb.size()));
            buf.insert(buf.end(), tb.begin(), tb.end());
            ++count;
        });
    std::memcpy(&buf[count_pos], &count, sizeof(count));
    // Trailing removed-list section (NetIds to despawn on the client).
    detail::put(buf, static_cast<uint32_t>(removed.size()));
    for (uint64_t id : removed) detail::put(buf, id);
    return buf;
}

// Emit a snapshot from already-serialized component blobs — the wire format is
// identical to write_snapshot()/write_snapshot_filtered() (magic, tick, count,
// [netid, len, bytes]..., removed_count, [netid]...). Delta encoders diff in
// serialized-bytes space, so they hold the blobs already and would otherwise
// re-serialize; this lets them emit exactly the entities that changed. `changed`
// points at caller-owned blobs (not copied), so keep them alive across the call.
inline std::vector<uint8_t> write_snapshot_blobs(
        uint64_t tick,
        const std::vector<std::pair<uint64_t, const std::vector<uint8_t>*>>& changed,
        const std::vector<uint64_t>& removed) {
    std::vector<uint8_t> buf;
    detail::put(buf, kSnapshotMagic);
    detail::put(buf, tick);
    detail::put(buf, static_cast<uint32_t>(changed.size()));
    for (const auto& [nid, blob] : changed) {
        detail::put(buf, nid);
        detail::put(buf, static_cast<uint32_t>(blob->size()));
        buf.insert(buf.end(), blob->begin(), blob->end());
    }
    detail::put(buf, static_cast<uint32_t>(removed.size()));
    for (uint64_t id : removed) detail::put(buf, id);
    return buf;
}

// Apply a snapshot to a client world: find-or-create each entity by NetId and
// overwrite its replicated components. Returns the snapshot tick (0 on parse
// failure). `netid_map` persists across calls (NetId -> client entt entity).
inline uint64_t apply_snapshot(schizo::ecs::World& world,
                               std::unordered_map<uint64_t, schizo::ecs::Entity>& netid_map,
                               const std::vector<uint8_t>& bytes) {
    const uint8_t* p   = bytes.data();
    const uint8_t* end = bytes.data() + bytes.size();
    uint32_t magic = 0; uint64_t tick = 0; uint32_t count = 0;
    if (!detail::get(p, end, magic) || magic != kSnapshotMagic) return 0;
    if (!detail::get(p, end, tick)) return 0;
    if (!detail::get(p, end, count)) return 0;

    for (uint32_t i = 0; i < count; ++i) {
        uint64_t net_id = 0; uint32_t len = 0;
        if (!detail::get(p, end, net_id) || !detail::get(p, end, len)) break;
        if (p + len > end) break;

        auto it = netid_map.find(net_id);
        schizo::ecs::Entity e;
        if (it == netid_map.end()) {
            e = world.create();
            world.add<schizo::ecs::NetId>(e, schizo::ecs::NetId{net_id, 0, kNetIdReplicated});
            netid_map.emplace(net_id, e);
        } else {
            e = it->second;
        }
        schizo::ecs::Transform t;
        if (gws::serialize::load(p, len, t)) world.add<schizo::ecs::Transform>(e, t);
        p += len;
    }

    // Optional trailing removed-list (AOI despawns). Plain snapshots end after the
    // entities, so this read simply fails there and leaves the world untouched.
    uint32_t removed_count = 0;
    if (detail::get(p, end, removed_count)) {
        for (uint32_t i = 0; i < removed_count; ++i) {
            uint64_t rid = 0;
            if (!detail::get(p, end, rid)) break;
            auto it = netid_map.find(rid);
            if (it == netid_map.end()) continue;
            if (world.valid(it->second)) world.destroy(it->second);
            netid_map.erase(it);
        }
    }
    return tick;
}

// ---- Server / Client session wrappers over the Transport ----

class ReplicationServer {
public:
    bool start(uint16_t port, uint32_t max_clients = 16) {
        transport_ = create_enet_transport();
        return transport_ && transport_->start_server(port, max_clients);
    }
    // Drain connect/disconnect events; broadcast a fresh snapshot to all clients.
    void broadcast(schizo::ecs::World& world, uint64_t tick) {
        if (!transport_) return;
        NetEvent ev;
        while (transport_->poll(ev)) { /* connect/disconnect tracked by peer_count */ }
        const std::vector<uint8_t> snap = write_snapshot(world, tick);
        transport_->broadcast(snap.data(), snap.size(), Channel::Unreliable);
        transport_->flush();
    }
    size_t client_count() const { return transport_ ? transport_->peer_count() : 0; }

private:
    std::unique_ptr<Transport> transport_;
};

class ReplicationClient {
public:
    bool connect(const std::string& host, uint16_t port) {
        transport_ = create_enet_transport();
        if (!transport_ || !transport_->start_client()) return false;
        server_ = transport_->connect(host, port);
        return server_ != kInvalidPeer;
    }
    // Poll the transport; apply any received snapshots to `world`.
    void update(schizo::ecs::World& world) {
        if (!transport_) return;
        NetEvent ev;
        while (transport_->poll(ev)) {
            if (ev.type == NetEvent::Type::Connect)       connected_ = true;
            else if (ev.type == NetEvent::Type::Disconnect) connected_ = false;
            else if (ev.type == NetEvent::Type::Receive)  last_tick_ = apply_snapshot(world, netid_map_, ev.data);
        }
    }
    bool     connected() const { return connected_; }
    uint64_t last_tick() const { return last_tick_; }
    size_t   entity_count() const { return netid_map_.size(); }

private:
    std::unique_ptr<Transport> transport_;
    PeerId   server_    = kInvalidPeer;
    bool     connected_ = false;
    uint64_t last_tick_ = 0;
    std::unordered_map<uint64_t, schizo::ecs::Entity> netid_map_;
};

}  // namespace engine::network
