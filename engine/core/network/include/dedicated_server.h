#pragma once

// ====================
// Dedicated server — headless authoritative simulation  (Stage 7 step 4 / H3)
// ====================
//
// The productionization of the verified transport + replication + prediction
// pieces into a real server loop that runs with NO renderer/audio/window:
//
//   on connect(peer)    -> spawn a replicated avatar entity (NetId + Transform)
//   on receive(peer)    -> parse the client's InputMsg or AckMsg (magic-tagged)
//   tick(dt)            -> apply each client's latest input to its avatar,
//                          run the optional sim-step hook (physics/AI),
//                          advance the server tick, send each client a delta
//   on disconnect(peer) -> despawn the avatar
//
// Interest management (AOI): with set_aoi_radius(r>0) a client is only relevant to
// entities within r of its avatar (plus its own avatar); r<=0 (default) makes the
// whole replicated world relevant.
//
// Delta-encoding (ack-based): each client acks the tick it last received; the
// server keeps that acknowledged state as a per-client BASELINE and sends only the
// entities whose serialized component bytes differ from it, plus a despawn list for
// baseline entities no longer relevant. Because the baseline only advances on an
// ack, every change (and despawn) keeps being re-sent until the client confirms it
// — so the scheme is self-healing over the Unreliable snapshot channel with no
// separate redundancy counter. A client that never acks simply keeps getting full
// state each tick (delta against an empty baseline), so it degrades gracefully.
//
// Physics/AI are wired in via set_sim_step() rather than a hard dependency, so
// the `network` lib stays free of Jolt: the server *binary* links `gws_physics`
// and installs a hook that drives a PhysicsScene inside the fixed step.
//
// Header-only, "bring your own ECS + serialization": the consumer links
// `network` + `gws_ecs` + `gws_serialization`. Reuses the replication.h snapshot
// format + InputPacket from prediction.h, so the wire matches the client side
// already verified by repl_check / prediction_check.

#include "transport.h"
#include "replication.h"   // write_snapshot_blobs, kNetIdReplicated
#include "prediction.h"    // InputPacket

#include "ecs/world.h"
#include "ecs/components.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cstdint>

namespace engine::network {

// Client -> server input message: a magic tag + one InputPacket's fields. The
// tag lets the server ignore anything that isn't an input (future client msgs).
inline constexpr uint32_t kInputMagic = 0x54504E49u;  // 'INPT'
struct InputMsg { uint32_t magic; uint64_t tick; float move_x; float move_z; };

// Client -> server acknowledgement: "I have applied snapshot tick T". Advances the
// server's per-client delta baseline. Sent on the Reliable channel.
inline constexpr uint32_t kAckMagic = 0x314B4341u;  // 'ACK1'
struct AckMsg { uint32_t magic; uint64_t tick; };

// Cap on retained per-client sent-state history (ticks kept until acked). A client
// that stops acking just falls back to full snapshots; this bounds the memory.
inline constexpr size_t kMaxSnapshotHistory = 256;

inline InputMsg make_input_msg(uint64_t tick, float move_x, float move_z) {
    return InputMsg{kInputMagic, tick, move_x, move_z};
}
inline AckMsg make_ack_msg(uint64_t tick) { return AckMsg{kAckMagic, tick}; }

class DedicatedServer {
public:
    bool start(uint16_t port, uint32_t max_clients = 16) {
        schizo::ecs::register_core_components();   // so NetId/Transform reflect + serialize
        transport_ = create_enet_transport();
        return transport_ && transport_->start_server(port, max_clients);
    }

    // Advance the authoritative simulation by one fixed tick.
    void tick(float dt) {
        if (!transport_) return;

        // 1) Drain transport: connects spawn avatars, inputs update per-client state.
        NetEvent ev;
        while (transport_->poll(ev)) {
            switch (ev.type) {
                case NetEvent::Type::Connect:    on_connect(ev.peer);            break;
                case NetEvent::Type::Disconnect: on_disconnect(ev.peer);         break;
                case NetEvent::Type::Receive:    on_receive(ev.peer, ev.data);   break;
                default: break;
            }
        }

        // 2) Apply each client's latest input to its avatar (authoritative move).
        for (auto& [peer, c] : clients_) {
            if (!c.has_input) continue;
            if (auto* t = world_.try_get<schizo::ecs::Transform>(c.entity)) {
                t->position.x += c.input.move_x * move_speed_ * dt;
                t->position.z += c.input.move_z * move_speed_ * dt;
            }
            c.acked_input_tick = static_cast<int64_t>(c.input.tick);
        }

        // 3) Step world systems (physics/AI) inside the fixed tick, if installed.
        //    Kept as a hook so the network lib carries no Jolt dependency.
        if (sim_step_) sim_step_(world_, dt);

        ++tick_;

        // 4) Send each client a delta (unreliable, high-rate): only the relevant
        //    entities whose bytes changed since that client's acked baseline.
        send_snapshots();
        transport_->flush();
    }

