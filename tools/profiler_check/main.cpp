// ====================
// profiler_check — Stage 14 profiler verification CLI
// ====================
//
// Verifies the pieces of the perf-infra stage that are pure logic (no Vulkan /
// ImGui), so they can be checked headlessly:
//   [N4a] Transport NetStats — byte/packet counters + per-channel split are
//         accurate over a real ENet loopback exchange; RTT/loss/peers readable.
//   [N4b] NetProfiler — cumulative counters difference into correct per-second
//         bandwidth over a known dt.
//   [N4c] Prediction rollback depth — reconcile reports how many inputs replayed.
//
// (The CPU/GPU/memory overlay is visual and verified in-editor; the data feeds
// behind it — gws::profile::Profiler, GPUProfiler, MemoryTracker — are exercised
// by the editor self-checks and their own call sites.)

#include "net_profiler.h"
#include "prediction.h"
#include "transport.h"

#include "memory_snapshot.h"   // N3: per-tag tracker + allocator registry
#include "frame_capture.h"      // N5: one-frame draw-list snapshot

#include <cstdio>
#include <cstring>
#include <string>

using namespace engine::network;

static int g_failures = 0;
static void check(const char* name, bool ok) {
    std::printf("  [%s] %s\n", ok ? "OK  " : "FAIL", name);
    if (!ok) ++g_failures;
}

// [N4a]+[N4b] — drive a loopback exchange and inspect the counters.
static void test_transport_stats() {
    std::printf("N4a/b: transport NetStats + NetProfiler bandwidth\n");
    auto server = create_enet_transport();
    auto client = create_enet_transport();
    if (!server->start_server(17780, 8) || !client->start_client()) {
        check("loopback setup", false);
        return;
    }
    const PeerId server_peer = client->connect("127.0.0.1", 17780);
    if (server_peer == kInvalidPeer) { check("connect", false); return; }

    constexpr int    kCount = 10;     // application packets
    constexpr size_t kSize  = 32;     // bytes each
    unsigned char    payload[kSize];
    std::memset(payload, 0xAB, kSize);

    bool   connected = false;
    int    sent      = 0;
    for (int i = 0; i < 600 && server->stats().packets_recv < kCount; ++i) {
        NetEvent ev;
        while (server->poll(ev, 2)) { /* drain; counters update inside poll */ }
        while (client->poll(ev, 2)) {
            if (ev.type == NetEvent::Type::Connect) connected = true;
        }
        if (connected && sent < kCount) {
            client->send(server_peer, payload, kSize, Channel::Reliable);
            client->flush();
            ++sent;
        }
    }

    const NetStats cs = client->stats();
    const NetStats ss = server->stats();
    const auto R = static_cast<size_t>(Channel::Reliable);
    const auto U = static_cast<size_t>(Channel::Unreliable);

    check("client bytes_sent == count*size",
          cs.bytes_sent == kCount * kSize && cs.packets_sent == kCount);
    check("client sent counted on Reliable channel only",
          cs.bytes_sent_ch[R] == kCount * kSize && cs.bytes_sent_ch[U] == 0);
    check("server bytes_recv == count*size",
          ss.bytes_recv == kCount * kSize && ss.packets_recv == kCount);
    check("server recv counted on Reliable channel",
          ss.bytes_recv_ch[R] == kCount * kSize);
    check("client sees 1 peer, loss == 0", cs.peers == 1 && ss.peers == 1 && cs.packet_loss == 0.0f);
    std::printf("       (sent=%llu B / %llu pkt, recv=%llu B / %llu pkt, rtt=%ums, loss=%.3f)\n",
                (unsigned long long)cs.bytes_sent, (unsigned long long)cs.packets_sent,
                (unsigned long long)ss.bytes_recv, (unsigned long long)ss.packets_recv,
                cs.rtt_ms, cs.packet_loss);

    // [N4b] NetProfiler rate math — deterministic, no timing flake: seed then
    // feed a synthetic +16000-byte delta over 0.5s -> expect 32000 B/s.
    NetProfiler prof;
    NetStats a{}; a.bytes_sent = 1000; a.packets_sent = 4;
    prof.sample(a, 1.0f / 60.0f);                 // seed baseline (rates stay 0)
    check("first sample seeds (no rate)", prof.view().send_bps == 0.0);
    NetStats b = a; b.bytes_sent += 16000; b.packets_sent += 30;
    prof.sample(b, 0.5f);
    check("send_bps == 32000 over 0.5s", prof.view().send_bps == 32000.0);
    check("send_pps == 60 over 0.5s",    prof.view().send_pps == 60.0);
    prof.set_rollback_depth(3, 7);
    check("rollback depth carried", prof.view().rollback_last == 3 && prof.view().rollback_max == 7);
}

// [N4c] — reconcile reports rollback depth (unacked inputs replayed).
static void test_rollback_depth() {
    std::printf("N4c: prediction rollback depth\n");
    const float dt = 1.0f / 60.0f;
    SimStep step = make_kinematic_step(5.0f);
    PredictionClient client(step);

    // Predict 5 inputs locally (none acked yet).
    for (int i = 0; i < 5; ++i) client.tick(PlayerInput{1.0f, 0.0f, 0}, dt);
    check("5 inputs pending", client.pending() == 5);

    // Server has acked nothing -> reconcile must replay all 5.
    StatePacket none{ /*server_tick*/ 5, /*acked_input_tick*/ -1, glm::vec3(0.0f) };
    client.reconcile(none, dt);
    check("rollback depth == 5 when nothing acked", client.last_rollback_depth() == 5);

    // Server acks the first 3 -> next reconcile replays the remaining 2.
    StatePacket acked3{ 8, 2, glm::vec3(0.15f, 0.0f, 0.0f) };  // ticks 0..2 acked
    client.reconcile(acked3, dt);
    check("rollback depth == 2 after acking 3", client.last_rollback_depth() == 2);
    check("max rollback depth == 5", client.max_rollback_depth() == 5);
}

