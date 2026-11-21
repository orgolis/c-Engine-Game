// ============================================================
// NetSession implementation — see net_session.h
// ============================================================
//
// First-person multiplayer model:
//   - PLAYERS are client-authoritative: each instance runs Play mode, owns its
//     own first-person character, and reports its world position every frame.
//     The host relays everyone's positions; every instance renders the OTHER
//     players as avatar proxies cloned from the scene's "Player" entity visuals.
//   - PROPS are host-authoritative: the host mirrors every dynamic-collider
//     scene entity's world transform into the snapshot; clients apply them onto
//     their matching entities (matched by scene-order index) AFTER their local
//     physics update, so the host's simulation wins on every screen.
//
// Packets are self-identifying by a leading 4-byte magic:
//   - kSnapshotMagic : host -> clients, props + avatars (replication.h)
//   - kMsgAssign     : host -> one client, "your avatar is NetId N"
//   - kMsgPlayerPos  : client -> host, this client's player world position

#include "net_session.h"
#include <utility>   // std::as_const: reads must not stamp the entity

#include "replication.h"          // write_snapshot / apply_snapshot + kSnapshotMagic
#include "transport.h"
#include "lobby_client.h"         // host_lobby: lobby codes through a relay
#include "lobby_direct.h"         // host_direct / join_direct: codes without a server
#include "ecs/world.h"
#include "ecs/components.h"

#include "scene.h"
#include "entity.h"
#include "transform.h"
#include "entity_factory.h"
#include "mesh_renderer_component.h"
#include "collider_component.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>
#include <cmath>

namespace schizo::editor {

namespace net = engine::network;
namespace ecs = schizo::ecs;

namespace {
// Shared scene entities (props): 1-based index in the scene entity list. Both
// instances load the same scene file in the same order, so index N is the same
// logical entity everywhere (runtime-spawned entities only ever APPEND).
inline constexpr uint64_t kSceneNetIdBase = 1;
// Player avatars use a high NetId range, well clear of any scene index.
inline constexpr uint64_t kAvatarNetIdBase = 1'000'000ull;

inline constexpr uint32_t kMsgAssign    = 0x4E475341u;  // "ASGN"
inline constexpr uint32_t kMsgPlayerPos = 0x534F5050u;  // "PPOS"
inline constexpr uint32_t kMsgGame = 0x314D474Fu;

void ensure_components_registered() {
    static bool done = false;
    if (!done) { ecs::register_core_components(); done = true; }
}

glm::vec4 avatar_color(uint64_t netid) {
    static const glm::vec4 palette[8] = {
        {0.90f, 0.30f, 0.30f, 1.0f}, {0.30f, 0.55f, 0.95f, 1.0f},
        {0.35f, 0.90f, 0.45f, 1.0f}, {0.95f, 0.85f, 0.35f, 1.0f},
        {0.80f, 0.40f, 0.95f, 1.0f}, {0.35f, 0.92f, 0.92f, 1.0f},
        {0.95f, 0.60f, 0.30f, 1.0f}, {0.75f, 0.75f, 0.78f, 1.0f},
    };
    return palette[static_cast<uint32_t>(netid - kAvatarNetIdBase) % 8u];
}

// Copy the render-relevant components of `src` onto `dst` (mesh asset path +
// primitive MeshRenderer). Colliders / cameras / audio are deliberately NOT
// copied — proxies are visual-only.
void copy_visuals(const std::shared_ptr<schizo::scene::Entity>& src,
                  const std::shared_ptr<schizo::scene::Entity>& dst) {
    if (const auto* skin=std::as_const(*src).GetSkinnedMeshComponent();skin&&!skin->gltf_path.empty())
        *dst->GetSkinnedMeshComponent()=*skin;
    if (const auto* mc = std::as_const(*src).GetMeshComponent(); mc && !mc->mesh_path.empty())
        dst->SetMesh(mc->mesh_path);
    if (auto mr = src->GetComponent<schizo::scene::MeshRendererComponent>()) {
        auto out = dst->AddComponent<schizo::scene::MeshRendererComponent>(
            mr->GetMeshType(), mr->GetColor());
        if (out) {
            out->SetMetallic(mr->GetMetallic());
            out->SetRoughness(mr->GetRoughness());
            out->SetOcclusion(mr->GetOcclusion());
            out->SetEmissive(mr->GetEmissive());
            out->SetEmissiveIntensity(mr->GetEmissiveIntensity());
            out->SetAlphaMode(mr->GetAlphaMode());
            out->SetAlphaCutoff(mr->GetAlphaCutoff());
        }
    }
}

bool has_visuals(const std::shared_ptr<schizo::scene::Entity>& e) {
    if (const auto* skin=std::as_const(*e).GetSkinnedMeshComponent();skin&&!skin->gltf_path.empty()) return true;
    if (const auto* mc = std::as_const(*e).GetMeshComponent(); mc && !mc->mesh_path.empty()) return true;
    return e->GetComponent<schizo::scene::MeshRendererComponent>() != nullptr;
}
}  // namespace

struct NetSession::Impl {
    std::vector<NetGameMessage> messages;
    std::unordered_set<uint64_t> removed_ids;
    std::unique_ptr<net::Transport> transport;
    std::unique_ptr<net::lobby::HostLink> lobby;   // host_lobby(): the lobby riding on `transport`
    std::unique_ptr<net::lobby::DirectHost> direct_host;   // host_direct()
    std::unique_ptr<net::lobby::DirectJoin> direct_join;   // join_direct(), until it connects
    // Both stop before the transport they ride on goes.
    void stop_lobbies() {
        if (lobby) lobby->stop();
        lobby.reset();
        if (direct_host) direct_host->stop();
        direct_host.reset();
        if (direct_join) direct_join->stop();
        direct_join.reset();
    }

