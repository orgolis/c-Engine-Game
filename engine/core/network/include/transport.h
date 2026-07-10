#pragma once

// ====================
// Transport — reliable-UDP behind a backend-agnostic interface  (Stage 7 step 1)
// ====================
//
// The networking layer talks to this, never to ENet directly (so the backend
// can be swapped later). Server `start_server` + `poll`s connect/receive
// events; client `start_client` + `connect`s, then `poll`s. Two channels:
// Reliable (ordered, for events/snapshots that must arrive) and Unreliable
// (for high-rate state that can be dropped).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace engine::network {

using PeerId = uint32_t;
inline constexpr PeerId   kInvalidPeer  = 0;
inline constexpr uint8_t  kNumChannels  = 2;

enum class Channel : uint8_t { Reliable = 0, Unreliable = 1 };

struct NetEvent {
    enum class Type : uint8_t { None, Connect, Disconnect, Receive };
    Type                 type    = Type::None;
    PeerId               peer    = kInvalidPeer;
    uint8_t              channel = 0;
    std::vector<uint8_t> data;     // Receive payload
};

// Cumulative traffic + live connection-quality counters for the N4 network
// profiler. Byte/packet counts are lifetime totals (the profiler differences
// them over a window to get bandwidth); rtt/loss are sampled from the live
// peers each call. Application-layer bytes (payload only — ENet's own
// header/ack overhead is not counted, which is what gameplay cares about).
struct NetStats {
    uint64_t bytes_sent      = 0;   // total payload bytes sent (all channels)
    uint64_t bytes_recv      = 0;   // total payload bytes received
    uint64_t packets_sent    = 0;
    uint64_t packets_recv    = 0;
    uint64_t bytes_sent_ch[kNumChannels] = {};   // per-channel payload bytes
    uint64_t bytes_recv_ch[kNumChannels] = {};
    uint32_t rtt_ms          = 0;   // mean round-trip time over peers (ENet)
    float    packet_loss     = 0.0f;// mean reliable-packet loss, 0..1, over peers
    size_t   peers           = 0;
};

class Transport {
public:
    virtual ~Transport() = default;

    virtual bool   start_server(uint16_t port, uint32_t max_peers) = 0;
    virtual bool   start_client() = 0;
    virtual PeerId connect(const std::string& host, uint16_t port) = 0;
    virtual void   disconnect(PeerId peer) = 0;

    virtual void   send(PeerId peer, const void* data, size_t size, Channel ch = Channel::Reliable) = 0;
    virtual void   broadcast(const void* data, size_t size, Channel ch = Channel::Reliable) = 0;

    // Poll one queued event (call repeatedly until it returns false). A nonzero
    // timeout blocks up to that many ms waiting for the first event.
    virtual bool   poll(NetEvent& out, uint32_t timeout_ms = 0) = 0;
    virtual void   flush() = 0;
    virtual size_t peer_count() const = 0;

    // N4 profiler hook: cumulative traffic counters + live RTT/loss.
    virtual NetStats stats() const = 0;
};

std::unique_ptr<Transport> create_enet_transport();

}  // namespace engine::network
