#include "ai_assistant_panel.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>
#include <thread>

namespace fs = std::filesystem;

namespace schizo::editor {
namespace {

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

bool write_file(const fs::path& path, const std::string& value) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
    return static_cast<bool>(out);
}

std::string shell_quote(const std::string& value) {
#ifdef _WIN32
    std::string result = "\"";
    for (char c : value) {
        if (c == '"') result += "\\\"";
        else result += c;
    }
    return result + "\"";
#else
    std::string result = "'";
    for (char c : value) {
        if (c == '\'') result += "'\\''";
        else result += c;
    }
    return result + "'";
#endif
}

fs::path make_run_directory() {
    std::random_device random;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 16; ++attempt) {
        const fs::path candidate = fs::temp_directory_path() /
            ("worldshaper-agent-" + std::to_string(stamp) + "-" +
             std::to_string(random()) + "-" + std::to_string(attempt));
        std::error_code ec;
        if (fs::create_directory(candidate, ec)) return candidate;
    }
    return {};
}

std::string clipped(std::string value, size_t limit = 5000) {
    if (value.size() <= limit) return value;
    value.resize(limit);
    value += "\n... output shortened ...";
    return value;
}

const char* provider_name(AiProvider provider) {
    return provider == AiProvider::Codex ? "Codex" : "Claude Code";
}

void readonly_text(const char* id, const std::string& text, float height) {
    ImGui::InputTextMultiline(id, const_cast<char*>(text.c_str()), text.size() + 1,
                              ImVec2(-1.0f, height), ImGuiInputTextFlags_ReadOnly);
}

}  // namespace

AiAssistantPanel::AiAssistantPanel() {
    std::snprintf(prompt_, sizeof(prompt_),
                  "Build a small playable area around the selected entity.");
}

AiAssistantPanel::~AiAssistantPanel() = default;

AiAssistantPanel::ProviderResult AiAssistantPanel::run_provider(
    AiProvider provider, const std::string& prompt) {
    ProviderResult result;
    const fs::path run_dir = make_run_directory();
    if (run_dir.empty()) {
        result.error = "Could not create the isolated provider workspace.";
        return result;
    }

    const fs::path prompt_path = run_dir / "request.txt";
    const fs::path schema_path = run_dir / "response-schema.json";
    const fs::path response_path = run_dir / "response.json";
    const fs::path log_path = run_dir / "provider.log";
    const std::string schema = EngineAgentOutputSchemaJson();
    if (!write_file(prompt_path, prompt) || !write_file(schema_path, schema)) {
        result.error = "Could not prepare the isolated provider request.";
        std::error_code ec;
        fs::remove_all(run_dir, ec);
        return result;
    }

    // The prompt is piped through stdin, never interpolated into the shell.
    // The subprocess starts in a unique empty folder and only writes its JSON
    // answer there. Neither command receives the project or engine path.
    std::string command = "cd " + shell_quote(run_dir.string()) + " && ";
    if (provider == AiProvider::Codex) {
#ifdef __linux__
        // On Linux, bubblewrap hides the entire real home directory (including
        // this engine checkout) and clears inherited environment secrets. The
        // only writable/visible workspace is this one-request temp folder.
        if (fs::is_regular_file("/usr/bin/bwrap")) {
            fs::path codex_home;
            if (const char* configured = std::getenv("CODEX_HOME"))
                codex_home = configured;
            else if (const char* home = std::getenv("HOME"))
                codex_home = fs::path(home) / ".codex";
            const fs::path auth = codex_home / "auth.json";

            command +=
                "/usr/bin/bwrap --die-with-parent --new-session --unshare-pid "
                "--ro-bind /usr /usr --ro-bind /etc /etc --ro-bind /run /run "
                "--proc /proc --dev /dev --tmpfs /home --tmpfs /tmp "
                "--dir /home/agent --dir /home/agent/.codex ";
            if (fs::is_regular_file(auth))
                command += "--ro-bind " + shell_quote(auth.string()) +
                           " /home/agent/.codex/auth.json ";
            command +=
                "--bind " + shell_quote(run_dir.string()) + " /work --chdir /work "
                "--clearenv --setenv PATH /usr/bin:/usr/lib/chatgpt/resources "
                "--setenv HOME /home/agent --setenv CODEX_HOME /home/agent/.codex "
                "--setenv LANG C.UTF-8 codex --ask-for-approval never exec "
                "--ephemeral --skip-git-repo-check --ignore-user-config --ignore-rules "
                "--sandbox read-only --output-schema /work/response-schema.json "
                "-C /work -o /work/response.json - < /work/request.txt "
                "> /work/provider.log 2>&1";
        } else
#endif
        {
        command +=
            "codex --ask-for-approval never exec --ephemeral --skip-git-repo-check --ignore-user-config "
            "--ignore-rules --sandbox read-only "
            "--output-schema " + shell_quote(schema_path.string()) +
            " -C " + shell_quote(run_dir.string()) +
            " -o " + shell_quote(response_path.string()) +
            " - < " + shell_quote(prompt_path.string()) +
            " > " + shell_quote(log_path.string()) + " 2>&1";
        }
    } else {
        // Claude gets no tools and no MCP servers. Plan mode is an additional
        // safety layer; the JSON schema keeps its result provider-neutral.
        command +=
            "claude -p --no-session-persistence --disable-slash-commands "
            "--no-chrome --strict-mcp-config --mcp-config " + shell_quote("{}") +
            " --tools \"\" --permission-mode plan --output-format json "
            "--json-schema " + shell_quote(schema) +
            " < " + shell_quote(prompt_path.string()) +
            " > " + shell_quote(response_path.string()) +
            " 2> " + shell_quote(log_path.string());
    }

    const int exit_code = std::system(command.c_str());
    result.output = read_file(response_path);
    const std::string log = clipped(read_file(log_path));
    if (exit_code != 0 || result.output.empty()) {
        result.error = std::string(provider_name(provider)) +
            " could not create a plan. Make sure its CLI is installed and signed in.";
        if (!log.empty()) result.error += "\n\n" + log;
    } else {
        result.ok = true;
    }

    std::error_code ec;
    fs::remove_all(run_dir, ec);
    return result;
}