    ecs::World net_world;                                     // props + avatars
    std::unordered_map<uint64_t, ecs::Entity> ent_by_netid;   // host: prop mirror
    std::unordered_map<uint64_t, ecs::Entity> netid_map;      // client: apply_snapshot map

    net::PeerId server_peer = net::kInvalidPeer;
    uint64_t    tick        = 0;
    bool        connected   = false;
    std::weak_ptr<schizo::scene::Scene> last_scene;

    glm::vec3 local_player_pos{0.0f};                         // fed each frame
    float     local_player_yaw = 0.0f;                        // radians about +Y (0: facing +Z)
    std::unordered_map<net::PeerId, float> peer_yaw;          // host: each client's, as it last said
    uint32_t  local_player_id = 0;                            // scene entity id (0 = none)
    uint64_t  my_avatar = 0;                                  // our own avatar NetId

    // Host authoritative state:
    std::unordered_map<net::PeerId, uint64_t>  avatar_by_peer;
    std::unordered_map<net::PeerId, glm::vec3> peer_pos;
    std::unordered_map<uint64_t, ecs::Entity>  avatar_ent;
    uint32_t next_client_index = 1;                           // host itself is 0

    // Rendering: OOP proxy per OTHER player's avatar. Each proxy is a small
    // entity tree cloned from the scene "Player"'s visuals (root first).
    std::unordered_map<uint64_t, std::vector<std::shared_ptr<schizo::scene::Entity>>> proxy;
    std::unordered_set<uint32_t> proxy_ids;                   // every proxy entity id

    // Accumulated floating-origin shift of THIS peer. Replicated
    // positions are absolute and every peer rebases on its own camera,
    // so this is what converts between the wire frame and ours.
    glm::vec3 origin_offset{0.0f};

    ecs::Entity ensure_avatar_entity(uint64_t netid, uint32_t owner, const glm::vec3& pos) {
        auto it = avatar_ent.find(netid);
        if (it != avatar_ent.end()) return it->second;
        ecs::Entity e = net_world.create();
        net_world.add<ecs::NetId>(e, ecs::NetId{netid, owner, net::kNetIdReplicated});
        ecs::Transform t; t.position = pos;
        net_world.add<ecs::Transform>(e, t);
        avatar_ent.emplace(netid, e);
        return e;
    }

