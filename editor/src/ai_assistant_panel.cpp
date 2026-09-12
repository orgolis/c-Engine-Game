#include "ai_assistant_panel.h"
#include "project_paths.h"
#include "secure_credential_store.h"

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
#include <utility>

#ifndef _WIN32
#include <cerrno>
#include <sys/stat.h>
#endif

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
#ifdef _WIN32
        std::error_code ec;
        if (fs::create_directory(candidate, ec)) return candidate;
#else
        // Create atomically as owner-only. This folder briefly contains the
        // reduced scene request and provider answer, but never credentials.
        if (::mkdir(candidate.c_str(), S_IRWXU) == 0) return candidate;
        if (errno != EEXIST) return {};
#endif
    }
    return {};
}

std::string json_string(const std::string& value) {
    std::string result = "\"";
    for (const unsigned char c : value) {
        switch (c) {
            case '\"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (c < 0x20) {
                    constexpr char hex[] = "0123456789abcdef";
                    result += "\\u00";
                    result += hex[(c >> 4) & 0x0f];
                    result += hex[c & 0x0f];
                } else {
                    result += static_cast<char>(c);
                }
        }
    }
    return result + "\"";
}

const char* provider_name(AiProvider provider) {
    return provider == AiProvider::Codex ? "Codex" : "Claude Code";
}

fs::path find_executable(const std::string& name) {
    const char* raw_path = std::getenv("PATH");
    if (!raw_path) return {};
#ifdef _WIN32
    constexpr char separator = ';';
    const char* extensions[] = {"", ".exe", ".cmd", ".bat"};
#else
    constexpr char separator = ':';
    const char* extensions[] = {""};
#endif
    std::stringstream paths(raw_path);
    std::string directory;
    while (std::getline(paths, directory, separator)) {
        if (directory.empty()) continue;
        for (const char* extension : extensions) {
            std::error_code ec;
            const fs::path candidate = fs::path(directory) / (name + extension);
            if (fs::is_regular_file(candidate, ec)) return candidate;
        }
    }
    return {};
}

fs::path credential_helper_path() {
    fs::path path = executable_dir() / "worldshaper-credential-helper";
#ifdef _WIN32
    path += ".exe";
#endif
    return path;
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

AiAssistantPanel::~AiAssistantPanel() {
    secure_credentials::Erase(anthropic_api_key_, sizeof(anthropic_api_key_));
}

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
            const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
            const char* session_bus = std::getenv("DBUS_SESSION_BUS_ADDRESS");
            if (!runtime_dir || !session_bus) {
                result.error =
                    "The encrypted OS credential vault is unavailable in the sandbox.";
                std::error_code ec;
                fs::remove_all(run_dir, ec);
                return result;
            }

            command +=
                "/usr/bin/bwrap --die-with-parent --new-session --unshare-pid "
                "--ro-bind /usr /usr --ro-bind /etc /etc --ro-bind /run /run "
                "--proc /proc --dev /dev --tmpfs /home --tmpfs /tmp "
                "--dir /home/agent --dir /home/agent/.codex ";
            command +=
                "--bind " + shell_quote(run_dir.string()) + " /work --chdir /work "
                "--clearenv --setenv PATH /usr/bin:/usr/lib/chatgpt/resources "
                "--setenv HOME /home/agent --setenv CODEX_HOME /home/agent/.codex "
                "--setenv XDG_RUNTIME_DIR " + shell_quote(runtime_dir) + " "
                "--setenv DBUS_SESSION_BUS_ADDRESS " + shell_quote(session_bus) + " "
                "--setenv LANG C.UTF-8 codex "
                "-c cli_auth_credentials_store=keyring "
                "--ask-for-approval never exec "
                "--ephemeral --skip-git-repo-check --ignore-user-config --ignore-rules "
                "--sandbox read-only --output-schema /work/response-schema.json "
                "-C /work -o /work/response.json - < /work/request.txt "
                "> /dev/null 2>&1";
        } else
