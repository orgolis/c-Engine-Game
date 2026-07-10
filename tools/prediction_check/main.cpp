// ====================
// prediction_check — Stage 7 prediction + reconciliation verification
// ====================
//
// Runs a client + authoritative server in lockstep with SIMULATED LATENCY
// (packets delivered N ticks late). Verifies the four properties that make
// client prediction correct:
//   1. Convergence  — after the stream drains, client == server (no drift).
//   2. Responsiveness — mid-stream the client LEADS the server (it predicted).
//   3. Smoothness   — when prediction is right, reconciliation adds no snap
//                     (max per-tick move stays ~ the normal step).
//   4. Correction   — when the server diverges (a wall the client didn't
//                     predict), the client reconciles to the authoritative path.

#include "prediction.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <utility>

using namespace engine::network;

struct Result {
    glm::vec3 client_final{0}, server_final{0};
    float     max_jump  = 0.0f;
    bool      responsive = false;
    uint64_t  reconciles = 0;
};

// Drive client+server for 200 input ticks (+x for 100, +z for 100) then drain,
// with `latency`-tick packet delay both ways. Optional server wall-clamp.
static Result run(int latency, bool wall, float wall_x) {
    const float dt = 1.0f / 60.0f;
    SimStep step = make_kinematic_step(5.0f);
    PredictionClient client(step);
    PredictionServer server(step);

    std::deque<std::pair<int, InputPacket>> to_server;
    std::deque<std::pair<int, StatePacket>> to_client;
    std::function<void(PlayerState&)> correct;
    if (wall) correct = [wall_x](PlayerState& s) { if (s.position.x > wall_x) s.position.x = wall_x; };

    Result r;
    glm::vec3 prev = client.predicted().position;
    const int total = 200;
    const int drain = latency * 3 + 20;

    for (int t = 0; t < total + drain; ++t) {
        while (!to_server.empty() && to_server.front().first <= t) {
            server.receive_input(to_server.front().second);
            to_server.pop_front();
        }
        server.tick(dt, correct);
        to_client.push_back({t + latency, server.snapshot()});

        while (!to_client.empty() && to_client.front().first <= t) {
            client.reconcile(to_client.front().second, dt);
            to_client.pop_front();
        }

        PlayerInput in{};
        if (t < total) { if (t < 100) in.move_x = 1.0f; else in.move_z = 1.0f; }
        InputPacket ip = client.tick(in, dt);
        if (t < total) to_server.push_back({t + latency, ip});

        if (t > 0) r.max_jump = std::max(r.max_jump, glm::length(client.predicted().position - prev));
        prev = client.predicted().position;
        if (t == 100 && client.predicted().position.x > server.state().position.x + 0.1f)
            r.responsive = true;
    }
    r.client_final = client.predicted().position;
    r.server_final = server.state().position;
    r.reconciles   = client.reconciles();
    return r;
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("prediction_check: client prediction + server reconciliation (Stage 7)\n");
    const float dt = 1.0f / 60.0f, speed = 5.0f;
    const float step_len = speed * dt;
    int failures = 0;

    // [1] 6-tick latency, no correction — convergence + responsiveness + smooth.
    {
        const Result r = run(6, false, 0.0f);
        const float exp = 100 * step_len;   // 100 ticks of move in x, then in z
        const bool conv     = glm::length(r.client_final - r.server_final) < 0.01f;
        const bool reached  = std::fabs(r.client_final.x - exp) < 0.01f &&
                              std::fabs(r.client_final.z - exp) < 0.01f;
        const bool smooth   = r.max_jump < step_len * 2.0f;   // no reconcile rubber-band
        const bool ok = conv && reached && r.responsive && smooth;
        std::printf("  [1] lat=6 no-wall: client(%.3f,%.3f) server(%.3f,%.3f); converge=%d reached=%d "
                    "responsive=%d smooth=%d (max_jump %.4f vs step %.4f), reconciles=%llu -> %s\n",
                    r.client_final.x, r.client_final.z, r.server_final.x, r.server_final.z,
                    conv, reached, r.responsive, smooth, r.max_jump, step_len,
                    (unsigned long long)r.reconciles, ok ? "OK" : "FAIL");
        failures += ok ? 0 : 1;
    }

    // [2] server correction — a wall at x<=3 the client didn't predict.
    {
        const Result r = run(6, true, 3.0f);
        const bool conv    = glm::length(r.client_final - r.server_final) < 0.01f;
        const bool clamped = std::fabs(r.client_final.x - 3.0f) < 0.01f;
        const bool ok = conv && clamped;
        std::printf("  [2] lat=6 wall x<=3: client.x %.3f, server.x %.3f; converge=%d clamped-to-wall=%d -> %s\n",
                    r.client_final.x, r.server_final.x, conv, clamped, ok ? "OK" : "FAIL");
        failures += ok ? 0 : 1;
    }

    // [3] higher latency still converges + stays responsive.
    {
        const Result r = run(20, false, 0.0f);
        const bool ok = (glm::length(r.client_final - r.server_final) < 0.01f) && r.responsive;
        std::printf("  [3] lat=20 no-wall: converge+responsive -> %s\n", ok ? "OK" : "FAIL");
        failures += ok ? 0 : 1;
    }

    std::printf("prediction_check: %s\n", failures == 0 ? "ALL OK" : "FAIL");
    return failures == 0 ? 0 : 1;
}