    void set_avatar_pos(uint64_t netid, const glm::vec3& pos, float yaw) {
        auto it = avatar_ent.find(netid);
        if (it == avatar_ent.end() || !net_world.valid(it->second)) return;
        if (auto* t = net_world.try_get<ecs::Transform>(it->second)) {
            t->position = pos;
            t->rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));   // the way it faces, for everyone
        }
    }

    // Build a proxy for a remote player: clone the scene "Player" entity's
    // visual tree (root visuals + each visual child, e.g. the "PlayerMesh"
    // capsule — cameras/colliders skipped). Falls back to a coloured cube when
    // the scene has no Player. Returns the created entities, root first.
    std::vector<std::shared_ptr<schizo::scene::Entity>> make_avatar_proxy(
        const std::shared_ptr<schizo::scene::Scene>& scene, uint64_t netid) {
        std::vector<std::shared_ptr<schizo::scene::Entity>> out;
        const uint32_t idx = static_cast<uint32_t>(netid - kAvatarNetIdBase);
        const std::string name = "[net] Player " + std::to_string(idx);

        auto src = scene->GetEntityByName("Player");
        if (src) {
            auto root = scene->CreateEntity(name);
            if (!root) return out;
            if (auto* t = root->GetTransform())
                t->SetLocalScale(src->GetTransform()->GetLocalScale());
            copy_visuals(src, root);
            out.push_back(root);
            bool any_visual = has_visuals(src);
            for (const auto& child : src->GetChildren()) {
                if (!child || !has_visuals(child)) continue;   // skips cameras etc.
                auto c = scene->CreateEntity(name + ":" + child->GetName());
                if (!c) continue;
                c->SetParent(root);
                if (auto* ct = c->GetTransform()) {
                    const auto* st = child->GetTransform();
                    ct->SetLocalPosition(st->GetLocalPosition());
                    ct->SetLocalRotation(st->GetLocalRotation());
                    ct->SetLocalScale(st->GetLocalScale());
                }
                copy_visuals(child, c);
                out.push_back(c);
                any_visual = true;
            }
            // Player had no renderable part at all — give the proxy a visible
            // fallback body so remote players are never invisible.
            if (!any_visual) {
                root->AddComponent<schizo::scene::MeshRendererComponent>(
                    schizo::scene::MeshType::Capsule, avatar_color(netid));
            }
            return out;
        }
        // No Player entity in the scene: coloured cube fallback.
        if (auto cube = schizo::scene::EntityFactory::CreateCube(
                scene, name, 1.0f, avatar_color(netid)))
            out.push_back(cube);
        return out;
    }

    // Create/update/remove proxies for every avatar EXCEPT our own.
    void sync_avatar_proxies(const std::shared_ptr<schizo::scene::Scene>& scene,
                             size_t& out_count) {
        out_count = 0;
        if (!scene) return;
        std::unordered_set<uint64_t> seen;
        net_world.each<ecs::NetId, ecs::Transform>(
            [&](ecs::Entity, ecs::NetId& nid, ecs::Transform& t) {
                if (nid.value < kAvatarNetIdBase) return;
                if (nid.value == my_avatar) return;            // don't render self
                seen.insert(nid.value);
                ++out_count;
                auto pit = proxy.find(nid.value);
                if (pit == proxy.end()) {
                    auto ents = make_avatar_proxy(scene, nid.value);
                    if (ents.empty()) return;
                    for (const auto& e : ents) proxy_ids.insert(e->GetId());
                    pit = proxy.emplace(nid.value, std::move(ents)).first;
                }
                if (!pit->second.empty())
                    if (auto* tf = pit->second.front()->GetTransform()) {
                        tf->SetWorldPosition(t.position + origin_offset);   // -> local frame
                        tf->SetWorldRotation(t.rotation);                    // the way that player faces
                    }
            });
        for (auto it = proxy.begin(); it != proxy.end();) {
            if (seen.count(it->first) == 0) {
                for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit)
                    if (*rit) { proxy_ids.erase((*rit)->GetId()); scene->RemoveEntity(*rit); }
                it = proxy.erase(it);
            } else { ++it; }
        }
    }

    void remove_all_proxies(const std::shared_ptr<schizo::scene::Scene>& scene) {
        if (scene)
            for (auto& [id, ents] : proxy)
                for (auto rit = ents.rbegin(); rit != ents.rend(); ++rit)
                    if (*rit) scene->RemoveEntity(*rit);
        proxy.clear();
        proxy_ids.clear();
    }

    // Should this scene entity be replicated as a host-authoritative prop?
    // Only dynamic-collider entities (the things physics moves) qualify; the
    // local player and net proxies never do.
    bool is_prop(const std::shared_ptr<schizo::scene::Entity>& ent) const {
        if (!ent || !ent->IsActiveInHierarchy()) return false;
        if (ent->GetId() == local_player_id) return false;
        if (proxy_ids.count(ent->GetId())) return false;
        auto col = ent->GetComponent<schizo::scene::ColliderComponent>();
        return col && col->IsDynamic();
    }

    // ================= HOST =================
    void tick_host(const std::shared_ptr<schizo::scene::Scene>& scene, float /*dt*/,
                   NetSessionStatus& st) {
        my_avatar = kAvatarNetIdBase;
        ensure_avatar_entity(my_avatar, 0, local_player_pos);
        set_avatar_pos(my_avatar, local_player_pos, local_player_yaw);

        net::NetEvent ev;
        while (transport->poll(ev)) {
            if (ev.type == net::NetEvent::Type::Connect) {
                const uint64_t aid = kAvatarNetIdBase + next_client_index;
                avatar_by_peer[ev.peer] = aid;
                ensure_avatar_entity(aid, static_cast<uint32_t>(ev.peer), local_player_pos);
                ++next_client_index;
                std::vector<uint8_t> msg(sizeof(uint32_t) + sizeof(uint64_t));
                std::memcpy(msg.data(), &kMsgAssign, sizeof(uint32_t));
                std::memcpy(msg.data() + sizeof(uint32_t), &aid, sizeof(uint64_t));
                transport->send(ev.peer, msg.data(), msg.size(), net::Channel::Reliable);
                spdlog::info("[net] client peer {} joined -> avatar {}", ev.peer, aid);
            } else if (ev.type == net::NetEvent::Type::Disconnect) {
                auto it = avatar_by_peer.find(ev.peer);
                if (it != avatar_by_peer.end()) {
                    removed_ids.insert(it->second);
                    auto ae = avatar_ent.find(it->second);
                    if (ae != avatar_ent.end()) {
                        if (net_world.valid(ae->second)) net_world.destroy(ae->second);
                        avatar_ent.erase(ae);
                    }
                    avatar_by_peer.erase(it);
                }
                peer_pos.erase(ev.peer);
                peer_yaw.erase(ev.peer);
                spdlog::info("[net] client peer {} left", ev.peer);
            } else if (ev.type == net::NetEvent::Type::Receive) {
                if (ev.data.size() >= sizeof(uint32_t)) {
                    uint32_t magic = 0;
                    std::memcpy(&magic, ev.data.data(), sizeof(magic));
                    if(magic==kMsgGame&&ev.data.size()<=262148&&messages.size()<256)
                        messages.push_back({ev.peer,{ev.data.begin()+4,ev.data.end()}});
                    if (magic == kMsgPlayerPos &&
                        ev.data.size() >= sizeof(uint32_t) + 3 * sizeof(float)) {
                        float p[3];
                        std::memcpy(p, ev.data.data() + sizeof(uint32_t), 3 * sizeof(float));
                        if(std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2])&&std::abs(p[0])<1000000&&std::abs(p[1])<1000000&&std::abs(p[2])<1000000)
                            peer_pos[ev.peer] = glm::vec3(p[0], p[1], p[2]);
                        else transport->disconnect(ev.peer);
                        // The way it faces, when its engine sends it (a fourth float).
                        if (ev.data.size() >= sizeof(uint32_t) + 4 * sizeof(float)) {
                            float y = 0.0f;
                            std::memcpy(&y, ev.data.data() + sizeof(uint32_t) + 3 * sizeof(float), sizeof(float));
                            if (std::isfinite(y) && std::abs(y) < 100.0f) peer_yaw[ev.peer] = y;
                        }
                    }
                }
            }
        }

        for (auto& [peer, aid] : avatar_by_peer) {
            auto pi = peer_pos.find(peer);
            if (pi != peer_pos.end()) {
                auto yi = peer_yaw.find(peer);
                set_avatar_pos(aid, pi->second, yi != peer_yaw.end() ? yi->second : 0.0f);
            }
        }

        // Mirror moving props (dynamic-collider scene entities) into net_world
        // so clients see the host's simulation. Matched by scene-order index.
        std::unordered_set<uint64_t> present;
        size_t props = 0;
        if (scene) {
            const auto& ents = scene->GetEntities();
            for (size_t i = 0; i < ents.size(); ++i) {
                if (!is_prop(ents[i])) continue;
                auto* tf = ents[i]->GetTransform();
                if (!tf) continue;
                const uint64_t nid = kSceneNetIdBase + static_cast<uint64_t>(i);
                removed_ids.erase(nid);
                present.insert(nid);
                ecs::Entity e;
                auto it = ent_by_netid.find(nid);
                if (it == ent_by_netid.end()) {
                    e = net_world.create();
                    net_world.add<ecs::NetId>(e, ecs::NetId{nid, 0, net::kNetIdReplicated});
                    ent_by_netid.emplace(nid, e);
                } else {
                    e = it->second;
                }
                ecs::Transform t;
                t.position = tf->GetWorldPosition() - origin_offset;   // -> wire frame
                t.rotation = tf->GetWorldRotation();
                t.scale    = tf->GetWorldScale();
                net_world.add<ecs::Transform>(e, t);
                ++props;
            }
        }
        for (auto it = ent_by_netid.begin(); it != ent_by_netid.end();) {
            if (present.count(it->first) == 0) {
                removed_ids.insert(it->first);
                if (net_world.valid(it->second)) net_world.destroy(it->second);
                it = ent_by_netid.erase(it);
            } else { ++it; }
        }

        std::vector<uint8_t> snap = net::write_snapshot(net_world, tick);
        uint32_t removed=uint32_t(removed_ids.size());const auto* count_bytes=reinterpret_cast<const uint8_t*>(&removed);snap.insert(snap.end(),count_bytes,count_bytes+4);
        for(auto id:removed_ids){const auto* bytes=reinterpret_cast<const uint8_t*>(&id);snap.insert(snap.end(),bytes,bytes+8);}
        transport->broadcast(snap.data(), snap.size(), net::Channel::Unreliable);
        transport->flush();

        size_t avatars = 0;
        sync_avatar_proxies(scene, avatars);

        const net::NetStats ns = transport->stats();
        st.peer_count = transport->peer_count();
        st.connected  = st.peer_count > 0;
        st.tick       = tick;
        st.bytes_sent = ns.bytes_sent;
        st.bytes_recv = ns.bytes_recv;
        st.replicated = props;
        st.avatars    = avatars;
        st.my_avatar  = my_avatar;
        ++tick;
    }

    // ================= CLIENT =================
    void tick_client(const std::shared_ptr<schizo::scene::Scene>& scene, float /*dt*/,
                     NetSessionStatus& st) {
        net::NetEvent ev;
        while (transport->poll(ev)) {
            switch (ev.type) {
                case net::NetEvent::Type::Connect:    connected = true;  break;
                case net::NetEvent::Type::Disconnect: connected = false; break;
                case net::NetEvent::Type::Receive: {
                    if (ev.data.size() < sizeof(uint32_t)) break;
                    uint32_t magic = 0;
                    std::memcpy(&magic, ev.data.data(), sizeof(magic));
                    if(magic==kMsgGame&&ev.data.size()<=262148&&messages.size()<256)
                        messages.push_back({ev.peer,{ev.data.begin()+4,ev.data.end()}});
                    if (magic == net::kSnapshotMagic) {
                        uint64_t incoming=0;if(ev.data.size()>=12)std::memcpy(&incoming,ev.data.data()+4,8);if(incoming<=tick)break;
                        tick = net::apply_snapshot(net_world, netid_map, ev.data);
                    } else if (magic == kMsgAssign &&
                               ev.data.size() >= sizeof(uint32_t) + sizeof(uint64_t)) {
                        std::memcpy(&my_avatar, ev.data.data() + sizeof(uint32_t), sizeof(uint64_t));
                        spdlog::info("[net] assigned avatar {}", my_avatar);
                    }
                    break;
                }
                default: break;
            }
        }

        if (connected && server_peer != net::kInvalidPeer) {
            std::vector<uint8_t> msg(sizeof(uint32_t) + 4 * sizeof(float));
            std::memcpy(msg.data(), &kMsgPlayerPos, sizeof(uint32_t));
            float p[4] = { local_player_pos.x, local_player_pos.y, local_player_pos.z, local_player_yaw };
            std::memcpy(msg.data() + sizeof(uint32_t), p, 4 * sizeof(float));
            transport->send(server_peer, msg.data(), msg.size(), net::Channel::Unreliable);
            transport->flush();
        }

        // Apply host-authoritative prop transforms onto our matching scene
        // entities. Runs AFTER our local physics update this frame (tick order
        // in the main loop), so the host's positions win on screen.
        size_t props = 0;
        if (scene) {
            const auto& ents = scene->GetEntities();
            net_world.each<ecs::NetId, ecs::Transform>(
                [&](ecs::Entity, ecs::NetId& nid, ecs::Transform& t) {
                    if (nid.value < kSceneNetIdBase || nid.value >= kAvatarNetIdBase) return;
                    const size_t idx = static_cast<size_t>(nid.value - kSceneNetIdBase);
                    if (idx >= ents.size()) return;
                    const auto& ent = ents[idx];
                    if (!ent) return;
                    if (ent->GetId() == local_player_id) return;   // never our player
                    if (proxy_ids.count(ent->GetId())) return;
                    auto* tf = ent->GetTransform();
                    if (!tf) return;
                    tf->SetWorldPosition(t.position + origin_offset);   // -> local frame
                    tf->SetWorldRotation(t.rotation);
                    tf->SetLocalScale(t.scale);
                    ++props;
                });
        }

        size_t avatars = 0;
        sync_avatar_proxies(scene, avatars);

        const net::NetStats ns = transport->stats();
        st.peer_count = transport->peer_count();
        st.connected  = connected;
        st.tick       = tick;
        st.bytes_sent = ns.bytes_sent;
        st.bytes_recv = ns.bytes_recv;
        st.replicated = props;
        st.avatars    = avatars;
        st.my_avatar  = my_avatar;
    }
};