void AiAssistantPanel::start_request(
    const std::shared_ptr<schizo::scene::Scene>& scene,
    uint32_t selected_entity_id) {
    pending_plan_.reset();
    error_.clear();
    outgoing_scene_summary_ = BuildEngineAgentSceneSnapshot(scene, selected_entity_id);
    planned_scene_fingerprint_ = BuildEngineAgentSceneSnapshot(scene, 0);
    const std::string full_prompt = BuildEngineAgentPrompt(prompt_, outgoing_scene_summary_);
    const AiProvider selected_provider = provider_;
    running_ = true;
    status_ = std::string(provider_name(provider_)) + " is preparing a proposal...";
    future_ = std::async(std::launch::async,
                         [selected_provider, full_prompt] {
                             return run_provider(selected_provider, full_prompt);
                         });
}

void AiAssistantPanel::poll_request() {
    if (!running_ || !future_.valid() ||
        future_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        return;

    running_ = false;
    ProviderResult result = future_.get();
    if (!result.ok) {
        status_.clear();
        error_ = std::move(result.error);
        return;
    }

    EngineAgentPlan plan;
    std::string parse_error;
    if (!ParseEngineAgentPlan(result.output, plan, parse_error)) {
        status_.clear();
        error_ = parse_error;
        return;
    }
    status_ = "Proposal ready. Review it before applying.";
    pending_plan_ = std::move(plan);
}

void AiAssistantPanel::Render(const EngineAgentApplyContext& context,
                              uint32_t selected_entity_id,
                              bool* open) {
    poll_request();
    if (!ImGui::Begin("AI Assistant", open)) {
        ImGui::End();
        return;
    }

    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.95f, 1.0f), "ENGINE AUTHOR MODE");
    ImGui::SameLine();
    ImGui::TextDisabled("proposal -> validation -> your approval");
    ImGui::Separator();

    int provider_index = provider_ == AiProvider::Codex ? 0 : 1;
    const char* providers[] = {"Codex", "Claude Code"};
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::Combo("Provider", &provider_index, providers, 2))
        provider_ = provider_index == 0 ? AiProvider::Codex : AiProvider::Claude;

    ImGui::TextUnformatted("Describe what should change in the game:");
    ImGui::InputTextMultiline("##ai_request", prompt_, sizeof(prompt_),
                              ImVec2(-1.0f, 90.0f));

    const bool can_request = context.scene && context.undo &&
                             !context.project_root.empty() && prompt_[0] != '\0' && !running_;
    ImGui::BeginDisabled(!can_request);
    if (ImGui::Button("Prepare Plan", ImVec2(140.0f, 0.0f)))
        start_request(context.scene, selected_entity_id);
    ImGui::EndDisabled();
    if (running_) {
        ImGui::SameLine();
        ImGui::TextDisabled("Working...");
    }

    if (!status_.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", status_.c_str());
    }
    if (!error_.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Provider error");
        ImGui::TextWrapped("%s", error_.c_str());
    }

    if (!outgoing_scene_summary_.empty() &&
        ImGui::CollapsingHeader("What was sent (scene summary)")) {
        ImGui::TextDisabled("Your request plus exactly this reduced snapshot; no source files.");
        readonly_text("##sent_snapshot", outgoing_scene_summary_, 150.0f);
    }

    if (pending_plan_) {
        ImGui::SeparatorText("Proposed changes");
        ImGui::TextWrapped("%s", pending_plan_->message.c_str());
        for (size_t i = 0; i < pending_plan_->actions.size(); ++i) {
            const auto& action = pending_plan_->actions[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::BulletText("%s", DescribeEngineAgentAction(action).c_str());
            if (!action.content.empty() && ImGui::TreeNode("Preview generated file")) {
                readonly_text("##generated_content", action.content, 150.0f);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        const bool scene_changed =
            BuildEngineAgentSceneSnapshot(context.scene, 0) != planned_scene_fingerprint_;
        if (scene_changed) {
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f),
                               "The scene changed after this proposal. Prepare a fresh plan.");
        }

        ImGui::BeginDisabled(scene_changed);
        if (ImGui::Button("Apply Changes", ImVec2(140.0f, 0.0f))) {
            std::string apply_error;
            if (ApplyEngineAgentPlan(*pending_plan_, context, apply_error)) {
                status_ = "Changes applied as one Ctrl+Z undo step.";
                pending_plan_.reset();
                error_.clear();
            } else {
                error_ = apply_error;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Discard")) {
            pending_plan_.reset();
            status_ = "Proposal discarded. Nothing was changed.";
            error_.clear();
        }
    }

    ImGui::SeparatorText("Hard limits");
    ImGui::TextWrapped(
        "The model cannot edit engine/editor code, use the terminal, create native code, "
        "or apply changes by itself. It can only propose scene actions, sandboxed Python "
        "scripts, and small OBJ models. Every proposal is validated here first.");
    ImGui::End();
}

}  // namespace schizo::editor
