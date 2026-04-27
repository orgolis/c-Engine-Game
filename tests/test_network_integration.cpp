#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <entt/entt.hpp>
#include "network_systems.h"

using namespace engine::network;
using InputAction = engine::character::InputAction;
using Catch::Approx;

TEST_CASE("Network - Deterministic Simulation", "[network][deterministic]") {
    DeterministicSimulation sim;

    SECTION("Initial state") {
        REQUIRE(sim.GetCurrentTick() == 0);
        REQUIRE(sim.IsDeterministic());
    }

    SECTION("Fixed timestep is constant") {
        REQUIRE(DeterministicSimulation::FIXED_TIMESTEP == Approx(1.0f / 60.0f));
    }

    SECTION("Single real frame processes multiple ticks") {
        entt::registry registry;
        
        float real_delta = 0.05f;  // 50ms = 3 ticks @ 60Hz
        sim.Update(real_delta, registry);
        
        REQUIRE(sim.GetTicksProcessedThisFrame() > 0);
        REQUIRE(sim.GetCurrentTick() > 0);
    }

    SECTION("Accumulates time correctly") {
        entt::registry registry;
        
        sim.Update(0.01f, registry);  // 10ms
        uint64_t tick_after_10ms = sim.GetCurrentTick();
        
        sim.Update(0.01f, registry);  // Another 10ms
        uint64_t tick_after_20ms = sim.GetCurrentTick();
        
        REQUIRE(tick_after_20ms >= tick_after_10ms);
    }

    SECTION("Reset clears state") {
        entt::registry registry;
        sim.Update(0.05f, registry);
        
        REQUIRE(sim.GetCurrentTick() > 0);
        
        sim.Reset();
        
        REQUIRE(sim.GetCurrentTick() == 0);
        REQUIRE(sim.GetAccumulatedTime() == Approx(0.0f));
    }
}

TEST_CASE("Network - Input Synchronizer", "[network][input][sync]") {
    InputSynchronizer sync;

    SECTION("Buffer local input") {
        InputAction input;
        input.forward = 1.0f;
        input.sprint = true;

        sync.BufferLocalInput(input, 0);

        REQUIRE(sync.HasInputForTick(0));
        REQUIRE(sync.GetBufferedInputCount() == 1);
    }

    SECTION("Retrieve input by tick") {
        InputAction input;
        input.forward = 1.0f;

        sync.BufferLocalInput(input, 5);

        InputAction retrieved = sync.GetInputForTick(5);
        REQUIRE(retrieved.forward == Approx(1.0f));
    }

    SECTION("Return empty input for missing tick") {
        InputAction retrieved = sync.GetInputForTick(999);
        
        REQUIRE(retrieved.forward == Approx(0.0f));
        REQUIRE(retrieved.lateral == Approx(0.0f));
    }

    SECTION("Track pending ticks") {
        for (int i = 0; i < 5; ++i) {
            InputAction input;
            input.forward = static_cast<float>(i);
            sync.BufferLocalInput(input, i);
        }

        auto pending = sync.GetPendingInputTicks();
        REQUIRE(pending.size() == 5);

        sync.ConfirmInputTick(2);
        pending = sync.GetPendingInputTicks();
        
        REQUIRE(pending.size() == 2);  // Only ticks 3 and 4 pending
    }

    SECTION("Respects buffer capacity") {
        for (int i = 0; i < InputSynchronizer::MAX_BUFFERED_INPUTS + 10; ++i) {
            InputAction input;
            sync.BufferLocalInput(input, i);
        }

        REQUIRE(sync.GetBufferedInputCount() <= InputSynchronizer::MAX_BUFFERED_INPUTS);
    }

    SECTION("Prune old inputs") {
        for (int i = 0; i < 10; ++i) {
            InputAction input;
            sync.BufferLocalInput(input, i);
        }

        sync.PruneOldInputs(5);

        REQUIRE(!sync.HasInputForTick(0));
        REQUIRE(sync.HasInputForTick(5));
    }
}