NetSession::NetSession() : impl_(std::make_unique<Impl>()) {}
NetSession::~NetSession() {
    // The lobby before the transport it rides on: it says CLOSE through it and
    // takes its handler back -- the other way round it would touch a freed transport.
    impl_->stop_lobbies();
    impl_->transport.reset();
}

bool NetSession::host(uint16_t port,uint32_t max_clients) {
    shutdown();
    ensure_components_registered();
    impl_->transport = net::create_enet_transport();
    if (!impl_->transport || !impl_->transport->start_server(port, std::clamp(max_clients,1u,16u))) {
        spdlog::error("[net] host: failed to start server on port {}", port);
        impl_->transport.reset();
        return false;
    }
    impl_->tick = 0;
    status_ = NetSessionStatus{};
    status_.role     = NetRole::Host;
    status_.endpoint = "host :" + std::to_string(port);
    spdlog::info("[net] hosting on port {}", port);
    return true;
}

bool NetSession::join(const std::string& ip, uint16_t port) {
    shutdown();
    ensure_components_registered();
    impl_->transport = net::create_enet_transport();
    if (!impl_->transport || !impl_->transport->start_client()) {
        spdlog::error("[net] join: failed to start client");
        impl_->transport.reset();
        return false;
    }
    impl_->server_peer = impl_->transport->connect(ip, port);
    if (impl_->server_peer == net::kInvalidPeer) {
        spdlog::error("[net] join: connect({}:{}) failed", ip, port);
        impl_->transport.reset();
        return false;
    }
    impl_->tick = 0;
    status_ = NetSessionStatus{};
    status_.role     = NetRole::Client;
    status_.endpoint = ip + ":" + std::to_string(port);
    spdlog::info("[net] joining {}:{}", ip, port);
    return true;
}

