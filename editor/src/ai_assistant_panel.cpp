#include "ai_assistant_panel.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <system_error>
#include <thread>
#include <utility>

#include "ai_runtime_manager.h"
#include "project_paths.h"
#include "secure_credential_store.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace schizo::editor {
namespace {

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

bool write_file(const fs::path& path, const std::string& value) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
    return static_cast<bool>(out);
}

struct CodexMetadataConversation {
    std::string jsonl;
    std::string pending;
    bool initialized = false;
    bool model_replied = false;
    bool usage_replied = false;

    void accept(const char* bytes, size_t size) {
        pending.append(bytes, size);
        size_t newline = 0;
        while ((newline = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;

            jsonl += line;
            jsonl.push_back('\n');
            const json message = json::parse(line, nullptr, false);
            if (message.is_discarded() || !message.is_object() || !message.contains("id"))
                continue;
            const int id = message.value("id", -1);
            if (id == 0 && message.contains("result"))
                initialized = true;
            else if (id == 1)
                model_replied = true;
            else if (id == 2)
                usage_replied = true;
        }
    }

    bool complete() const { return model_replied && usage_replied; }
};

constexpr const char* kCodexInitializeRequest =
    "{\"id\":0,\"method\":\"initialize\",\"params\":{\"clientInfo\":"
    "{\"name\":\"gameworldshaper\",\"title\":\"GameWorldshaper\",\"version\":\"0.8.5\"}}}\n";
constexpr const char* kCodexMetadataRequests =
    "{\"method\":\"initialized\",\"params\":{}}\n"
    "{\"id\":1,\"method\":\"model/list\",\"params\":{\"limit\":100,\"includeHidden\":false}}\n"
    "{\"id\":2,\"method\":\"account/rateLimits/read\"}\n";

#ifdef _WIN32
bool write_pipe(HANDLE pipe, const char* bytes, size_t size) {
    while (size > 0) {
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(size, 1u << 20));
        DWORD written = 0;
        if (!WriteFile(pipe, bytes, chunk, &written, nullptr) || written == 0)
            return false;
        bytes += written;
        size -= written;
    }
    return true;
}