    schizo::ecs::World& world()          { return world_; }
    uint64_t            tick_count() const { return tick_; }
    size_t              client_count() const { return clients_.size(); }
    void                set_move_speed(float s) { move_speed_ = s; }

    // Optional per-tick world simulation (physics/AI). Runs each fixed tick after
    // inputs are applied and before the snapshot. The `network` lib stays Jolt-free;
    // the server binary installs a hook that drives a PhysicsScene here.
    using SimStepFn = std::function<void(schizo::ecs::World&, float /*dt*/)>;
    void set_sim_step(SimStepFn fn) { sim_step_ = std::move(fn); }

    // Interest management: an entity is relevant to a client only within `r` of its
    // avatar (r<=0 -> the whole replicated world is relevant, the default).
    void  set_aoi_radius(float r) { aoi_radius_ = r; }
    float aoi_radius() const      { return aoi_radius_; }

    // Entities a peer is confirmed to hold (its acked baseline size); 0 if unknown.
    size_t relevant_count(PeerId peer) const {
        auto it = clients_.find(peer);
        return it == clients_.end() ? 0 : it->second.baseline.size();
    }
    // Introspection for the last delta the server built for a peer (test/debug).
    size_t last_delta_entities(PeerId peer) const {
        auto it = clients_.find(peer);
        return it == clients_.end() ? 0 : it->second.last_delta.size();
    }
    bool in_last_delta(PeerId peer, uint64_t netid) const {
        auto it = clients_.find(peer);
        if (it == clients_.end()) return false;
        const auto& d = it->second.last_delta;
        return std::find(d.begin(), d.end(), netid) != d.end();
    }

    // Last input tick the server applied for a peer (-1 if none / unknown).
    int64_t acked_input_tick(PeerId peer) const {
        auto it = clients_.find(peer);
        return it == clients_.end() ? -1 : it->second.acked_input_tick;
    }
    // The peer's avatar NetId (0 if unknown) — lets a client identify itself.
    uint64_t avatar_netid(PeerId peer) const {
        auto it = clients_.find(peer);
        return it == clients_.end() ? 0 : it->second.netid;
    }

private:
    // Serialized replicated state keyed by NetId (one component blob per entity).
    using StateMap = std::unordered_map<uint64_t, std::vector<uint8_t>>;

    struct ClientState {
        schizo::ecs::Entity entity{};
        uint64_t netid = 0;
        InputPacket input{};
        bool     has_input        = false;
        int64_t  acked_input_tick = -1;
        // Delta-encoding: baseline = state the client has acknowledged; history =
        // states sent since, kept until an ack advances the baseline to one of them.
        StateMap                    baseline;
        uint64_t                    baseline_tick = 0;
        std::map<uint64_t, StateMap> history;
        std::vector<uint64_t>       last_delta;   // netids in the most recent delta
    };

    void on_connect(PeerId peer) {
        const schizo::ecs::Entity e = world_.create();
        const uint64_t nid = next_netid_++;
        world_.add<schizo::ecs::NetId>(e, schizo::ecs::NetId{nid, peer, kNetIdReplicated});
        schizo::ecs::Transform t;
        t.position = glm::vec3(1.5f * static_cast<float>(clients_.size()), 0.0f, 0.0f);  // spread avatars
        world_.add<schizo::ecs::Transform>(e, t);
        ClientState cs;
        cs.entity = e;
        cs.netid  = nid;
        clients_[peer] = std::move(cs);
    }

    void on_disconnect(PeerId peer) {
        auto it = clients_.find(peer);
        if (it == clients_.end()) return;
        if (world_.valid(it->second.entity)) world_.destroy(it->second.entity);
        clients_.erase(it);
    }

