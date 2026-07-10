#pragma once

// ====================
// Network profiler (Stage 14 — Pillar N4)
// ====================
//
// Turns the Transport's *cumulative* NetStats counters into the live, per-second
// view a HUD wants: up/down bandwidth (overall + per channel), packet rate, and
// the sampled RTT / packet-loss / peer count. Plus a rolling window so the
// overlay can show a smoothed "kbit/s" instead of a single jittery frame delta.
//
// Usage (once per frame on the net thread / main loop):
//     prof.sample(transport.stats(), dt_seconds);
//     prof.set_rollback_depth(client.last_rollback_depth(),
//                             client.max_rollback_depth());
// then read prof.view() for display. Transport-agnostic and header-only so it
// links into the editor, a headless server, or a check tool unchanged.

#include "transport.h"

#include <cstdint>

namespace engine::network {

// Per-second snapshot the overlay renders. Rates are bytes/sec unless noted.
struct NetProfileView {
    double   send_bps      = 0.0;   // payload bytes/sec sent
    double   recv_bps      = 0.0;   // payload bytes/sec received
    double   send_bps_ch[kNumChannels] = {};
    double   recv_bps_ch[kNumChannels] = {};
    double   send_pps      = 0.0;   // packets/sec sent
    double   recv_pps      = 0.0;   // packets/sec received
    uint32_t rtt_ms        = 0;
    float    packet_loss   = 0.0f;  // 0..1
    size_t   peers         = 0;
    size_t   rollback_last  = 0;    // inputs replayed on the last reconcile
    size_t   rollback_max   = 0;    // worst-case rollback depth seen
    // Lifetime totals (carried through for a "session total" readout).
    uint64_t total_bytes_sent = 0;
    uint64_t total_bytes_recv = 0;
};

class NetProfiler {
public:
    // Difference `cur` against the previous sample over `dt` seconds to produce
    // per-second rates. The first call only seeds the baseline (rates stay 0).
    void sample(const NetStats& cur, float dt) {
        if (have_prev_ && dt > 0.0f) {
            const double inv = 1.0 / static_cast<double>(dt);
            view_.send_bps = delta(cur.bytes_sent,   prev_.bytes_sent)   * inv;
            view_.recv_bps = delta(cur.bytes_recv,   prev_.bytes_recv)   * inv;
            view_.send_pps = delta(cur.packets_sent, prev_.packets_sent) * inv;
            view_.recv_pps = delta(cur.packets_recv, prev_.packets_recv) * inv;
            for (size_t c = 0; c < kNumChannels; ++c) {
                view_.send_bps_ch[c] = delta(cur.bytes_sent_ch[c], prev_.bytes_sent_ch[c]) * inv;
                view_.recv_bps_ch[c] = delta(cur.bytes_recv_ch[c], prev_.bytes_recv_ch[c]) * inv;
            }
        }
        view_.rtt_ms          = cur.rtt_ms;
        view_.packet_loss     = cur.packet_loss;
        view_.peers           = cur.peers;
        view_.total_bytes_sent = cur.bytes_sent;
        view_.total_bytes_recv = cur.bytes_recv;
        prev_      = cur;
        have_prev_ = true;
    }

    void set_rollback_depth(size_t last, size_t max) {
        view_.rollback_last = last;
        view_.rollback_max  = max;
    }

    const NetProfileView& view() const { return view_; }
    void reset() { *this = NetProfiler{}; }

private:
    static double delta(uint64_t a, uint64_t b) {
        return a >= b ? static_cast<double>(a - b) : 0.0;   // guard counter reset
    }
    NetStats       prev_{};
    NetProfileView view_{};
    bool           have_prev_ = false;
};

}  // namespace engine::network
