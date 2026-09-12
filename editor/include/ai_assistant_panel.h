#pragma once

#include "engine_agent_gateway.h"

#include <future>
#include <optional>
#include <string>

namespace schizo::editor {

enum class AiProvider {
    Codex,
    Claude
};

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

    void Render(const EngineAgentApplyContext& context,
                uint32_t selected_entity_id,
                bool* open = nullptr);

private:
    struct ProviderResult {
        bool ok = false;
        std::string output;
        std::string error;
    };

    struct AuthResult {
        bool cli_available = false;
        bool signed_in = false;
        std::string detail;
    };

    void start_request(const std::shared_ptr<schizo::scene::Scene>& scene,
                       uint32_t selected_entity_id);
    void poll_request();
    void start_auth_check();
    void start_login();
    void poll_auth();
    static ProviderResult run_provider(AiProvider provider,
                                       const std::string& prompt);
    static AuthResult run_auth_command(AiProvider provider, bool login);

    AiProvider provider_ = AiProvider::Codex;
    char prompt_[4096]{};
    bool running_ = false;
    std::future<ProviderResult> future_;
    bool auth_running_ = false;
    bool auth_checked_ = false;
    bool auth_login_running_ = false;
    std::future<AuthResult> auth_future_;
    AuthResult auth_;
    std::optional<EngineAgentPlan> pending_plan_;
    std::string planned_scene_fingerprint_;
    std::string outgoing_scene_summary_;
    std::string status_;
    std::string error_;
};

}  // namespace schizo::editor