    void on_receive(PeerId peer, const std::vector<uint8_t>& data) {
        if (data.size() < sizeof(uint32_t)) return;
        uint32_t magic = 0;
        std::memcpy(&magic, data.data(), sizeof(magic));
        auto it = clients_.find(peer);
        if (it == clients_.end()) return;

        if (magic == kInputMagic && data.size() >= sizeof(InputMsg)) {
            InputMsg m{};
            std::memcpy(&m, data.data(), sizeof(m));
            it->second.input     = InputPacket{m.tick, m.move_x, m.move_z};
            it->second.has_input = true;
        } else if (magic == kAckMagic && data.size() >= sizeof(AckMsg)) {
            AckMsg a{};
            std::memcpy(&a, data.data(), sizeof(a));
            on_ack(it->second, a.tick);
        }
    }

    // Advance a client's delta baseline to the acked tick's sent state (if we still
    // have it), then drop the now-superseded history.
    void on_ack(ClientState& c, uint64_t acked_tick) {
        if (acked_tick <= c.baseline_tick) return;
        auto h = c.history.find(acked_tick);
        if (h == c.history.end()) return;         // too old / never sent — ignore
        c.baseline      = h->second;
        c.baseline_tick = acked_tick;
        c.history.erase(c.history.begin(), c.history.upper_bound(acked_tick));
    }

    // Serialize the entities relevant to a client (AOI-filtered) into a StateMap of
    // netid -> component bytes. Relevance = within the radius of the client's avatar
    // (its own avatar always included); radius<=0 means the whole world is relevant.
    StateMap relevant_state(const ClientState& c) {
        glm::vec3 center(0.0f);
        const bool aoi = aoi_radius_ > 0.0f;
        if (aoi) if (auto* ct = world_.try_get<schizo::ecs::Transform>(c.entity)) center = ct->position;
        const float r2 = aoi_radius_ * aoi_radius_;

        StateMap cur;
        world_.each<schizo::ecs::NetId, schizo::ecs::Transform>(
            [&](schizo::ecs::Entity, schizo::ecs::NetId& nid, schizo::ecs::Transform& t) {
                if (!(nid.flags & kNetIdReplicated)) return;
                if (aoi && nid.value != c.netid) {
                    const float dx = t.position.x - center.x;
                    const float dy = t.position.y - center.y;
                    const float dz = t.position.z - center.z;
                    if (dx * dx + dy * dy + dz * dz > r2) return;
                }
                cur.emplace(nid.value, gws::serialize::save(t));
            });
        return cur;
    }

    // Send each client a delta: entities whose bytes differ from its acked baseline,
    // plus a despawn list for baseline entities no longer relevant. Nothing to say
    // (no changes, no despawns) -> no packet, the real bandwidth win.
    void send_snapshots() {
        for (auto& [peer, c] : clients_) {
            StateMap cur = relevant_state(c);

            std::vector<std::pair<uint64_t, const std::vector<uint8_t>*>> changed;
            for (const auto& [id, blob] : cur) {
                auto b = c.baseline.find(id);
                if (b == c.baseline.end() || b->second != blob) changed.emplace_back(id, &blob);
            }
            std::vector<uint64_t> removed;
            for (const auto& [id, blob] : c.baseline)
                if (cur.find(id) == cur.end()) removed.push_back(id);

            // Record what this delta covers (introspection) regardless of sending.
            c.last_delta.clear();
            c.last_delta.reserve(changed.size());
            for (const auto& [id, blob] : changed) c.last_delta.push_back(id);

            if (changed.empty() && removed.empty()) continue;  // baseline already current

            const std::vector<uint8_t> snap = write_snapshot_blobs(tick_, changed, removed);
            transport_->send(peer, snap.data(), snap.size(), Channel::Unreliable);

            c.history[tick_] = std::move(cur);
            while (c.history.size() > kMaxSnapshotHistory) c.history.erase(c.history.begin());
        }
    }

    std::unique_ptr<Transport>                transport_;
    schizo::ecs::World                        world_;
    std::unordered_map<PeerId, ClientState>   clients_;
    SimStepFn sim_step_;
    uint64_t next_netid_ = 1;
    uint64_t tick_       = 0;
    float    move_speed_ = 5.0f;
    float    aoi_radius_ = 0.0f;   // <=0 disables AOI (full-world broadcast)
};

}  // namespace engine::network