std::string query_codex_metadata(const fs::path& executable, const fs::path& working_directory) {
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE input_read = nullptr;
    HANDLE input_write = nullptr;
    HANDLE output_read = nullptr;
    HANDLE output_write = nullptr;
    if (!CreatePipe(&input_read, &input_write, &security, 0) ||
        !CreatePipe(&output_read, &output_write, &security, 0)) {
        if (input_read)
            CloseHandle(input_read);
        if (input_write)
            CloseHandle(input_write);
        if (output_read)
            CloseHandle(output_read);
        if (output_write)
            CloseHandle(output_write);
        return {};
    }
    if (!SetHandleInformation(input_write, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(input_read);
        CloseHandle(input_write);
        CloseHandle(output_read);
        CloseHandle(output_write);
        return {};
    }

    HANDLE null_error = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input_read;
    startup.hStdOutput = output_write;
    startup.hStdError = null_error == INVALID_HANDLE_VALUE ? output_write : null_error;

    const std::wstring executable_wide = executable.wstring();
    std::wstring command = L"\"" + executable_wide +
                           L"\" -c cli_auth_credentials_store=keyring app-server --listen stdio://";
    std::vector<wchar_t> command_buffer(command.begin(), command.end());
    command_buffer.push_back(L'\0');
    const std::wstring directory_wide = working_directory.wstring();
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(executable_wide.c_str(), command_buffer.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, directory_wide.c_str(), &startup, &process);
    CloseHandle(input_read);
    CloseHandle(output_write);
    if (null_error != INVALID_HANDLE_VALUE)
        CloseHandle(null_error);
    if (!started) {
        CloseHandle(input_write);
        CloseHandle(output_read);
        return {};
    }

    CodexMetadataConversation conversation;
    bool sent_metadata_requests = false;
    write_pipe(input_write, kCodexInitializeRequest, std::char_traits<char>::length(kCodexInitializeRequest));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline && !conversation.complete()) {
        DWORD available = 0;
        if (!PeekNamedPipe(output_read, nullptr, 0, nullptr, &available, nullptr))
            break;
        if (available > 0) {
            char buffer[8192];
            const DWORD wanted = std::min<DWORD>(available, static_cast<DWORD>(sizeof(buffer)));
            DWORD received = 0;
            if (!ReadFile(output_read, buffer, wanted, &received, nullptr) || received == 0)
                break;
            conversation.accept(buffer, received);
        } else if (WaitForSingleObject(process.hProcess, 0) != WAIT_TIMEOUT) {
            break;
        } else {
            Sleep(10);
        }

        if (conversation.initialized && !sent_metadata_requests) {
            sent_metadata_requests = write_pipe(input_write, kCodexMetadataRequests,
                                                std::char_traits<char>::length(kCodexMetadataRequests));
            if (!sent_metadata_requests)
                break;
        }
    }

    CloseHandle(input_write);
    if (WaitForSingleObject(process.hProcess, 500) == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 0);
        WaitForSingleObject(process.hProcess, 1000);
    }
    CloseHandle(output_read);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return conversation.jsonl;
}
#else
bool write_pipe(int pipe, const char* bytes, size_t size) {
    // A runtime that exits between fork and the first write must not terminate
    // the editor with SIGPIPE. Block it only for this worker thread and consume
    // only the signal generated by this write attempt.
    sigset_t blocked_signals;
    sigset_t previous_signals;
    ::sigemptyset(&blocked_signals);
    ::sigaddset(&blocked_signals, SIGPIPE);
    if (::pthread_sigmask(SIG_BLOCK, &blocked_signals, &previous_signals) != 0)
        return false;
    sigset_t pending_signals;
    ::sigpending(&pending_signals);
    const bool pipe_was_pending = ::sigismember(&pending_signals, SIGPIPE) == 1;

    bool success = true;
    while (size > 0) {
        const ssize_t written = ::write(pipe, bytes, size);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0) {
            success = false;
            break;
        }
        bytes += written;
        size -= static_cast<size_t>(written);
    }
    if (!success && !pipe_was_pending) {
        timespec no_wait{};
        ::sigtimedwait(&blocked_signals, nullptr, &no_wait);
    }
    ::pthread_sigmask(SIG_SETMASK, &previous_signals, nullptr);
    return success;
}