bool NetSession::host_lobby(const std::string& server, const std::string& game_id, int max_players,
                            const std::string& title) {
    net::lobby::Endpoint at;
    if (!net::lobby::parse_endpoint(server, at)) {
        spdlog::error("[net] lobby server '{}' is not host:port", server);
        return false;
    }
    if (!host(0, static_cast<uint32_t>(std::max(1, max_players - 1)))) return false;
    impl_->lobby = std::make_unique<net::lobby::HostLink>();
    impl_->lobby->start(*impl_->transport, at, game_id, max_players, title);
    status_.endpoint = "lobby " + server;
    spdlog::info("[net] opening a lobby on {}", server);
    return true;
}

// Switches for tests and for players who want only one of the two ways:
// GWS_LOBBY_INTERNET=0 (no STUN, no DHT), GWS_LOBBY_LAN=0 (no broadcast).
static net::lobby::DirectConfig direct_config() {
    net::lobby::DirectConfig c;
    if (const char* v = std::getenv("GWS_LOBBY_INTERNET")) c.internet = std::atoi(v) != 0;
    if (const char* v = std::getenv("GWS_LOBBY_LAN")) c.lan = std::atoi(v) != 0;
    return c;
}

bool NetSession::host_direct(const std::string& game_id, int max_players, const std::string& title) {
    if (!host(0, static_cast<uint32_t>(std::max(1, max_players - 1)))) return false;
    const auto cfg = direct_config();
    impl_->direct_host = std::make_unique<net::lobby::DirectHost>();
    impl_->direct_host->start(*impl_->transport, game_id, max_players, title, cfg);
    status_.endpoint = "code (direct)";
    spdlog::info("[net] opening a game with a code, no lobby server (port {}; {}{})", impl_->transport->local_port(),
                 cfg.lan ? "this network" : "", cfg.internet ? (cfg.lan ? " + the internet" : "the internet") : "");
    return true;
}

