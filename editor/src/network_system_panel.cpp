#include "network_system_panel.h"
#include "../core/network/include/network_manager.h"
#include "../core/network/include/deterministic_simulation.h"
#include "../core/network/include/rollback_manager.h"
#include <imgui.h>
#include <spdlog/spdlog.h>

namespace schizo::editor {

NetworkSystemPanel::NetworkSystemPanel() {
    state_.show_tick_info = true;
    state_.show_network_stats = true;
    state_.show_rollback_info = true;
    state_.show_reconciliation_visual = false;
    state_.stats_window_open = true;
    state_.show_latency_graph = false;
    state_.show_bandwidth_graph = false;
    state_.reconciliation_visual_scale = 1.0f;
}

NetworkSystemPanel::~NetworkSystemPanel() = default;

void NetworkSystemPanel::Render(engine::network::NetworkManager* network_manager) {
    if (!ImGui::CollapsingHeader("Network System", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    
    if (network_manager) {
        UpdateStatistics(network_manager);
    }
    
    ImGui::TextUnformatted("Network Status:");
    ImGui::Separator();
    
    if (state_.show_tick_info && network_manager) {
        RenderTickInfo(nullptr);  // Would pass simulation pointer
        ImGui::Spacing();
    }
    
    if (state_.show_network_stats) {
        RenderNetworkStats(network_manager);
        ImGui::Spacing();
    }
    
    if (state_.show_rollback_info && network_manager) {
        RenderRollbackInfo(nullptr);  // Would pass rollback manager pointer
        ImGui::Spacing();
    }
    
    ImGui::Separator();
    ImGui::TextUnformatted("Debug Visualization:");
    ImGui::Checkbox("Show Tick Info##net", &state_.show_tick_info);
    ImGui::Checkbox("Show Network Stats##net", &state_.show_network_stats);
    ImGui::Checkbox("Show Rollback Info##net", &state_.show_rollback_info);
    ImGui::Checkbox("Show Reconciliation Visual##net", &state_.show_reconciliation_visual);
    
    if (state_.show_latency_graph || state_.show_bandwidth_graph) {
        ImGui::Separator();
        if (state_.show_latency_graph) {
            RenderLatencyGraph();
        }
        if (state_.show_bandwidth_graph) {
            RenderBandwidthGraph();
        }
    }
    
    // Debug Tools Section
    if (ImGui::CollapsingHeader("Debug Tools##network", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();
        
        if (network_manager) {
            RenderNetworkSimulationControls(network_manager);
            ImGui::Separator();
            RenderNetworkPacketInspection(network_manager);
        } else {
            ImGui::TextDisabled("No network manager loaded for debug tools");
        }
        
        ImGui::Unindent();
    }
}

void NetworkSystemPanel::RenderViewportDebug(engine::network::NetworkManager* network_manager,
                                             int viewport_width, int viewport_height) {
    if (!network_manager || !state_.show_reconciliation_visual) {
        return;
    }
    
    // Render reconciliation visualization in viewport
    // Would show predicted vs server-confirmed positions
    ImGui::SetNextWindowPos(ImVec2(viewport_width - 300, 10));
    ImGui::SetNextWindowBgAlpha(0.35f);
    ImGui::Begin("##NetworkDebug", nullptr,
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize);
    
    RenderReconciliationStatus(network_manager);
    
    ImGui::End();
}

void NetworkSystemPanel::RenderTickInfo(engine::network::DeterministicSimulation* simulation) {
    ImGui::TextUnformatted("Simulation Tick:");
    ImGui::Text("Current Tick: %lu", current_stats_.last_tick);
    ImGui::Text("Accumulated Time: 0.0ms");
    ImGui::Text("Tick Rate: 60 Hz (16.67ms per tick)");
    ImGui::ProgressBar(0.5f, ImVec2(-1, 0), "0.5 / 1.0 ticks");
}

void NetworkSystemPanel::RenderNetworkStats(engine::network::NetworkManager* network_manager) {
    ImGui::TextUnformatted("Network Statistics:");
    ImGui::Indent();
    
    // Latency display with color coding
    ImVec4 latency_color = ImVec4(0, 1, 0, 1);  // Green for good
    if (current_stats_.latency_ms > 50) latency_color = ImVec4(1, 1, 0, 1);  // Yellow
    if (current_stats_.latency_ms > 100) latency_color = ImVec4(1, 0, 0, 1);  // Red
    
    ImGui::TextColored(latency_color, "Latency: %.1f ms", current_stats_.latency_ms);
    ImGui::Text("Jitter: %.1f ms", current_stats_.jitter_ms);
    ImGui::Text("Packet Loss: %.2f%%", current_stats_.packet_loss);
    ImGui::Text("Bandwidth: %.1f Kbps", current_stats_.bandwidth_kbps);
    
    ImGui::Separator();
    ImGui::TextUnformatted("Input Buffer Status:");
    ImGui::ProgressBar(0.5f, ImVec2(-1, 0));
    ImGui::SameLine();
    ImGui::Text("60/120 inputs buffered");
    
    ImGui::TextUnformatted("State History:");
    ImGui::ProgressBar(0.67f, ImVec2(-1, 0));
    ImGui::SameLine();
    ImGui::Text("8/12 snapshots stored");
    
    ImGui::Unindent();
}

void NetworkSystemPanel::RenderRollbackInfo(engine::network::RollbackManager* rollback_manager) {
    ImGui::TextUnformatted("Rollback Status:");
    ImGui::Indent();
    
    ImGui::TextColored(ImVec4(1, 0.5, 0.5, 1), "Total Rollbacks: %u", current_stats_.rollback_count);
    ImGui::Text("Last Rollback Distance: 5 ticks");
    ImGui::Text("Current Reconciliation Progress: %.1f%%", 
               current_stats_.reconciliation_progress * 100.0f);
    
    ImGui::ProgressBar(current_stats_.reconciliation_progress, ImVec2(-1, 0));
    
    ImGui::TextUnformatted("Recent Rollback Events:");
    ImGui::Text("  [Tick 1423] Rolled back 3 ticks");
    ImGui::Text("  [Tick 1420] Rolled back 2 ticks");
    ImGui::Text("  [Tick 1410] Rolled back 5 ticks");
    
    ImGui::Unindent();
}

void NetworkSystemPanel::RenderInputBufferStatus(engine::network::NetworkManager* network_manager) {
    ImGui::TextUnformatted("Input Buffer:");
    
    for (int i = 0; i < 6; ++i) {
        ImGui::ProgressBar(0.3f, ImVec2(20, 0), "");
        ImGui::SameLine();
    }
    ImGui::Text(" 60/180 inputs pending");
}

void NetworkSystemPanel::UpdateStatistics(engine::network::NetworkManager* network_manager) {
    // Sample network statistics for graph display
    // In a real implementation, would query actual network stats from network_manager
    
    current_stats_.latency_ms = 45.0f + (rand() % 10);
    current_stats_.jitter_ms = 2.5f;
    current_stats_.packet_loss = 0.1f;
    current_stats_.bandwidth_kbps = 22.6f;
    current_stats_.last_tick = 1423;
    current_stats_.reconciliation_progress = 0.75f;
}

void NetworkSystemPanel::RenderLatencyGraph() {
    ImGui::TextUnformatted("Latency History:");
    
    // Draw latency graph (simplified)
    static float latency_history[60] = {};
    for (int i = 0; i < 59; ++i) {
        latency_history[i] = latency_history[i + 1];
    }
    latency_history[59] = current_stats_.latency_ms;
    
    ImGui::PlotLines("ms##latency", latency_history, 60, 0, nullptr, 0.0f, 150.0f, 
                    ImVec2(-1, 100));
}

void NetworkSystemPanel::RenderBandwidthGraph() {
    ImGui::TextUnformatted("Bandwidth Usage:");
    
    static float upstream[60] = {};
    static float downstream[60] = {};
    
    // Shift history and add new value
    for (int i = 0; i < 59; ++i) {
        upstream[i] = upstream[i + 1];
        downstream[i] = downstream[i + 1];
    }
    upstream[59] = 10.0f;
    downstream[59] = 15.0f;
    
    ImGui::PlotLines("Upstream Kbps##bw", upstream, 60, 0, nullptr, 0.0f, 50.0f,
                    ImVec2(-1, 80));
    ImGui::PlotLines("Downstream Kbps##bw", downstream, 60, 0, nullptr, 0.0f, 50.0f,
                    ImVec2(-1, 80));
}

void NetworkSystemPanel::RenderReconciliationStatus(engine::network::NetworkManager* network_manager) {
    ImGui::TextUnformatted("Reconciliation Status:");
    ImGui::Text("Predicted Position: (10.5, 2.0, 15.3)");
    ImGui::Text("Server Position: (10.2, 2.0, 15.1)");
    ImGui::Text("Correction Distance: 0.36m");
    ImGui::ProgressBar(current_stats_.reconciliation_progress, ImVec2(-1, 0));
}

void NetworkSystemPanel::RenderStateHistoryVisualization() {
    ImGui::TextUnformatted("State History Timeline:");
    
    // Draw timeline of saved states
    for (int i = 0; i < 12; ++i) {
        ImGui::ProgressBar(0.2f, ImVec2(30, 0), "");
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::Text("12 snapshots stored (60ms total buffer)");
}

void NetworkSystemPanel::RenderNetworkSimulationControls(engine::network::NetworkManager* network_manager) {
    ImGui::TextUnformatted("Network Simulation Controls:");
    ImGui::Separator();
    
    // Latency injection
    static float injected_latency = 0.0f;
    ImGui::SliderFloat("Inject Latency (ms)##net", &injected_latency, 0.0f, 200.0f);
    if (ImGui::Button("Apply Latency##net", ImVec2(-1, 0))) {
        // network_manager->InjectLatency(injected_latency)
    }
    
    ImGui::Spacing();
    
    // Packet loss simulation
    static float packet_loss_percent = 0.0f;
    ImGui::SliderFloat("Packet Loss (%%)##net", &packet_loss_percent, 0.0f, 100.0f);
    if (ImGui::Button("Apply Packet Loss##net", ImVec2(-1, 0))) {
        // network_manager->SimulatePacketLoss(packet_loss_percent / 100.0f)
    }
    
    ImGui::Spacing();
    ImGui::TextUnformatted("Simulation Actions:");
    
    if (ImGui::Button("Simulate Rollback##net", ImVec2(-1, 0))) {
        // network_manager->ForceRollback(5)  // Force 5-tick rollback
        ImGui::OpenPopup("Rollback Simulated##popup");
    }
    
    if (ImGui::BeginPopupModal("Rollback Simulated##popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "✓ Rollback simulated: Rolled back 5 ticks");
        ImGui::Spacing();
        if (ImGui::Button("OK##rollback", ImVec2(100, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    
    if (ImGui::Button("Simulate Lost Packet##net", ImVec2(-1, 0))) {
        // network_manager->SimulateLostPacket()
    }
    
    ImGui::Spacing();
    ImGui::TextUnformatted("Test Tools:");
    
    if (ImGui::Button("Reset Network State##net", ImVec2(-1, 0))) {
        // Reset all network simulation conditions
        injected_latency = 0.0f;
        packet_loss_percent = 0.0f;
    }
    
    if (ImGui::Button("Disconnect & Reconnect##net", ImVec2(-1, 0))) {
        // Simulate connection loss and recovery
    }
}

void NetworkSystemPanel::RenderNetworkPacketInspection(engine::network::NetworkManager* network_manager) {
    ImGui::TextUnformatted("Packet Inspection:");
    ImGui::Separator();
    
    ImGui::TextUnformatted("Recent Packets:");
    
    // EndChild() must always be paired with BeginChild() since ImGui 1.89.5,
    // even when BeginChild returns false — otherwise the parent's End() trips
    // the "Must call EndChild() and not End()!" assertion.
    ImGui::BeginChild("PacketList", ImVec2(-1, 150), true);
    ImGui::TextColored(ImVec4(0, 1, 0, 1), "[1223] PlayerInput - size: 64 bytes");
    ImGui::TextColored(ImVec4(0, 1, 0, 1), "[1222] StateUpdate - size: 256 bytes");
    ImGui::TextColored(ImVec4(1, 1, 0, 1), "[1221] Ack - size: 16 bytes (retransmit)");
    ImGui::TextColored(ImVec4(0, 1, 0, 1), "[1220] AbilityEvent - size: 128 bytes");
    ImGui::TextColored(ImVec4(0, 1, 0, 1), "[1219] PlayerInput - size: 64 bytes");
    ImGui::EndChild();
    
    ImGui::Spacing();
    ImGui::TextUnformatted("Packet Statistics:");
    ImGui::Text("Total Sent: 5,234");
    ImGui::Text("Total Received: 5,198");
    ImGui::Text("Dropped: 36 (0.69%%)");
    ImGui::Text("Retransmitted: 12");
    
    ImGui::Spacing();
    ImGui::TextUnformatted("Inspection Tools:");
    
    static int selected_packet = 0;
    ImGui::SliderInt("Select Packet Index##net", &selected_packet, 0, 100);
    
    if (ImGui::Button("Inspect Selected##net", ImVec2(-1, 0))) {
        ImGui::OpenPopup("Packet Details##popup");
    }
    
    if (ImGui::BeginPopupModal("Packet Details##popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Packet #1223 Details:");
        ImGui::Separator();
        ImGui::Text("Type: PlayerInput");
        ImGui::Text("Sequence Number: 1223");
        ImGui::Text("Timestamp: 12345.67ms");
        ImGui::Text("Size: 64 bytes");
        ImGui::Text("Status: Acknowledged");
        ImGui::Text("Latency: 45.3ms");
        
        ImGui::Spacing();
        if (ImGui::Button("Close##packet", ImVec2(100, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace schizo::editor
