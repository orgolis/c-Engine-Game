#pragma once

// ====================
// Client prediction + server reconciliation  (Stage 7 step 3)
// ====================
//
// The server is authoritative. The client applies its own input IMMEDIATELY
// (prediction — responsive feel) and buffers each (tick, input). When an
// authoritative state for an acked input-tick arrives, the client SNAPS to it
// and RE-SIMULATES the still-unacked buffered inputs forward (reconciliation).
// If the prediction was right, re-sim reproduces the same position → no snap;
// if the server diverged (e.g. a wall the client didn't predict), the client
// converges to the authoritative path.
//
// The simulation step is a pluggable deterministic function `SimStep`: here a
// kinematic move, but it is meant to wrap the deterministic fixed-step sim
// (Jolt physics, built CROSS_PLATFORM_DETERMINISTIC) — the machinery below is
// identical regardless of what the step does, which is why it can be verified
// in isolation with a simple step under controlled latency.

#include <glm/glm.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>

namespace engine::network {

struct PlayerInput {
    float    move_x = 0.0f;   // -1..1 (strafe)
    float    move_z = 0.0f;   // -1..1 (forward)
    uint64_t tick   = 0;
};

struct PlayerState {
    glm::vec3 position{0.0f};
};

// Deterministic, identical on client + server. (Wrap physics here later.)
using SimStep = std::function<void(PlayerState&, const PlayerInput&, float dt)>;

// Default kinematic step — pos += move * speed * dt.
inline SimStep make_kinematic_step(float speed = 5.0f) {
    return [speed](PlayerState& s, const PlayerInput& in, float dt) {
        s.position += glm::vec3(in.move_x, 0.0f, in.move_z) * speed * dt;
    };
}

// Wire packets (POD; serialize trivially for the transport).
struct InputPacket { uint64_t tick; float move_x, move_z; };
// acked_input_tick = the last input tick the server has applied, or -1 if none.
struct StatePacket { uint64_t server_tick; int64_t acked_input_tick; glm::vec3 position; };

// ---- Server: authoritative sim ----
class PredictionServer {
public:
    explicit PredictionServer(SimStep step) : step_(std::move(step)) {}

    void receive_input(const InputPacket& in) { inputs_[in.tick] = in; }

    // Advance the authoritative sim one tick: apply the client input tagged with
    // this tick (if it has arrived), and record it as acked. A correction hook
    // lets a test/world clamp the authoritative state (e.g. collision).
    void tick(float dt, const std::function<void(PlayerState&)>& correct = {}) {
        // Apply the oldest buffered input (inputs arrive late + in order under
        // latency, so process by arrival, not by matching the server clock).
        if (!inputs_.empty()) {
            auto it = inputs_.begin();
            step_(state_, PlayerInput{it->second.move_x, it->second.move_z, it->first}, dt);
            acked_input_tick_ = static_cast<int64_t>(it->first);
            inputs_.erase(it);
        }
        if (correct) correct(state_);
        ++server_tick_;
    }

    StatePacket snapshot() const { return {server_tick_, acked_input_tick_, state_.position}; }
    const PlayerState& state() const { return state_; }
    uint64_t tick_count() const { return server_tick_; }

private:
    SimStep                          step_;
    PlayerState                      state_;
    uint64_t                         server_tick_       = 0;
    int64_t                          acked_input_tick_  = -1;   // -1 = none applied
    std::map<uint64_t, InputPacket>  inputs_;
};

// ---- Client: predict + reconcile ----
class PredictionClient {
public:
    explicit PredictionClient(SimStep step) : step_(std::move(step)) {}

    // Apply input immediately (predict), buffer it, and return the packet to send.
    InputPacket tick(const PlayerInput& input, float dt) {
        const PlayerInput pi{input.move_x, input.move_z, client_tick_};
        step_(predicted_, pi, dt);
        pending_.push_back(pi);
        const InputPacket pkt{client_tick_, input.move_x, input.move_z};
        ++client_tick_;
        return pkt;
    }

    // On an authoritative snapshot: drop acked inputs, snap to it, re-simulate
    // the still-unacked inputs forward.
    void reconcile(const StatePacket& srv, float dt) {
        while (!pending_.empty() &&
               static_cast<int64_t>(pending_.front().tick) <= srv.acked_input_tick)
            pending_.pop_front();
        predicted_.position = srv.position;
        // Rollback depth = how many unacked inputs we had to replay this frame.
        last_rollback_depth_ = pending_.size();
        if (last_rollback_depth_ > max_rollback_depth_) max_rollback_depth_ = last_rollback_depth_;
        for (const PlayerInput& pi : pending_) step_(predicted_, pi, dt);
        ++reconciles_;
    }

    const PlayerState& predicted()   const { return predicted_; }
    uint64_t           client_tick() const { return client_tick_; }
    size_t             pending()     const { return pending_.size(); }
    uint64_t           reconciles()  const { return reconciles_; }
    // N4 profiler: inputs replayed on the last / worst reconcile.
    size_t             last_rollback_depth() const { return last_rollback_depth_; }
    size_t             max_rollback_depth()  const { return max_rollback_depth_; }

private:
    SimStep                 step_;
    PlayerState             predicted_;
    uint64_t                client_tick_ = 0;
    uint64_t                reconciles_  = 0;
    size_t                  last_rollback_depth_ = 0;
    size_t                  max_rollback_depth_  = 0;
    std::deque<PlayerInput> pending_;
};

}  // namespace engine::network