// [N3] — per-tag memory snapshot + opt-in allocator registry.
static void test_memory_snapshot() {
    std::printf("N3: memory snapshot (per-tag tracker + allocator registry)\n");
#if GWS_MEMORY_TRACKING
    using namespace gws::memory;
    auto& tracker = MemoryTracker::instance();
    tracker.reset();

    // Fake allocations keyed by distinct pointers (tracker keys by ptr, not real
    // memory): 100+200 B under Render, 50 B under Physics.
    auto* p1 = reinterpret_cast<void*>(0x1000);
    auto* p2 = reinterpret_cast<void*>(0x2000);
    auto* p3 = reinterpret_cast<void*>(0x3000);
    tracker.on_alloc(p1, 100, AllocationTag::Render);
    tracker.on_alloc(p2, 200, AllocationTag::Render);
    tracker.on_alloc(p3, 50,  AllocationTag::Physics);

    MemorySnapshot snap = snapshot_memory();
    auto live_of = [&](AllocationTag t) -> size_t {
        for (const auto& u : snap.tags) if (u.tag == t) return u.stats.live_bytes;
        return ~size_t(0);
    };
    auto count_of = [&](AllocationTag t) -> size_t {
        for (const auto& u : snap.tags) if (u.tag == t) return u.stats.live_count;
        return ~size_t(0);
    };
    check("snapshot has one entry per tag", snap.tags.size() == static_cast<size_t>(AllocationTag::Count));
    check("Render live == 300 B / 2 allocs", live_of(AllocationTag::Render) == 300 && count_of(AllocationTag::Render) == 2);
    check("Physics live == 50 B / 1 alloc",  live_of(AllocationTag::Physics) == 50 && count_of(AllocationTag::Physics) == 1);
    check("totals == 350 B / 3 allocs", snap.total_live_bytes == 350 && snap.total_live_count == 3);

    tracker.on_free(p1);                          // free one Render alloc
    snap = snapshot_memory();
    check("after free: Render live == 200 B / 1 alloc",
          live_of(AllocationTag::Render) == 200 && count_of(AllocationTag::Render) == 1);
    check("Render peak retained == 300 B", [&] {
        for (const auto& u : snap.tags) if (u.tag == AllocationTag::Render) return u.stats.peak_bytes == 300;
        return false; }());

    // Allocator registry: register a synthetic allocator, read it back, remove.
    AllocationStats fake; fake.total_reserved = 8 * 1024 * 1024; fake.total_allocated = 2 * 1024 * 1024;
    fake.peak_used = 3 * 1024 * 1024; fake.fragmentation = 12;
    size_t before = AllocatorRegistry::instance().count();
    {
        ScopedAllocatorProbe probe("frame", [&] { return fake; });
        check("registry sees +1 allocator", AllocatorRegistry::instance().count() == before + 1);
        auto probes = AllocatorRegistry::instance().snapshot();
        bool found = false;
        for (const auto& [name, st] : probes)
            if (name == "frame") { found = st.total_reserved == fake.total_reserved && st.fragmentation == 12; }
        check("registry reports reserved + fragmentation", found);
    }
    check("registry unregisters on scope exit", AllocatorRegistry::instance().count() == before);

    tracker.reset();
#else
    check("memory tracking compiled in (skipped: release build)", true);
#endif
}

// [N5] — one-frame draw-list capture: arm / begin / add / end / dump.
static void test_frame_capture() {
    std::printf("N5: frame capture (one-frame draw-list snapshot)\n");
    using gws::profile::FrameCapture;
    using gws::profile::CapturedDraw;
    auto& fc = FrameCapture::instance();

    // Not armed -> begin() is a no-op and does NOT capture.
    check("begin() ignored when not armed", fc.begin(1) == false && !fc.capturing());

    fc.arm();
    check("armed", fc.armed());
    const bool opened = fc.begin(42);
    check("begin() captures when armed (and disarms)", opened && fc.capturing() && !fc.armed());
    fc.add(CapturedDraw{"Geometry",    "mesh#1", 0, 1588, 4734, false});
    fc.add(CapturedDraw{"Geometry",    "mesh#2", 0,  100,  300, false});
    fc.add(CapturedDraw{"Transparent", "glass",  1,  24,   36,  true});
    fc.end();

    const auto& cap = fc.last();
    check("captured frame index == 42", cap.frame_index == 42);
    check("captured 3 draws", cap.draw_count() == 3);
    check("total indices == 5070", cap.total_indices() == 5070);
    check("total triangles == 1690", cap.total_triangles() == 1690);
    check("has_capture set + not still capturing", fc.has_capture() && !fc.capturing());
    const std::string txt = fc.to_text();
    check("to_text mentions draws + a label", txt.find("3 draws") != std::string::npos &&
                                               txt.find("glass") != std::string::npos);

    // A second arm captures a fresh frame, replacing the last.
    fc.arm();
    fc.begin(43);
    fc.add(CapturedDraw{"Shadow", "caster", 0, 8, 12, false});
    fc.end();
    check("re-capture replaces last (1 draw, frame 43)",
          fc.last().draw_count() == 1 && fc.last().frame_index == 43);
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("profiler_check: Stage 14 perf-infra (N3 memory + N4 network + N5 capture)\n");
    test_transport_stats();
    test_rollback_depth();
    test_memory_snapshot();
    test_frame_capture();
    std::printf("profiler_check: %s\n", g_failures == 0 ? "ALL OK" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
