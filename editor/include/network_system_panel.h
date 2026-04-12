#pragma once

#include <cstdint>
#include <memory>
#include <string>

// Forward declarations
namespace engine::network {
    class NetworkManager;
    class DeterministicSimulation;
    class RollbackManager;
}

namespace schizo::editor {

/**
 * @struct NetworkSystemState
 * @brief Editor state for network system display
 */
struct NetworkSystemState {
    // Display flags
    bool show_tick_info = true;
    bool show_network_stats = true;
    bool show_rollback_info = true;
    bool show_reconciliation_visual = false;
    
    // Statistics window state
    bool stats_window_open = true;
    
    // Graph display
    bool show_latency_graph = false;
    bool show_bandwidth_graph = false;
    
    // Debug visualization scale
    float reconciliation_visual_scale = 1.0f;
};

/**
 * @class NetworkSystemPanel
 * @brief Inspector panel for network system monitoring and debugging
 * 
 * Features:
 * - Real-time tick counter display
 * - Network latency and jitter measurement
 * - Input buffer status visualization
 * - Rollback event logging and indicators
 * - Reconciliation progress display
 * - Bandwidth usage metrics
 * - State history visualization
 */
class NetworkSystemPanel {
public:
    NetworkSystemPanel();
    ~NetworkSystemPanel();
    
    /**
     * Render inspector panel for network system
     * @param network_manager Pointer to network manager to monitor (optional)
     */
    void Render(engine::network::NetworkManager* network_manager);
    
    /**
     * Render viewport debug visualization for network reconciliation
     * @param network_manager Network manager with reconciliation data
     * @param viewport_width Width of viewport
     * @param viewport_height Height of viewport
     */
    void RenderViewportDebug(engine::network::NetworkManager* network_manager,
                            int viewport_width, int viewport_height);
    
    /**
     * Render simulation tick information
     * @param simulation Deterministic simulation to display
     */
    void RenderTickInfo(engine::network::DeterministicSimulation* simulation);
    
    /**
     * Render network statistics display
     * @param network_manager Network manager with stats
     */
    void RenderNetworkStats(engine::network::NetworkManager* network_manager);
    
    /**
     * Render rollback information panel
     * @param rollback_manager Rollback manager with history
     */
    void RenderRollbackInfo(engine::network::RollbackManager* rollback_manager);
    
    /**
     * Render input buffer status
     * @param network_manager Network manager with input buffer
     */
    void RenderInputBufferStatus(engine::network::NetworkManager* network_manager);
    
    /**
     * Update recorded statistics (latency, bandwidth, etc.)
     * @param network_manager Network manager to sample from
     */
    void UpdateStatistics(engine::network::NetworkManager* network_manager);
    
    /**
     * Render network simulation control tools
     * @param network_manager Network manager to inject conditions into
     */
    void RenderNetworkSimulationControls(engine::network::NetworkManager* network_manager);
    
    /**
     * Render network packet inspection tools
     * @param network_manager Network manager with packet data
     */
    void RenderNetworkPacketInspection(engine::network::NetworkManager* network_manager);
    
    /**
     * Get current editor state
     */
    const NetworkSystemState& GetState() const { return state_; }

private:
    NetworkSystemState state_;
    
    // Statistics tracking
    struct NetworkStats {
        float latency_ms = 0.0f;
        float jitter_ms = 0.0f;
        float packet_loss = 0.0f;
        float bandwidth_kbps = 0.0f;
        uint64_t last_tick = 0;
        uint32_t rollback_count = 0;
        float reconciliation_progress = 0.0f;
    };
    NetworkStats current_stats_;
    
    // Helper methods
    void RenderLatencyGraph();
    void RenderBandwidthGraph();
    void RenderReconciliationStatus(engine::network::NetworkManager* network_manager);
    void RenderStateHistoryVisualization();
};

} // namespace schizo::editor