#endif
        {
        command +=
            "codex -c cli_auth_credentials_store=keyring "
            "--ask-for-approval never exec --ephemeral --skip-git-repo-check --ignore-user-config "
            "--ignore-rules --sandbox read-only "
            "--output-schema " + shell_quote(schema_path.string()) +
            " -C " + shell_quote(run_dir.string()) +
            " -o " + shell_quote(response_path.string()) +
            " - < " + shell_quote(prompt_path.string()) +
            " > " + shell_quote(
#ifdef _WIN32
                "NUL"
#else
                "/dev/null"
#endif
            ) + " 2>&1";
        }
    } else {
        const fs::path helper = credential_helper_path();
        if (!fs::is_regular_file(helper)) {
            result.error = "The encrypted credential helper is missing.";
            std::error_code ec;
            fs::remove_all(run_dir, ec);
            return result;
        }

        // --bare prevents Claude from loading OAuth credentials, settings,
        // hooks, skills, or MCP configuration from the user's home/project.
        // apiKeyHelper retrieves the API key from the OS vault directly into
        // Claude's stdin pipe; the key is never in argv, env, or a file.
        const std::string helper_command =
            shell_quote(helper.string()) + " anthropic";
        const std::string settings =
            "{\"apiKeyHelper\":" + json_string(helper_command) + "}";
        command +=
            "claude --bare -p --no-session-persistence --disable-slash-commands "
            "--no-chrome --strict-mcp-config --mcp-config " + shell_quote("{}") +
            " --settings " + shell_quote(settings) +
            " --tools \"\" --permission-mode plan --output-format json "
            "--json-schema " + shell_quote(schema) +
            " < " + shell_quote(prompt_path.string()) +
            " > " + shell_quote(response_path.string()) +
            " 2> " + shell_quote(
#ifdef _WIN32
                "NUL"
#else
                "/dev/null"
#endif
            );
    }

    const int exit_code = std::system(command.c_str());
    result.output = read_file(response_path);
    if (exit_code != 0 || result.output.empty()) {
        result.error = std::string(provider_name(provider)) +
            " could not create a plan. Check the CLI and encrypted account connection.";
    } else {
        result.ok = true;
    }

    std::error_code ec;
    fs::remove_all(run_dir, ec);
    return result;
}

AiAssistantPanel::AuthResult AiAssistantPanel::run_auth_command(
    AiProvider provider, bool login) {
    AuthResult result;
    const fs::path executable =
        find_executable(provider == AiProvider::Codex ? "codex" : "claude");
    if (executable.empty()) {
        result.detail = std::string(provider_name(provider)) + " CLI is not installed.";
        return result;
    }
    result.cli_available = true;

    if (provider == AiProvider::Claude) {
        result.secure_store_available = secure_credentials::Available();
        result.signed_in = result.secure_store_available &&
                           secure_credentials::HasAnthropicApiKey();
        if (!result.secure_store_available) {
            result.detail = "The encrypted OS credential vault is unavailable.";
        } else if (result.signed_in) {
            result.detail = "Connected using encrypted OS credential storage.";
        } else {
            result.detail = "No encrypted Anthropic API key is stored.";
        }
        return result;
    }

    // Codex is always forced to the native keyring. Unlike "auto", this
    // fails closed instead of falling back to a plaintext credential file.
    result.secure_store_available = true;

    const fs::path run_dir = make_run_directory();
    if (run_dir.empty()) {
        result.detail = "Could not create a temporary login workspace.";
        return result;
    }
    auto run = [&](const std::string& arguments) {
#ifdef _WIN32
        std::string command = "cd /d " + shell_quote(run_dir.string()) + " && ";
        constexpr const char* null_device = "NUL";
#else
        std::string command = "cd " + shell_quote(run_dir.string()) + " && ";
        constexpr const char* null_device = "/dev/null";
#endif
        command += shell_quote(executable.string()) + arguments +
                   " > " + shell_quote(null_device) + " 2>&1";
        return std::system(command.c_str());
    };

    if (login) {
        // The official Codex browser flow receives the account credentials;
        // the editor never sees them. Only the OS keyring may persist them.
        const int exit_code =
            run(" -c cli_auth_credentials_store=keyring login");
        if (exit_code != 0) {
            result.detail =
                "The secure browser sign-in did not complete. No credential was saved.";
            std::error_code ec;
            fs::remove_all(run_dir, ec);
            return result;
        }
    }

    const int status_code =
        run(" -c cli_auth_credentials_store=keyring login status");
    result.signed_in = status_code == 0;
    result.detail = result.signed_in
        ? "Connected using encrypted OS credential storage."
        : "Not connected, or encrypted OS credential storage is unavailable.";

    std::error_code ec;
    fs::remove_all(run_dir, ec);
    return result;
}

