#pragma once

#include <cstdint>
#include <future>
#include <optional>
#include <string>
#include <vector>

#include "ai_runtime_manager.h"
#include "engine_agent_gateway.h"

namespace schizo::editor {

enum class AiProvider { Codex, Claude };

/**
 * Dockable, provider-neutral AI authoring UI.
 *
 * The provider subprocess only receives a text snapshot and must return a
 * structured plan. It never touches the live scene. The plan is shown to the
 * user and only reaches EngineAgentGateway after an explicit Apply click.
 */
class AiAssistantPanel {
public:
    AiAssistantPanel();
    ~AiAssistantPanel();

    AiAssistantPanel(const AiAssistantPanel&) = delete;
    AiAssistantPanel& operator=(const AiAssistantPanel&) = delete;

    void Render(const EngineAgentApplyContext& context, uint32_t selected_entity_id, bool* open = nullptr);

private:
    struct ProviderResult {
        bool ok = false;
        std::string output;
        std::string error;
        uint64_t input_tokens = 0;
        uint64_t output_tokens = 0;
        double cost_usd = 0.0;
    };

    struct ModelOption {
        std::string id;
        std::string label;
    };

    struct UsageWindow {
        std::string label;
        int used_percent = -1;
        int64_t resets_at = 0;
    };

    struct AuthResult {
        bool cli_available = false;
        bool secure_store_available = false;
        bool signed_in = false;
        std::string detail;
        std::string plan;
        std::string credit_balance;
        std::vector<ModelOption> models;
        std::vector<UsageWindow> usage_windows;
    };

    void start_request(const std::shared_ptr<schizo::scene::Scene>& scene, uint32_t selected_entity_id);
    void poll_request();
    void start_auth_check();
    void start_login();
    void poll_auth();
    void start_runtime_install();
    void poll_runtime_install();
    static ProviderResult run_provider(AiProvider provider, const std::string& model, const std::string& prompt);
    static AuthResult run_auth_command(AiProvider provider, bool login);

    AiProvider provider_ = AiProvider::Codex;
    char prompt_[4096]{};
    char anthropic_api_key_[513]{};
    bool running_ = false;
    std::future<ProviderResult> future_;
    bool auth_running_ = false;
    bool auth_checked_ = false;
    bool auth_login_running_ = false;
    std::future<AuthResult> auth_future_;
    AuthResult auth_;
    bool runtime_installing_ = false;
    std::future<AiRuntimeInstallResult> runtime_future_;
    std::string codex_model_;
    std::string claude_model_ = "sonnet";
    uint64_t session_input_tokens_ = 0;
    uint64_t session_output_tokens_ = 0;
    double session_cost_usd_ = 0.0;
    std::optional<EngineAgentPlan> pending_plan_;
    std::string planned_scene_fingerprint_;
    std::string outgoing_scene_summary_;
    std::string status_;
    std::string error_;
};

}  // namespace schizo::editor