std::string query_codex_metadata(const fs::path& executable, const fs::path& working_directory) {
    int input_pipe[2] = {-1, -1};
    int output_pipe[2] = {-1, -1};
    if (::pipe(input_pipe) != 0 || ::pipe(output_pipe) != 0) {
        if (input_pipe[0] >= 0)
            ::close(input_pipe[0]);
        if (input_pipe[1] >= 0)
            ::close(input_pipe[1]);
        if (output_pipe[0] >= 0)
            ::close(output_pipe[0]);
        if (output_pipe[1] >= 0)
            ::close(output_pipe[1]);
        return {};
    }

    pid_t child = ::fork();
    if (child == 0) {
        ::chdir(working_directory.c_str());
        ::dup2(input_pipe[0], STDIN_FILENO);
        ::dup2(output_pipe[1], STDOUT_FILENO);
        const int null_error = ::open("/dev/null", O_WRONLY);
        if (null_error >= 0) {
            ::dup2(null_error, STDERR_FILENO);
            ::close(null_error);
        }
        ::close(input_pipe[0]);
        ::close(input_pipe[1]);
        ::close(output_pipe[0]);
        ::close(output_pipe[1]);
        ::execl(executable.c_str(), executable.c_str(), "-c", "cli_auth_credentials_store=keyring", "app-server",
                "--listen", "stdio://", static_cast<char*>(nullptr));
        ::_exit(127);
    }

    ::close(input_pipe[0]);
    ::close(output_pipe[1]);
    if (child < 0) {
        ::close(input_pipe[1]);
        ::close(output_pipe[0]);
        return {};
    }

    CodexMetadataConversation conversation;
    bool sent_metadata_requests = false;
    write_pipe(input_pipe[1], kCodexInitializeRequest, std::char_traits<char>::length(kCodexInitializeRequest));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    bool child_reaped = false;
    while (std::chrono::steady_clock::now() < deadline && !conversation.complete()) {
        pollfd descriptor{output_pipe[0], POLLIN, 0};
        const int ready = ::poll(&descriptor, 1, 50);
        if (ready > 0 && (descriptor.revents & (POLLIN | POLLHUP))) {
            char buffer[8192];
            const ssize_t received = ::read(output_pipe[0], buffer, sizeof(buffer));
            if (received > 0)
                conversation.accept(buffer, static_cast<size_t>(received));
            else if (received == 0)
                break;
        } else if (ready < 0 && errno != EINTR) {
            break;
        }

        if (conversation.initialized && !sent_metadata_requests) {
            sent_metadata_requests = write_pipe(input_pipe[1], kCodexMetadataRequests,
                                                std::char_traits<char>::length(kCodexMetadataRequests));
            if (!sent_metadata_requests)
                break;
        }

        int status = 0;
        if (::waitpid(child, &status, WNOHANG) == child) {
            child_reaped = true;
            break;
        }
    }

    ::close(input_pipe[1]);
    ::close(output_pipe[0]);
    if (!child_reaped) {
        for (int attempt = 0; attempt < 50; ++attempt) {
            int status = 0;
            if (::waitpid(child, &status, WNOHANG) == child) {
                child_reaped = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    if (!child_reaped) {
        ::kill(child, SIGTERM);
        ::waitpid(child, nullptr, 0);
    }
    return conversation.jsonl;
}
#endif

std::string shell_quote(const std::string& value) {
#ifdef _WIN32
    std::string result = "\"";
    for (char c : value) {
        if (c == '"')
            result += "\\\"";
        else
            result += c;
    }
    return result + "\"";
#else
    std::string result = "'";
    for (char c : value) {
        if (c == '\'')
            result += "'\\''";
        else
            result += c;
    }
    return result + "'";
#endif
}

fs::path make_run_directory() {
    std::random_device random;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 16; ++attempt) {
        const fs::path candidate =
            fs::temp_directory_path() / ("worldshaper-agent-" + std::to_string(stamp) + "-" + std::to_string(random()) +
                                         "-" + std::to_string(attempt));
#ifdef _WIN32
        std::error_code ec;
        if (fs::create_directory(candidate, ec))
            return candidate;
#else
        // Create atomically as owner-only. This folder briefly contains the
        // reduced scene request and provider answer, but never credentials.
        if (::mkdir(candidate.c_str(), S_IRWXU) == 0)
            return candidate;
        if (errno != EEXIST)
            return {};
#endif
    }
    return {};
}

std::string json_string(const std::string& value) {
    std::string result = "\"";
    for (const unsigned char c : value) {
        switch (c) {
            case '\"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\b':
                result += "\\b";
                break;
            case '\f':
                result += "\\f";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
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

AiRuntimeProvider runtime_provider(AiProvider provider) {
    return provider == AiProvider::Codex ? AiRuntimeProvider::Codex : AiRuntimeProvider::Claude;
}

std::string reset_time_text(int64_t timestamp) {
    if (timestamp <= 0)
        return {};
    const std::time_t value = static_cast<std::time_t>(timestamp);
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &value) != 0)
        return {};
#else
    if (!localtime_r(&value, &local))
        return {};
#endif
    std::ostringstream text;
    text << std::put_time(&local, "%d.%m. %H:%M");
    return text.str();
}

fs::path credential_helper_path() {
    fs::path path = executable_dir() / "worldshaper-credential-helper";
#ifdef _WIN32
    path += ".exe";
#endif
    return path;
}

void readonly_text(const char* id, const std::string& text, float height) {
    ImGui::InputTextMultiline(id, const_cast<char*>(text.c_str()), text.size() + 1, ImVec2(-1.0f, height),
                              ImGuiInputTextFlags_ReadOnly);
}

}  // namespace

AiAssistantPanel::AiAssistantPanel() {
    std::snprintf(prompt_, sizeof(prompt_), "Build a small playable area around the selected entity.");
}

AiAssistantPanel::~AiAssistantPanel() {
    secure_credentials::Erase(anthropic_api_key_, sizeof(anthropic_api_key_));
}

AiAssistantPanel::ProviderResult AiAssistantPanel::run_provider(AiProvider provider, const std::string& model,
                                                                const std::string& prompt) {
    ProviderResult result;
    fs::path executable = FindAiRuntime(runtime_provider(provider));
    if (executable.empty()) {
        result.error = std::string(provider_name(provider)) + " runtime is not installed.";
        return result;
    }
    std::error_code canonical_error;
    const fs::path canonical = fs::canonical(executable, canonical_error);
    if (!canonical_error)
        executable = canonical;

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
            const char* home_value = std::getenv("HOME");
            const fs::path sandbox_home = home_value ? fs::path(home_value).lexically_normal() : fs::path{};
            // Codex includes its home path in the native-keyring identity. Keep
            // that path string, but mount a fresh empty /home so neither the
            // provider nor its tools can see the user's actual home contents.
            if (!runtime_dir || !session_bus || !sandbox_home.is_absolute() || sandbox_home == "/home" ||
                sandbox_home.parent_path() != "/home") {
                result.error = "The encrypted OS credential vault is unavailable in the sandbox.";
                std::error_code ec;
                fs::remove_all(run_dir, ec);
                return result;
            }
            const fs::path sandbox_codex_home = sandbox_home / ".codex";

            command +=
                "/usr/bin/bwrap --die-with-parent --new-session --unshare-pid "
                "--ro-bind /usr /usr --ro-bind /etc /etc --ro-bind /run /run "
                "--proc /proc --dev /dev --tmpfs /home --tmpfs /tmp "
                "--dir " +
                shell_quote(sandbox_home.string()) + " --dir " + shell_quote(sandbox_codex_home.string()) +
                " --dir /runtime ";
            command += "--ro-bind " + shell_quote(executable.parent_path().string()) +
                       " /runtime "
                       "--bind " +
                       shell_quote(run_dir.string()) +
                       " /work --chdir /work "
                       "--clearenv --setenv PATH /usr/bin "
                       "--setenv HOME " +
                       shell_quote(sandbox_home.string()) + " --setenv CODEX_HOME " +
                       shell_quote(sandbox_codex_home.string()) + " "
                       "--setenv XDG_RUNTIME_DIR " +
                       shell_quote(runtime_dir) +
                       " "
                       "--setenv DBUS_SESSION_BUS_ADDRESS " +
                       shell_quote(session_bus) +
                       " "
                       "--setenv LANG C.UTF-8 /runtime/" +
                       shell_quote(executable.filename().string()) +
                       " "
                       "-c cli_auth_credentials_store=keyring "
                       "--ask-for-approval never" +
                       (model.empty() ? std::string{} : " --model " + shell_quote(model)) +
                       " exec "
                       "--ephemeral --skip-git-repo-check --ignore-user-config --ignore-rules "
                       "--sandbox read-only --output-schema /work/response-schema.json "
                       "-C /work -o /work/response.json - < " +
                       shell_quote(prompt_path.string()) + " "
                       "> /dev/null 2>&1";
        } else
#endif
        {
            command += shell_quote(executable.string()) +
                       " -c cli_auth_credentials_store=keyring --ask-for-approval never" +
                       (model.empty() ? std::string{} : " --model " + shell_quote(model)) +
                       " exec --ephemeral --skip-git-repo-check --ignore-user-config "
                       "--ignore-rules --sandbox read-only "
                       "--output-schema " +
                       shell_quote(schema_path.string()) + " -C " + shell_quote(run_dir.string()) + " -o " +
                       shell_quote(response_path.string()) + " - < " + shell_quote(prompt_path.string()) + " > " +
                       shell_quote(
#ifdef _WIN32
                           "NUL"
#else
                           "/dev/null"
#endif
                           ) +
                       " 2>&1";
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
        const std::string helper_command = shell_quote(helper.string()) + " anthropic";
        const std::string settings = "{\"apiKeyHelper\":" + json_string(helper_command) + "}";
        command += shell_quote(executable.string()) + " --bare -p --no-session-persistence --disable-slash-commands " +
                   (model.empty() ? std::string{} : "--model " + shell_quote(model) + " ") +
                   "--no-chrome --strict-mcp-config --mcp-config " + shell_quote("{}") + " --settings " +
                   shell_quote(settings) +
                   " --tools \"\" --permission-mode plan --output-format json "
                   "--json-schema " +
                   shell_quote(schema) + " < " + shell_quote(prompt_path.string()) + " > " +
                   shell_quote(response_path.string()) + " 2> " +
                   shell_quote(
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
        if (provider == AiProvider::Claude) {
            // Claude's JSON wrapper contains exact request usage. We retain
            // only aggregate numbers in memory for this editor session.
            try {
                const json response = json::parse(result.output, nullptr, false);
                if (!response.is_discarded() && response.is_object()) {
                    result.cost_usd = response.value("total_cost_usd", 0.0);
                    const json usage = response.value("usage", json::object());
                    result.input_tokens = usage.value("input_tokens", uint64_t{0}) +
                                          usage.value("cache_creation_input_tokens", uint64_t{0}) +
                                          usage.value("cache_read_input_tokens", uint64_t{0});
                    result.output_tokens = usage.value("output_tokens", uint64_t{0});
                }
            } catch (const json::exception&) {
                // Usage is optional UI metadata; never fail a valid plan over it.
            }
        }
    }

    std::error_code ec;
    fs::remove_all(run_dir, ec);
    return result;
}

AiAssistantPanel::AuthResult AiAssistantPanel::run_auth_command(AiProvider provider, bool login) {
    AuthResult result;
    const fs::path executable = FindAiRuntime(runtime_provider(provider));
    result.cli_available = !executable.empty();

    if (provider == AiProvider::Claude) {
        result.secure_store_available = secure_credentials::Available();
        result.signed_in = result.secure_store_available && secure_credentials::HasAnthropicApiKey();
        if (!result.secure_store_available) {
            result.detail = "The encrypted OS credential vault is unavailable.";
        } else if (!result.cli_available && result.signed_in) {
            result.detail = "API key is protected; install the runtime to use it.";
        } else if (!result.cli_available) {
            result.detail = "Claude runtime is not installed.";
        } else if (result.signed_in) {
            result.detail = "Connected using encrypted OS credential storage.";
        } else {
            result.detail = "No encrypted Anthropic API key is stored.";
        }
        return result;
    }

    if (!result.cli_available) {
        result.detail = "Codex runtime is not installed.";
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
        command += shell_quote(executable.string()) + arguments + " > " + shell_quote(null_device) + " 2>&1";
        return std::system(command.c_str());
    };

    if (login) {
        // The official Codex browser flow receives the account credentials;
        // the editor never sees them. Only the OS keyring may persist them.
        const int exit_code = run(" -c cli_auth_credentials_store=keyring login");
        if (exit_code != 0) {
            result.detail = "The secure browser sign-in did not complete. No credential was saved.";
            std::error_code ec;
            fs::remove_all(run_dir, ec);
            return result;
        }
    }

    const int status_code = run(" -c cli_auth_credentials_store=keyring login status");
    result.signed_in = status_code == 0;
    result.detail = result.signed_in ? "Connected using encrypted OS credential storage."
                                     : "Not connected, or encrypted OS credential storage is unavailable.";

    if (result.signed_in) {
        // app-server requires a real handshake: wait for initialize's reply,
        // acknowledge it, and only then request models and allowance windows.
        // The exchange remains in memory; account metadata is never persisted.
        const std::string metadata = query_codex_metadata(executable, run_dir);
        if (!metadata.empty()) {
            std::istringstream lines(metadata);
            std::string line;
            while (std::getline(lines, line)) {
                try {
                    const json message = json::parse(line, nullptr, false);
                    if (message.is_discarded() || !message.is_object() || !message.contains("id") ||
                        !message.contains("result"))
                        continue;
                    const int id = message.value("id", -1);
                    const json& payload = message["result"];
                    if (id == 1 && payload.contains("data") && payload["data"].is_array()) {
                        for (const json& item : payload["data"]) {
                            if (item.value("hidden", false))
                                continue;
                            ModelOption option;
                            option.id = item.value("model", std::string{});
                            option.label = item.value("displayName", option.id);
                            if (!option.id.empty())
                                result.models.push_back(std::move(option));
                        }
                    } else if (id == 2) {
                        const auto add_snapshot = [&](const json& snapshot, const std::string& fallback_name) {
                            // limitName is optional and is commonly JSON null.
                            // json::value<string>() throws for null and used to
                            // discard the complete, otherwise valid response.
                            std::string name;
                            if (snapshot.contains("limitName") && snapshot["limitName"].is_string())
                                name = snapshot["limitName"].get<std::string>();
                            if (name.empty() && fallback_name == "codex")
                                name = "Codex";
                            else if (name == "codex_other" || (name.empty() && fallback_name == "codex_other"))
                                name = "Other models";
                            if (result.plan.empty() && snapshot.contains("planType") &&
                                snapshot["planType"].is_string())
                                result.plan = snapshot["planType"].get<std::string>();
                            if (result.credit_balance.empty() && snapshot.contains("credits") &&
                                snapshot["credits"].is_object()) {
                                const json& credits = snapshot["credits"];
                                if (credits.value("unlimited", false))
                                    result.credit_balance = "Unlimited";
                                else if (credits.contains("balance") && credits["balance"].is_string())
                                    result.credit_balance = credits["balance"].get<std::string>();
                            }
                            const auto add_window = [&](const char* key, const char* suffix) {
                                if (!snapshot.contains(key) || !snapshot[key].is_object())
                                    return;
                                const json& window = snapshot[key];
                                UsageWindow value;
                                const int64_t minutes = window.value("windowDurationMins", int64_t{0});
                                std::string duration = suffix;
                                if (minutes > 0 && minutes % (24 * 60) == 0)
                                    duration = std::to_string(minutes / (24 * 60)) + "d";
                                else if (minutes > 0 && minutes % 60 == 0)
                                    duration = std::to_string(minutes / 60) + "h";
                                value.label = name.empty() ? duration : name + " (" + duration + ")";
                                value.used_percent = window.value("usedPercent", -1);
                                value.resets_at = window.value("resetsAt", int64_t{0});
                                if (value.used_percent >= 0)
                                    result.usage_windows.push_back(std::move(value));
                            };
                            add_window("primary", "short window");
                            add_window("secondary", "long window");
                        };

                        if (payload.contains("rateLimitsByLimitId") && payload["rateLimitsByLimitId"].is_object()) {
                            for (auto it = payload["rateLimitsByLimitId"].begin();
                                 it != payload["rateLimitsByLimitId"].end(); ++it)
                                add_snapshot(it.value(), it.key());
                        } else if (payload.contains("rateLimits") && payload["rateLimits"].is_object()) {
                            add_snapshot(payload["rateLimits"], "Codex");
                        }
                    }
                } catch (const json::exception&) {
                    // Ignore one malformed metadata line and keep the login valid.
                }
            }
        }
    }

    std::error_code ec;
    fs::remove_all(run_dir, ec);
    return result;
}

void AiAssistantPanel::start_auth_check() {
    if (auth_running_)
        return;
    const AiProvider selected_provider = provider_;
    auth_running_ = true;
    auth_login_running_ = false;
    auth_checked_ = false;
    auth_future_ =
        std::async(std::launch::async, [selected_provider] { return run_auth_command(selected_provider, false); });
}

void AiAssistantPanel::start_login() {
    if (auth_running_ || provider_ != AiProvider::Codex)
        return;
    const AiProvider selected_provider = provider_;
    auth_running_ = true;
    auth_login_running_ = true;
    auth_checked_ = false;
    auth_future_ =
        std::async(std::launch::async, [selected_provider] { return run_auth_command(selected_provider, true); });
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

void AiAssistantPanel::start_runtime_install() {
    if (runtime_installing_ || running_ || auth_running_)
        return;
    const AiRuntimeProvider selected = runtime_provider(provider_);
    runtime_installing_ = true;
    error_.clear();
    status_ = std::string("Downloading the official ") + provider_name(provider_) + " runtime...";
    runtime_future_ = std::async(std::launch::async, [selected] { return InstallAiRuntime(selected); });
}

void AiAssistantPanel::poll_runtime_install() {
    if (!runtime_installing_ || !runtime_future_.valid() ||
        runtime_future_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        return;
    AiRuntimeInstallResult result = runtime_future_.get();
    runtime_installing_ = false;
    auth_ = {};
    auth_checked_ = false;
    if (result.ok) {
        status_ = std::string(provider_name(provider_)) + " runtime installed. Account check started.";
        error_.clear();
        start_auth_check();
    } else {
        status_.clear();
        error_ = std::move(result.error);
    }
}

void AiAssistantPanel::start_request(const std::shared_ptr<schizo::scene::Scene>& scene, uint32_t selected_entity_id) {
    pending_plan_.reset();
    error_.clear();
    outgoing_scene_summary_ = BuildEngineAgentSceneSnapshot(scene, selected_entity_id);
    planned_scene_fingerprint_ = BuildEngineAgentSceneSnapshot(scene, 0);
    const std::string full_prompt = BuildEngineAgentPrompt(prompt_, outgoing_scene_summary_);
    const AiProvider selected_provider = provider_;
    const std::string selected_model = provider_ == AiProvider::Codex ? codex_model_ : claude_model_;
    running_ = true;
    status_ = std::string(provider_name(provider_)) + " is preparing a proposal...";
    future_ = std::async(std::launch::async, [selected_provider, selected_model, full_prompt] {
        return run_provider(selected_provider, selected_model, full_prompt);
    });
}

void AiAssistantPanel::poll_request() {
    if (!running_ || !future_.valid() || future_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        return;

    running_ = false;
    ProviderResult result = future_.get();
    if (!result.ok) {
        status_.clear();
        error_ = std::move(result.error);
        return;
    }

    session_input_tokens_ += result.input_tokens;
    session_output_tokens_ += result.output_tokens;
    session_cost_usd_ += result.cost_usd;

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

void AiAssistantPanel::Render(const EngineAgentApplyContext& context, uint32_t selected_entity_id, bool* open) {
    poll_request();
    poll_auth();
    poll_runtime_install();
    if (!ImGui::Begin("AI Assistant", open)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("AI Scene Assistant");
    ImGui::TextDisabled("Describe the result. Review the safe plan. Apply it yourself.");
    ImGui::SeparatorText("1. Account");

    ImGui::BeginDisabled(running_ || auth_running_ || runtime_installing_);
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

    if (!auth_checked_ && !auth_running_ && !runtime_installing_)
        start_auth_check();

    const float account_height = provider_ == AiProvider::Codex ? 260.0f : 250.0f;
    ImGui::BeginChild("##ai_account", ImVec2(0.0f, account_height), true);
    if (runtime_installing_) {
        ImGui::TextColored(ImVec4(0.35f, 0.75f, 0.95f, 1.0f), "Downloading and installing official runtime...");
    } else if (auth_running_) {
        ImGui::TextColored(ImVec4(0.35f, 0.75f, 0.95f, 1.0f), "%s",
                           auth_login_running_ ? "Complete the sign-in in your browser..." : "Checking account...");
    } else if (auth_.signed_in) {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.48f, 1.0f), "Connected");
    } else if (auth_.cli_available && !auth_.secure_store_available) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Encrypted credential vault unavailable");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f),
                           auth_.cli_available ? "Not connected" : "Runtime not installed");
    }

    if (!runtime_installing_ && auth_checked_ && !auth_.cli_available) {
        if (ImGui::Button(provider_ == AiProvider::Codex ? "Install Codex Runtime" : "Install Claude Runtime"))
            start_runtime_install();
        ImGui::SameLine();
        ImGui::TextDisabled("Official per-user installation");
    }

    if (provider_ == AiProvider::Codex) {
        if (!auth_.signed_in) {
            ImGui::BeginDisabled(auth_running_ || runtime_installing_ || !auth_.cli_available ||
                                 !auth_.secure_store_available);
            if (ImGui::Button("Sign in with ChatGPT"))
                start_login();
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        ImGui::BeginDisabled(auth_running_ || runtime_installing_);
        if (ImGui::SmallButton("Refresh"))
            start_auth_check();
        ImGui::EndDisabled();
        ImGui::TextDisabled(auth_.signed_in ? "ChatGPT plan  -  encrypted OS keyring"
                                           : "Uses your ChatGPT plan; credentials stay in the encrypted OS keyring.");
    } else {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##anthropic_api_key", "Anthropic Console API key", anthropic_api_key_,
                                 sizeof(anthropic_api_key_), ImGuiInputTextFlags_Password);

        ImGui::BeginDisabled(auth_running_ || runtime_installing_ || !auth_.secure_store_available ||
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
            secure_credentials::Erase(anthropic_api_key_, sizeof(anthropic_api_key_));
            start_auth_check();
        }
        ImGui::EndDisabled();

        if (auth_.signed_in) {
            ImGui::SameLine();
            ImGui::BeginDisabled(auth_running_ || runtime_installing_);
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
        ImGui::BeginDisabled(auth_running_ || runtime_installing_);
        if (ImGui::SmallButton("Refresh"))
            start_auth_check();
        ImGui::EndDisabled();
        ImGui::TextDisabled("Saved only in Windows Credential Manager, macOS Keychain, or Linux Secret Service.");
    }
    if (auth_checked_ && !auth_.signed_in && !auth_.detail.empty())
        ImGui::TextDisabled("%s", auth_.detail.c_str());

    ImGui::Spacing();
    ImGui::TextUnformatted("Model");
    if (provider_ == AiProvider::Codex) {
        const char* preview = "Default (account recommended)";
        for (const ModelOption& option : auth_.models)
            if (option.id == codex_model_)
                preview = option.label.c_str();
        ImGui::BeginDisabled(!auth_.cli_available || runtime_installing_);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##codex_model", preview)) {
            if (ImGui::Selectable("Default (account recommended)", codex_model_.empty()))
                codex_model_.clear();
            for (const ModelOption& option : auth_.models) {
                const bool selected = codex_model_ == option.id;
                if (ImGui::Selectable(option.label.c_str(), selected))
                    codex_model_ = option.id;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();

        if (!auth_.usage_windows.empty()) {
            if (auth_.plan.empty())
                ImGui::TextDisabled("Usage");
            else
                ImGui::TextDisabled("Usage - %s plan", auth_.plan.c_str());
            if (!auth_.credit_balance.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("- Credits: %s", auth_.credit_balance.c_str());
            }
            for (const UsageWindow& window : auth_.usage_windows) {
                const int used = std::clamp(window.used_percent, 0, 100);
                const int remaining = 100 - used;
                std::string overlay = std::to_string(used) + "% used  -  " + std::to_string(remaining) + "% left";
                const std::string reset = reset_time_text(window.resets_at);
                if (!reset.empty())
                    overlay += " - resets " + reset;
                ImGui::TextDisabled("%s", window.label.c_str());
                ImGui::ProgressBar(static_cast<float>(remaining) / 100.0f, ImVec2(-1.0f, 0.0f), overlay.c_str());
            }
        } else if (auth_.signed_in && !auth_running_) {
            ImGui::TextDisabled("Usage is currently unavailable; Refresh retries it.");
        }
    } else {
        const char* labels[] = {"Default (Anthropic recommended)", "Sonnet (balanced)", "Opus (strongest)",
                                "Haiku (fastest)"};
        const char* ids[] = {"", "sonnet", "opus", "haiku"};
        int selected = 0;
        for (int i = 0; i < 4; ++i)
            if (claude_model_ == ids[i])
                selected = i;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##claude_model", &selected, labels, 4))
            claude_model_ = ids[selected];
        ImGui::TextDisabled("This editor session: %llu input / %llu output tokens - $%.4f",
                            static_cast<unsigned long long>(session_input_tokens_),
                            static_cast<unsigned long long>(session_output_tokens_), session_cost_usd_);
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
    ImGui::InputTextMultiline("##ai_request", prompt_, sizeof(prompt_), ImVec2(-1.0f, 110.0f));

    const bool can_request = context.scene && context.undo && !context.project_root.empty() && prompt_[0] != '\0' &&
                             auth_checked_ && auth_.cli_available && auth_.signed_in && !running_ &&
                             !runtime_installing_;
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
        ImGui::TextDisabled("%zu proposed action%s", pending_plan_->actions.size(),
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

        const bool scene_changed = BuildEngineAgentSceneSnapshot(context.scene, 0) != planned_scene_fingerprint_;
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

    if (!outgoing_scene_summary_.empty() && ImGui::CollapsingHeader("Data sent to the provider")) {
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