bool NetSession::join_direct(const std::string& game_id, const std::string& code) {
    shutdown();
    ensure_components_registered();
    impl_->transport = net::create_enet_transport();
    if (!impl_->transport || !impl_->transport->start_client()) {
        spdlog::error("[net] join: failed to start client");
        impl_->transport.reset();
        return false;
    }
    impl_->direct_join = std::make_unique<net::lobby::DirectJoin>();
    impl_->direct_join->start(*impl_->transport, game_id, code, direct_config());
    impl_->server_peer = net::kInvalidPeer;
    impl_->tick = 0;
    status_ = NetSessionStatus{};
    status_.role     = NetRole::Client;
    status_.endpoint = "code " + code;
    spdlog::info("[net] looking for game {} (no lobby server)", code);
    return true;
}

NetSession::JoinState NetSession::join_state() const {
    if (!impl_->direct_join) return JoinState::Off;
    switch (impl_->direct_join->state()) {
        case net::lobby::DirectJoin::State::Searching: return JoinState::Searching;
        case net::lobby::DirectJoin::State::Found:     return JoinState::Found;
        case net::lobby::DirectJoin::State::Failed:    return JoinState::Failed;
        default:                                       return JoinState::Off;
    }
}
std::string NetSession::join_message() const {
    if (!impl_->direct_join) return {};
    return impl_->direct_join->state() == net::lobby::DirectJoin::State::Failed ? impl_->direct_join->error()
                                                                               : impl_->direct_join->progress();
}

