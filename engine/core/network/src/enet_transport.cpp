// ENet-backed Transport (Stage 7). The only TU that includes ENet.

#include "transport.h"

#include <enet/enet.h>

#include <atomic>
#include <cstdint>
#include <unordered_map>

namespace engine::network {
namespace {

// ENet's global init is process-wide + refcounted across transports.
std::atomic<int> g_enet_refs{0};
bool ensure_enet() {
    if (g_enet_refs.fetch_add(1) == 0) {
        if (enet_initialize() != 0) { g_enet_refs.fetch_sub(1); return false; }
    }
    return true;
}
void release_enet() {
    if (g_enet_refs.fetch_sub(1) == 1) enet_deinitialize();
}

class EnetTransport final : public Transport {
public:
    EnetTransport() { inited_ = ensure_enet(); }
    ~EnetTransport() override {
        if (host_) enet_host_destroy(host_);
        if (inited_) release_enet();
    }

    bool start_server(uint16_t port, uint32_t max_peers) override {
        if (!inited_ || host_) return false;
        ENetAddress addr{};
        addr.host = ENET_HOST_ANY;
        addr.port = port;
        host_ = enet_host_create(&addr, max_peers, kNumChannels, 0, 0);
        return host_ != nullptr;
    }
    bool start_client() override {
        if (!inited_ || host_) return false;
        host_ = enet_host_create(nullptr, 1, kNumChannels, 0, 0);
        return host_ != nullptr;
    }
    PeerId connect(const std::string& host, uint16_t port) override {
        if (!host_) return kInvalidPeer;
        ENetAddress addr{};
        enet_address_set_host(&addr, host.c_str());
        addr.port = port;
        ENetPeer* peer = enet_host_connect(host_, &addr, kNumChannels, 0);
        return peer ? assign_id(peer) : kInvalidPeer;
    }
    void disconnect(PeerId peer) override {
        if (ENetPeer* p = find(peer)) enet_peer_disconnect(p, 0);
    }

    void send(PeerId peer, const void* data, size_t size, Channel ch) override {
        ENetPeer* p = find(peer);
        if (p && enet_peer_send(p, static_cast<enet_uint8>(ch), make_packet(data, size, ch)) == 0)
            account_sent(size, ch);
    }
    void broadcast(const void* data, size_t size, Channel ch) override {
        if (!host_) return;
        enet_host_broadcast(host_, static_cast<enet_uint8>(ch), make_packet(data, size, ch));
        // Count the payload once per recipient (what actually goes on the wire).
        const size_t n = peers_.empty() ? 1 : peers_.size();
        for (size_t i = 0; i < n; ++i) account_sent(size, ch);
    }

    bool poll(NetEvent& out, uint32_t timeout_ms) override {
        if (!host_) return false;
        ENetEvent ev;
        if (enet_host_service(host_, &ev, timeout_ms) <= 0) return false;
        out = NetEvent{};
        switch (ev.type) {
            case ENET_EVENT_TYPE_CONNECT:
                out.type = NetEvent::Type::Connect;
                out.peer = assign_id(ev.peer);
                return true;
            case ENET_EVENT_TYPE_DISCONNECT:
                out.type = NetEvent::Type::Disconnect;
                out.peer = id_of(ev.peer);
                remove(ev.peer);
                return true;
            case ENET_EVENT_TYPE_RECEIVE:
                out.type    = NetEvent::Type::Receive;
                out.peer    = id_of(ev.peer);
                out.channel = ev.channelID;
                out.data.assign(ev.packet->data, ev.packet->data + ev.packet->dataLength);
                account_recv(ev.packet->dataLength, ev.channelID);
                enet_packet_destroy(ev.packet);
                return true;
            default:
                return false;
        }
    }
    void   flush() override { if (host_) enet_host_flush(host_); }
    size_t peer_count() const override { return peers_.size(); }

    NetStats stats() const override {
        NetStats s = traffic_;                 // cumulative byte/packet counters
        s.peers = peers_.size();
        // RTT + loss are sampled live from the connected peers (mean).
        uint64_t rtt_sum = 0;
        double   loss_sum = 0.0;
        size_t   counted = 0;
        for (const auto& [id, p] : peers_) {
            if (!p) continue;
            rtt_sum  += p->roundTripTime;
            loss_sum += static_cast<double>(p->packetLoss) /
                        static_cast<double>(ENET_PEER_PACKET_LOSS_SCALE);
            ++counted;
        }
        if (counted) {
            s.rtt_ms      = static_cast<uint32_t>(rtt_sum / counted);
            s.packet_loss = static_cast<float>(loss_sum / counted);
        }
        return s;
    }

private:
    void account_sent(size_t n, Channel ch) {
        traffic_.bytes_sent += n;
        traffic_.packets_sent += 1;
        traffic_.bytes_sent_ch[static_cast<size_t>(ch) % kNumChannels] += n;
    }
    void account_recv(size_t n, uint8_t ch) {
        traffic_.bytes_recv += n;
        traffic_.packets_recv += 1;
        traffic_.bytes_recv_ch[ch % kNumChannels] += n;
    }

    static ENetPacket* make_packet(const void* d, size_t n, Channel ch) {
        const enet_uint32 flags = (ch == Channel::Reliable) ? ENET_PACKET_FLAG_RELIABLE : 0u;
        return enet_packet_create(d, n, flags);
    }
    // PeerId is stashed in ENetPeer::data (id stored as the pointer value).
    PeerId assign_id(ENetPeer* p) {
        if (p->data) return static_cast<PeerId>(reinterpret_cast<uintptr_t>(p->data));
        const PeerId id = ++next_id_;
        p->data = reinterpret_cast<void*>(static_cast<uintptr_t>(id));
        peers_[id] = p;
        return id;
    }
    PeerId id_of(ENetPeer* p) const {
        return p->data ? static_cast<PeerId>(reinterpret_cast<uintptr_t>(p->data)) : kInvalidPeer;
    }
    ENetPeer* find(PeerId id) {
        auto it = peers_.find(id);
        return it == peers_.end() ? nullptr : it->second;
    }
    void remove(ENetPeer* p) {
        peers_.erase(id_of(p));
        p->data = nullptr;
    }

    ENetHost* host_   = nullptr;
    bool      inited_ = false;
    PeerId    next_id_ = 0;
    NetStats  traffic_{};   // cumulative; rtt/loss filled live in stats()
    std::unordered_map<PeerId, ENetPeer*> peers_;
};

}  // namespace

std::unique_ptr<Transport> create_enet_transport() {
    return std::make_unique<EnetTransport>();
}

}  // namespace engine::network