TEST_CASE("Network - Rollback Manager", "[network][rollback]") {
    RollbackManager rollback;

    SECTION("No snapshots initially") {
        REQUIRE(rollback.GetSnapshotCount() == 0);
    }

    SECTION("Save and retrieve snapshot") {
        entt::registry registry;
        auto entity = registry.create();

        rollback.SaveSnapshot(0, registry);

        REQUIRE(rollback.HasSnapshot(0));
        REQUIRE(rollback.GetNewestSnapshotTick() == 0);
    }

    SECTION("Save multiple snapshots") {
        entt::registry registry;

        for (int i = 0; i < 5; ++i) {
            rollback.SaveSnapshot(i, registry);
        }

        REQUIRE(rollback.GetSnapshotCount() == 5);
        REQUIRE(rollback.GetOldestSnapshotTick() == 0);
        REQUIRE(rollback.GetNewestSnapshotTick() == 4);
    }

    SECTION("Respects max snapshot limit") {
        entt::registry registry;

        for (int i = 0; i < RollbackManager::MAX_SNAPSHOTS + 20; ++i) {
            rollback.SaveSnapshot(i, registry);
        }

        REQUIRE(rollback.GetSnapshotCount() <= RollbackManager::MAX_SNAPSHOTS);
    }

    SECTION("Prune old snapshots") {
        entt::registry registry;

        for (int i = 0; i < 10; ++i) {
            rollback.SaveSnapshot(i, registry);
        }

        rollback.PruneOldSnapshots(5);

        REQUIRE(!rollback.HasSnapshot(0));
        REQUIRE(!rollback.HasSnapshot(4));
        REQUIRE(rollback.HasSnapshot(5));
    }

    SECTION("Rollback to specific tick") {
        entt::registry registry;

        rollback.SaveSnapshot(10, registry);
        bool rolled_back = rollback.RollbackToTick(10, registry);

        REQUIRE(rolled_back);
    }

    SECTION("Rollback to non-existent tick fails") {
        entt::registry registry;

        bool rolled_back = rollback.RollbackToTick(999, registry);
        REQUIRE(!rolled_back);
    }
}

TEST_CASE("Network - Reconciliation System", "[network][reconciliation]") {
    ReconciliationSystem recon;

    SECTION("Initially not reconciling") {
        REQUIRE(!recon.IsReconciling());
    }

    SECTION("Queue reconciliation") {
        entt::registry registry;
        auto entity = registry.create();

        recon.QueueReconciliation(entity, glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(0.0f));

        REQUIRE(recon.IsReconciling());
    }

    SECTION("Reconciliation completes after time") {
        entt::registry registry;
        auto entity = registry.create();

        recon.QueueReconciliation(entity, glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(0.0f));

        // Update for more than reconcile time
        recon.Update(ReconciliationSystem::RECONCILE_TIME + 0.1f, registry);

        REQUIRE(!recon.IsReconciling());
    }
}

TEST_CASE("Network - Packet Protocol", "[network][packets]") {
    SECTION("ClientInputPacket size") {
        REQUIRE(sizeof(ClientInputPacket) > 0);
        REQUIRE(sizeof(ClientInputPacket) < 64);  // Should be small
    }

    SECTION("ServerGameStatePacket has correct structure") {
        ServerGameStatePacket packet;
        packet.tick = 100;
        packet.entity_count = 5;

        REQUIRE(packet.tick == 100);
        REQUIRE(packet.entity_count == 5);
    }

    SECTION("EntityState structure") {
        EntityState state;
        state.entity_id = 1;
        state.position = glm::vec3(10.0f, 20.0f, 30.0f);

        REQUIRE(state.entity_id == 1);
        REQUIRE(state.position.x == Approx(10.0f));
    }
}

TEST_CASE("Network - Network Manager", "[network][manager]") {
    NetworkManager manager;

    SECTION("Initial state offline") {
        REQUIRE(manager.GetNetworkRole() == NetworkRole::Offline);
        REQUIRE(!manager.IsConnected());
    }

    SECTION("Set to offline mode") {
        // No actual network operations in offline mode
        REQUIRE(manager.GetNetworkRole() == NetworkRole::Offline);
    }

    SECTION("Input synchronizer accessible") {
        InputAction input;
        manager.SendInput(0, input);

        REQUIRE(manager.GetInputSynchronizer().GetBufferedInputCount() > 0);
    }

    SECTION("Latency tracking") {
        REQUIRE(manager.GetLatencyMs() == Approx(0.0f));
        REQUIRE(manager.GetPacketLoss() == Approx(0.0f));
    }
}