std::string NetSession::lobby_reach() const { return impl_->direct_host ? impl_->direct_host->reach() : std::string(); }
std::string NetSession::lobby_note() const { return impl_->direct_host ? impl_->direct_host->note() : std::string(); }

NetSession::LobbyState NetSession::lobby_state() const {
    if (impl_->direct_host) {
        switch (impl_->direct_host->state()) {
            case net::lobby::DirectHost::State::Opening: return LobbyState::Registering;
            case net::lobby::DirectHost::State::Open:    return LobbyState::Open;
            case net::lobby::DirectHost::State::Failed:  return LobbyState::Failed;
            default:                                     return LobbyState::Off;
        }
    }
    if (!impl_->lobby) return LobbyState::Off;
    switch (impl_->lobby->state()) {
        case net::lobby::HostLink::State::Registering: return LobbyState::Registering;
        case net::lobby::HostLink::State::Open:        return LobbyState::Open;
        case net::lobby::HostLink::State::Failed:      return LobbyState::Failed;
        default:                                       return LobbyState::Off;
    }
}
std::string NetSession::lobby_code() const {
    if (impl_->direct_host) return impl_->direct_host->code();
    return impl_->lobby ? impl_->lobby->code() : std::string();
}
std::string NetSession::lobby_error() const {
    if (impl_->direct_host) return impl_->direct_host->error();
    return impl_->lobby ? impl_->lobby->error() : std::string();
}

void NetSession::shutdown() {
    // The lobby first: its CLOSE goes out on the transport about to go.
    impl_->stop_lobbies();
    impl_->messages.clear();
    impl_->removed_ids.clear();
    if (impl_->transport)
        spdlog::info("[net] session shut down ({})",
                     status_.role == NetRole::Host ? "host" : "client");
    if (auto s = impl_->last_scene.lock()) impl_->remove_all_proxies(s);
    impl_->transport.reset();
    // Destroy every net_world entity. The maps below are only INDICES into the
    // world — clearing them alone leaves the old entities (with NetId +
    // Transform) alive, and a re-hosted session would re-broadcast those stale
    // avatars/props and resurrect ghost proxies of the previous session's
    // players ("still see the previous ones" bug).
    {
        std::vector<ecs::Entity> stale;
        impl_->net_world.each<ecs::NetId>(
            [&](ecs::Entity e, ecs::NetId&) { stale.push_back(e); });
        for (ecs::Entity e : stale) impl_->net_world.destroy(e);
    }
    impl_->netid_map.clear();
    impl_->ent_by_netid.clear();
    impl_->avatar_by_peer.clear();
    impl_->peer_pos.clear();
    impl_->avatar_ent.clear();
    impl_->proxy.clear();
    impl_->proxy_ids.clear();
    impl_->server_peer = net::kInvalidPeer;
    impl_->next_client_index = 1;
    impl_->connected = false;
    impl_->my_avatar = 0;
    impl_->local_player_id = 0;
    impl_->tick = 0;
    status_ = NetSessionStatus{};
}

void NetSession::set_origin_offset(const glm::vec3& shift) {
    impl_->origin_offset = shift;
}

glm::vec3 NetSession::origin_offset() const {
    return impl_->origin_offset;
}