void AiAssistantPanel::start_auth_check() {
    if (auth_running_) return;
    const AiProvider selected_provider = provider_;
    auth_running_ = true;
    auth_login_running_ = false;
    auth_checked_ = false;
    auth_future_ = std::async(std::launch::async, [selected_provider] {
        return run_auth_command(selected_provider, false);
    });
}

void AiAssistantPanel::start_login() {
    if (auth_running_ || provider_ != AiProvider::Codex) return;
    const AiProvider selected_provider = provider_;
    auth_running_ = true;
    auth_login_running_ = true;
    auth_checked_ = false;
    auth_future_ = std::async(std::launch::async, [selected_provider] {
        return run_auth_command(selected_provider, true);
    });
}

void AiAssistantPanel::poll_auth() {
    if (!auth_running_ || !auth_future_.valid() ||
        auth_future_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        return;
    auth_ = auth_future_.get();
    auth_running_ = false;
    auth_login_running_ = false;
    auth_checked_ = true;
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
    poll_auth();
    if (!ImGui::Begin("AI Assistant", open)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("AI Scene Assistant");
    ImGui::TextDisabled("Describe the result. Review the safe plan. Apply it yourself.");
    ImGui::SeparatorText("1. Account");

    ImGui::BeginDisabled(running_ || auth_running_);
    int provider_index = provider_ == AiProvider::Codex ? 0 : 1;
    const char* providers[] = {"Codex (OpenAI)", "Claude (Anthropic Console)"};
    ImGui::SetNextItemWidth(250.0f);
    if (ImGui::Combo("##ai_provider", &provider_index, providers, 2)) {
        secure_credentials::Erase(anthropic_api_key_, sizeof(anthropic_api_key_));
        provider_ = provider_index == 0 ? AiProvider::Codex : AiProvider::Claude;
        auth_ = {};
        auth_checked_ = false;
        pending_plan_.reset();
        status_.clear();
        error_.clear();
    }
    ImGui::EndDisabled();

    if (!auth_checked_ && !auth_running_) start_auth_check();

    const float account_height =
        provider_ == AiProvider::Codex ? 104.0f : 164.0f;
    ImGui::BeginChild("##ai_account", ImVec2(0.0f, account_height), true);
    if (auth_running_) {
        ImGui::TextColored(ImVec4(0.35f, 0.75f, 0.95f, 1.0f), "%s",
                           auth_login_running_
                               ? "Complete the sign-in in your browser..."
                               : "Checking account...");
    } else if (auth_.signed_in) {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.48f, 1.0f), "Connected");
    } else if (auth_.cli_available && !auth_.secure_store_available) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                           "Encrypted credential vault unavailable");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f),
                           auth_.cli_available ? "Not connected" : "CLI not installed");
    }

    if (provider_ == AiProvider::Codex) {
        ImGui::BeginDisabled(auth_running_ || !auth_.cli_available ||
                             !auth_.secure_store_available);
        if (ImGui::Button("Sign in with ChatGPT / Google")) start_login();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(auth_running_);
        if (ImGui::SmallButton("Refresh")) start_auth_check();
        ImGui::EndDisabled();
        ImGui::TextDisabled(
            "The official browser handles sign-in. Storage is forced to the encrypted OS keyring.");
    } else {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint(
            "##anthropic_api_key", "Anthropic Console API key",
            anthropic_api_key_, sizeof(anthropic_api_key_),
            ImGuiInputTextFlags_Password);

        ImGui::BeginDisabled(auth_running_ || !auth_.secure_store_available ||
                             anthropic_api_key_[0] == '\0');
        if (ImGui::Button("Store encrypted")) {
            std::string secret(anthropic_api_key_);
            std::string store_error;
            if (secure_credentials::StoreAnthropicApiKey(secret, store_error)) {
                status_ = "Anthropic API key stored in the encrypted OS vault.";
                error_.clear();
            } else {
                error_ = std::move(store_error);
            }
            secure_credentials::Erase(secret);
            secure_credentials::Erase(
                anthropic_api_key_, sizeof(anthropic_api_key_));
            start_auth_check();
        }
        ImGui::EndDisabled();

        if (auth_.signed_in) {
            ImGui::SameLine();
            ImGui::BeginDisabled(auth_running_);
            if (ImGui::Button("Remove stored key")) {
                std::string remove_error;
                if (secure_credentials::RemoveAnthropicApiKey(remove_error)) {
                    status_ = "Anthropic API key removed from the OS vault.";
                    error_.clear();
                } else {
                    error_ = std::move(remove_error);
                }
                start_auth_check();
            }
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(auth_running_);
        if (ImGui::SmallButton("Refresh")) start_auth_check();
        ImGui::EndDisabled();
        ImGui::TextDisabled(
            "Saved only in Windows Credential Manager, macOS Keychain, or Linux Secret Service.");
    }
    ImGui::EndChild();

    ImGui::SeparatorText("2. Describe");

    if (context.scene) {
        if (selected_entity_id)
            ImGui::TextDisabled("Scene ready - selected entity #%u", selected_entity_id);
        else
            ImGui::TextDisabled("Scene ready - no entity selected");
    } else {
        ImGui::TextDisabled("Open a project and scene to begin.");
    }
    ImGui::InputTextMultiline("##ai_request", prompt_, sizeof(prompt_),
                              ImVec2(-1.0f, 110.0f));

    const bool can_request = context.scene && context.undo &&
                             !context.project_root.empty() && prompt_[0] != '\0' &&
                             auth_checked_ && auth_.signed_in && !running_;
    ImGui::BeginDisabled(!can_request);
    if (ImGui::Button("Create proposal", ImVec2(150.0f, 0.0f)))
        start_request(context.scene, selected_entity_id);
    ImGui::EndDisabled();
    if (running_) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s is preparing a safe plan...", provider_name(provider_));
    } else if (!auth_.signed_in) {
        ImGui::SameLine();
        ImGui::TextDisabled("Connect an account first.");
    }

    if (!status_.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.48f, 1.0f), "%s", status_.c_str());
    }
    if (!error_.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Needs attention");
        ImGui::TextWrapped("%s", error_.c_str());
    }

    ImGui::SeparatorText("3. Review");

    if (pending_plan_) {
        ImGui::TextWrapped("%s", pending_plan_->message.c_str());
        ImGui::TextDisabled("%zu proposed action%s",
                            pending_plan_->actions.size(),
                            pending_plan_->actions.size() == 1 ? "" : "s");
        ImGui::Spacing();
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
    } else if (running_) {
        ImGui::TextDisabled("The proposal will appear here when it is ready.");
    } else {
        ImGui::TextDisabled("No proposal yet.");
    }

    if (!outgoing_scene_summary_.empty() &&
        ImGui::CollapsingHeader("Data sent to the provider")) {
        ImGui::TextDisabled("Your request plus this reduced snapshot; no source files or script contents.");
        readonly_text("##sent_snapshot", outgoing_scene_summary_, 150.0f);
    }

    if (ImGui::CollapsingHeader("Safety limits")) {
        ImGui::TextWrapped(
            "The model cannot edit engine/editor code, use the terminal, create native code, "
            "or apply changes by itself. It can only propose scene actions, sandboxed Python "
            "scripts, and small OBJ models. Every proposal is validated locally first.");
    }
    ImGui::End();
}

}  // namespace schizo::editor
