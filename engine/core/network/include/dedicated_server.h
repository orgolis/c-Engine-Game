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
//   tick(dt)            -> apply each client's latest input to its avatar
//                          (kinematic step; wrap physics/AI here later),
//                          advance the server tick, broadcast a world snapshot
//   on disconnect(peer) -> despawn the avatar
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
#include <unordered_map>

namespace engine::network {

// Client -> server input message: a magic tag + one InputPacket's fields. The
// tag lets the server ignore anything that isn't an input (future client msgs).
inline constexpr uint32_t kInputMagic = 0x54504E49u;  // 'INPT'
struct InputMsg { uint32_t magic; uint64_t tick; float move_x; float move_z; };

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
        //    (A real build steps Jolt physics + AI systems here inside the fixed
        //     step; the kinematic move keeps this dependency-light + deterministic.)
        for (auto& [peer, c] : clients_) {
            if (!c.has_input) continue;
            if (auto* t = world_.try_get<schizo::ecs::Transform>(c.entity)) {
                t->position.x += c.input.move_x * move_speed_ * dt;
                t->position.z += c.input.move_z * move_speed_ * dt;
            }
            c.acked_input_tick = static_cast<int64_t>(c.input.tick);
        }

        ++tick_;

        // 3) Broadcast one world snapshot (unreliable, high-rate state).
        const std::vector<uint8_t> snap = write_snapshot(world_, tick_);
        transport_->broadcast(snap.data(), snap.size(), Channel::Unreliable);
        transport_->flush();
    }

    schizo::ecs::World& world()          { return world_; }
    uint64_t            tick_count() const { return tick_; }
    size_t              client_count() const { return clients_.size(); }
    void                set_move_speed(float s) { move_speed_ = s; }

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
    };

    void on_connect(PeerId peer) {
        const schizo::ecs::Entity e = world_.create();
        const uint64_t nid = next_netid_++;
        world_.add<schizo::ecs::NetId>(e, schizo::ecs::NetId{nid, peer, kNetIdReplicated});
        schizo::ecs::Transform t;
        t.position = glm::vec3(1.5f * static_cast<float>(clients_.size()), 0.0f, 0.0f);  // spread avatars
        world_.add<schizo::ecs::Transform>(e, t);
        clients_[peer] = ClientState{e, nid};
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

    std::unique_ptr<Transport>                transport_;
    schizo::ecs::World                        world_;
    std::unordered_map<PeerId, ClientState>   clients_;
    uint64_t next_netid_ = 1;
    uint64_t tick_       = 0;
    float    move_speed_ = 5.0f;
};

}  // namespace engine::network