bool NetSession::send_game(uint32_t peer,const std::vector<uint8_t>& data) {
    if(!impl_->transport||data.empty()||data.size()>262144) return false;
    std::vector<uint8_t> msg(4+data.size());std::memcpy(msg.data(),&kMsgGame,4);std::memcpy(msg.data()+4,data.data(),data.size());
    if(role()==NetRole::Client) impl_->transport->send(impl_->server_peer,msg.data(),msg.size(),net::Channel::Reliable);
    else if(peer) impl_->transport->send(peer,msg.data(),msg.size(),net::Channel::Reliable);
    else impl_->transport->broadcast(msg.data(),msg.size(),net::Channel::Reliable);
    impl_->transport->flush();return true;
}
std::vector<NetGameMessage> NetSession::take_game_messages(){auto result=std::move(impl_->messages);impl_->messages.clear();return result;}
std::vector<uint32_t> NetSession::peers()const{std::vector<uint32_t> result;for(const auto& [peer,id]:impl_->avatar_by_peer)result.push_back(peer);return result;}
uint64_t NetSession::peer_avatar(uint32_t peer)const{auto it=impl_->avatar_by_peer.find(peer);return it==impl_->avatar_by_peer.end()?0:it->second;}
uint32_t NetSession::avatar_entity(uint64_t avatar)const{auto it=impl_->proxy.find(avatar);return it==impl_->proxy.end()||it->second.empty()?0:it->second.front()->GetId();}
void NetSession::disconnect_peer(uint32_t peer){if(impl_->transport){impl_->transport->disconnect(peer);impl_->transport->flush();}}

void NetSession::set_local_player(uint32_t scene_entity_id, const glm::vec3& pos) {
    impl_->local_player_id  = scene_entity_id;
    impl_->local_player_pos = pos - impl_->origin_offset;    // -> wire frame
}
void NetSession::set_local_player(uint32_t scene_entity_id, const glm::vec3& pos, float yaw) {
    set_local_player(scene_entity_id, pos);
    if (std::isfinite(yaw)) impl_->local_player_yaw = yaw;
}

void NetSession::remote_player_positions(std::vector<glm::vec3>& out) {
    out.clear();
    impl_->net_world.each<ecs::NetId, ecs::Transform>(
        [&](ecs::Entity, ecs::NetId& nid, ecs::Transform& t) {
            if (nid.value < kAvatarNetIdBase) return;      // avatars only
            if (nid.value == impl_->my_avatar) return;     // not ourselves
            out.push_back(t.position + impl_->origin_offset);  // -> local frame (Jolt ghosts)
        });
}

void NetSession::tick_frame(const std::shared_ptr<schizo::scene::Scene>& scene, float dt) {
    if (!impl_->transport) return;
    if(impl_->last_scene.lock()!=scene){
        if(auto old=impl_->last_scene.lock())impl_->remove_all_proxies(old);
        impl_->proxy.clear();impl_->proxy_ids.clear();
        for(auto& [id,e]:impl_->ent_by_netid)if(impl_->net_world.valid(e))impl_->net_world.destroy(e);
        impl_->ent_by_netid.clear();
        std::vector<ecs::Entity> old_props;impl_->net_world.each<ecs::NetId>([&](ecs::Entity e,ecs::NetId& id){if(id.value<kAvatarNetIdBase)old_props.push_back(e);});
        for(auto e:old_props)impl_->net_world.destroy(e);
        for(auto it=impl_->netid_map.begin();it!=impl_->netid_map.end();)if(it->first<kAvatarNetIdBase)it=impl_->netid_map.erase(it);else++it;
    }
    impl_->last_scene = scene;
    // Joining a code without a server: look for the host first, then connect
    // to it from this same socket (its router has let the host in by now).
    if (impl_->direct_join && impl_->server_peer == net::kInvalidPeer) {
        net::NetEvent ev;
        while (impl_->transport->poll(ev, 0)) {}
        impl_->direct_join->update(dt);
        if (impl_->direct_join->state() == net::lobby::DirectJoin::State::Found) {
            const std::string at = impl_->direct_join->host();
            const uint16_t port = impl_->direct_join->port();
            impl_->server_peer = impl_->transport->connect(at, port);
            status_.endpoint = at + ":" + std::to_string(port);
            spdlog::info("[net] found the game on {} ({}:{}) -- connecting", impl_->direct_join->via(), at, port);
        }
        return;
    }
    if (status_.role == NetRole::Host)        impl_->tick_host(scene, dt, status_);
    else if (status_.role == NetRole::Client) impl_->tick_client(scene, dt, status_);
    if (impl_->lobby) impl_->lobby->update(dt);   // after the poll that delivered the server's datagrams
    if (impl_->direct_host) impl_->direct_host->update(dt);
}

}  // namespace schizo::editor
