#pragma once

// ====================
// Dedicated server — headless authoritative simulation  (Stage 7 step 4 / H3)
// ====================
//
// The productionization of the verified transport + replication + prediction
// pieces into a real server loop that runs with NO renderer/audio/window:
//
//   on connect(peer)    -> spawn a replicated avatar entity (NetId + Transform)
//   on receive(peer)    -> parse the client's InputMsg (magic-tagged), store it
//   tick(dt)            -> apply each client's latest input to its avatar,
//                          run the optional sim-step hook (physics/AI),
//                          advance the server tick, send each client a snapshot
//   on disconnect(peer) -> despawn the avatar
//
// Interest management (AOI): with set_aoi_radius(r>0) the server sends each client
// only the replicated entities within r of that client's avatar (plus the client's
// own avatar), and a despawn list for entities that just left its interest set —
// so bandwidth scales with what a client can see, not with world size. r<=0
// (default) keeps the simple full-world broadcast.
//
// Physics/AI are wired in via set_sim_step() rather than a hard dependency, so
// the `network` lib stays free of Jolt: the server *binary* links `gws_physics`
// and installs a hook that drives a PhysicsScene inside the fixed step.
//
// Header-only, "bring your own ECS + serialization": the consumer links
// `network` + `gws_ecs` + `gws_serialization`. Reuses write_snapshot() from
// replication.h and InputPacket from prediction.h, so the wire format matches
// the client side already verified by repl_check / prediction_check.

#include "transport.h"
#include "replication.h"   // write_snapshot, kNetIdReplicated
#include "prediction.h"    // InputPacket

#include "ecs/world.h"
#include "ecs/components.h"

#include <cstring>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace engine::network {

// Client -> server input message: a magic tag + one InputPacket's fields. The
// tag lets the server ignore anything that isn't an input (future client msgs).
inline constexpr uint32_t kInputMagic = 0x54504E49u;  // 'INPT'
struct InputMsg { uint32_t magic; uint64_t tick; float move_x; float move_z; };

// AOI: how many extra ticks a "leaving" entity's despawn is re-sent. Snapshots
// ride the Unreliable channel, so a one-shot removal could be dropped and strand
// a ghost on the client — repeating it a few ticks makes the despawn reliable.
inline constexpr int kAoiRemovalRedundancy = 4;

inline InputMsg make_input_msg(uint64_t tick, float move_x, float move_z) {
    return InputMsg{kInputMagic, tick, move_x, move_z};
}

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

        // 4) Send state snapshots (unreliable, high-rate): a per-client AOI slice
        //    when a radius is set, otherwise one full-world broadcast.
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

    // Interest management: replicate only entities within `r` of a client's avatar
    // (r<=0 disables AOI -> full-world broadcast, the default).
    void  set_aoi_radius(float r) { aoi_radius_ = r; }
    float aoi_radius() const      { return aoi_radius_; }
    // How many entities the server currently replicates to a peer (0 if unknown).
    size_t relevant_count(PeerId peer) const {
        auto it = clients_.find(peer);
        return it == clients_.end() ? 0 : it->second.relevant.size();
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
    struct ClientState {
        schizo::ecs::Entity entity{};
        uint64_t netid = 0;
        InputPacket input{};
        bool     has_input        = false;
        int64_t  acked_input_tick = -1;
        std::unordered_set<uint64_t>      relevant;  // netids currently replicated (AOI)
        std::unordered_map<uint64_t, int> leaving;   // netid -> ticks its despawn is still re-sent
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
        if (data.size() < sizeof(InputMsg)) return;
        InputMsg m{};
        std::memcpy(&m, data.data(), sizeof(m));
        if (m.magic != kInputMagic) return;
        auto it = clients_.find(peer);
        if (it == clients_.end()) return;
        it->second.input     = InputPacket{m.tick, m.move_x, m.move_z};
        it->second.has_input = true;
    }

    // Send this tick's state. AOI off -> one full-world broadcast. AOI on -> per
    // client, only entities within the radius of its avatar, plus a (redundant)
    // despawn list for entities that just left its interest set.
    void send_snapshots() {
        if (aoi_radius_ <= 0.0f) {
            const std::vector<uint8_t> snap = write_snapshot(world_, tick_);
            transport_->broadcast(snap.data(), snap.size(), Channel::Unreliable);
            return;
        }
        const float r2 = aoi_radius_ * aoi_radius_;
        for (auto& [peer, c] : clients_) {
            glm::vec3 center(0.0f);
            if (auto* ct = world_.try_get<schizo::ecs::Transform>(c.entity)) center = ct->position;

            // Which replicated entities are relevant to this client right now.
            std::unordered_set<uint64_t> now;
            world_.each<schizo::ecs::NetId, schizo::ecs::Transform>(
                [&](schizo::ecs::Entity, schizo::ecs::NetId& nid, schizo::ecs::Transform& t) {
                    if (!(nid.flags & kNetIdReplicated)) return;
                    const float dx = t.position.x - center.x;
                    const float dy = t.position.y - center.y;
                    const float dz = t.position.z - center.z;
                    if (nid.value == c.netid || (dx * dx + dy * dy + dz * dz) <= r2)
                        now.insert(nid.value);
                });

            // Entities that just left -> schedule a redundant despawn; anything
            // that (re)entered cancels a pending despawn.
            for (uint64_t id : c.relevant)
                if (!now.count(id)) c.leaving[id] = kAoiRemovalRedundancy;
            for (uint64_t id : now) c.leaving.erase(id);

            std::vector<uint64_t> removed;
            removed.reserve(c.leaving.size());
            for (auto lit = c.leaving.begin(); lit != c.leaving.end();) {
                removed.push_back(lit->first);
                if (--lit->second <= 0) lit = c.leaving.erase(lit);
                else                    ++lit;
            }

            auto keep = [&now](const schizo::ecs::NetId& nid, const schizo::ecs::Transform&) {
                return now.find(nid.value) != now.end();
            };
            const std::vector<uint8_t> snap = write_snapshot_filtered(world_, tick_, keep, removed);
            transport_->send(peer, snap.data(), snap.size(), Channel::Unreliable);
            c.relevant = std::move(now);
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
