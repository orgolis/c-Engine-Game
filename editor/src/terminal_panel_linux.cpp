#include "terminal_panel.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace schizo::editor {

namespace {
constexpr size_t kMaxLines = 5000;
int InputHistoryCallback(ImGuiInputTextCallbackData* data);
}

struct TerminalPanel::Impl {
    pid_t shell_pid = -1;
    int input_fd = -1;
    int output_fd = -1;
    std::thread reader;
    std::atomic<bool> running{false};
    std::atomic<bool> child_exited{false};
    std::mutex inbox_mutex;
    std::string inbox;
    std::deque<std::string> lines;
    std::string partial;
    char input[1024] = {};
    std::vector<std::string> history;
    int history_pos = -1;
    bool focus_input = true;
    std::string working_directory;

    ~Impl() { stop(); }

    bool start() {
        std::error_code path_error;
        working_directory = std::filesystem::current_path(path_error).string();
        if (path_error) working_directory = ".";
        int to_shell[2] = {-1, -1};
        int from_shell[2] = {-1, -1};
        if (pipe(to_shell) != 0 || pipe(from_shell) != 0) {
            spdlog::error("[terminal] Linux pipe creation failed: {}", std::strerror(errno));
            return false;
        }

        shell_pid = fork();
        if (shell_pid == 0) {
            setpgid(0, 0);
            if (chdir(working_directory.c_str()) != 0) _exit(126);
            dup2(to_shell[0], STDIN_FILENO);
            dup2(from_shell[1], STDOUT_FILENO);
            dup2(from_shell[1], STDERR_FILENO);
            close(to_shell[0]); close(to_shell[1]);
            close(from_shell[0]); close(from_shell[1]);
            setenv("TERM", "dumb", 1);
            execl("/bin/bash", "bash", "--noprofile", "--norc", static_cast<char*>(nullptr));
            _exit(127);
        }
        close(to_shell[0]);
        close(from_shell[1]);
        if (shell_pid < 0) {
            close(to_shell[1]);
            close(from_shell[0]);
            spdlog::error("[terminal] Linux fork failed: {}", std::strerror(errno));
            return false;
        }

        input_fd = to_shell[1];
        output_fd = from_shell[0];
        running = true;
        child_exited = false;
        reader = std::thread([this] {
            char buffer[8192];
            while (running.load()) {
                const ssize_t count = read(output_fd, buffer, sizeof(buffer));
                if (count <= 0) break;
                std::lock_guard<std::mutex> lock(inbox_mutex);
                inbox.append(buffer, static_cast<size_t>(count));
                constexpr size_t kMaxInbox = 1u << 20;
                if (inbox.size() > kMaxInbox) inbox.erase(0, inbox.size() - kMaxInbox);
            }
            child_exited = true;
        });
        spdlog::info("[terminal] started /bin/bash (Linux)");
        return true;
    }

    void stop() {
        running = false;
        if (input_fd >= 0) { close(input_fd); input_fd = -1; }
        if (shell_pid > 0) {
            kill(-shell_pid, SIGTERM);
            waitpid(shell_pid, nullptr, 0);
            shell_pid = -1;
        }
        if (reader.joinable()) reader.join();
        if (output_fd >= 0) { close(output_fd); output_fd = -1; }
    }

    void write_command(const std::string& command) {
        if (input_fd < 0) return;
        // The marker reports bash's real directory after every command. It is
        // consumed by drain() instead of rendered, which keeps `cd` reflected
        // in the next prompt without trying to interpret shell syntax here.
        std::string line = command +
            "\nprintf '__GWS_TERM_PWD__=%s\\n' \"$PWD\"\n";
        size_t sent = 0;
        while (sent < line.size()) {
            const ssize_t count = write(input_fd, line.data() + sent, line.size() - sent);
            if (count <= 0) break;
            sent += static_cast<size_t>(count);
        }
    }

    void drain() {
        std::string data;
        {
            std::lock_guard<std::mutex> lock(inbox_mutex);
            data.swap(inbox);
        }
        for (char ch : data) {
            if (ch == '\n') {
                if (!partial.empty() && partial.back() == '\r') partial.pop_back();
                constexpr const char* marker = "__GWS_TERM_PWD__=";
                if (partial.rfind(marker, 0) == 0) {
                    working_directory = partial.substr(std::strlen(marker));
                } else {
                    lines.push_back(std::move(partial));
                    while (lines.size() > kMaxLines) lines.pop_front();
                }
                partial.clear();
            } else if (ch == '\r') {
                partial.clear();
            } else if (static_cast<unsigned char>(ch) >= 0x20 || ch == '\t') {
                partial.push_back(ch);
            }
        }
    }

    void clear() { lines.clear(); partial.clear(); }

    std::string prompt() const {
        const std::filesystem::path path(working_directory);
        const std::string folder = path.filename().string();
        return (folder.empty() ? working_directory : folder) + " $";
    }
};

TerminalPanel::TerminalPanel() : impl_(std::make_unique<Impl>()) { impl_->start(); }
TerminalPanel::~TerminalPanel() = default;

void TerminalPanel::Render(bool* open) {
    if (!open || !*open) return;
    Impl& terminal = *impl_;
    terminal.drain();

    ImGui::Begin("Terminal", open);
    // Compact header like VS Code: shell identity on the left, controls on the
    // right. The full current path remains visible without occupying a row of
    // large buttons.
    ImGui::TextUnformatted("bash");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", terminal.working_directory.c_str());
    const float controls_width = 168.0f;
    if (ImGui::GetContentRegionAvail().x > controls_width) {
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - controls_width);
    } else {
        ImGui::SameLine();
    }
    if (ImGui::SmallButton("Restart")) {
        terminal.stop();
        terminal.clear();
        terminal.start();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) terminal.clear();
    ImGui::SameLine();
    if (ImGui::SmallButton("Ctrl+C") && terminal.shell_pid > 0)
        kill(-terminal.shell_pid, SIGINT);
    if (terminal.child_exited.load()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "[shell exited - Restart]");
    }
    ImGui::Separator();

    const float footer = ImGui::GetFrameHeightWithSpacing() + 5.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.060f, 0.070f, 1.0f));
    ImGui::BeginChild("##linux_terminal_scroll", ImVec2(0, -footer), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(terminal.lines.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            ImGui::TextUnformatted(terminal.lines[static_cast<size_t>(i)].c_str());
    }
    clipper.End();
    if (!terminal.partial.empty()) ImGui::TextUnformatted(terminal.partial.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // Prompt and input share one dark row, instead of looking like a form
    // field below the terminal. This is the interaction model used by VS Code.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.060f, 0.070f, 1.0f));
    ImGui::BeginChild("##linux_terminal_prompt", ImVec2(0, ImGui::GetFrameHeight()), false);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.82f, 0.58f, 1.0f));
    ImGui::TextUnformatted(terminal.prompt().c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.055f, 0.060f, 0.070f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 0.0f));
    if (terminal.focus_input) { ImGui::SetKeyboardFocusHere(); terminal.focus_input = false; }
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue |
                                      ImGuiInputTextFlags_CallbackHistory;
    if (ImGui::InputText("##linux_terminal_command", terminal.input,
                         sizeof(terminal.input), flags, InputHistoryCallback, &terminal)) {
        const std::string command = terminal.input;
        terminal.lines.push_back(terminal.prompt() + " " + command);
        while (terminal.lines.size() > kMaxLines) terminal.lines.pop_front();
        terminal.write_command(command);
        if (!command.empty() && (terminal.history.empty() || terminal.history.back() != command))
            terminal.history.push_back(command);
        terminal.history_pos = -1;
        terminal.input[0] = '\0';
        terminal.focus_input = true;
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::End();
}

namespace {
int InputHistoryCallback(ImGuiInputTextCallbackData* data) {
    auto* terminal = static_cast<TerminalPanel::Impl*>(data->UserData);
    if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory || terminal->history.empty()) return 0;
    if (data->EventKey == ImGuiKey_UpArrow) {
        if (terminal->history_pos < 0)
            terminal->history_pos = static_cast<int>(terminal->history.size()) - 1;
        else if (terminal->history_pos > 0)
            --terminal->history_pos;
    } else if (data->EventKey == ImGuiKey_DownArrow) {
        if (terminal->history_pos >= 0) ++terminal->history_pos;
        if (terminal->history_pos >= static_cast<int>(terminal->history.size()))
            terminal->history_pos = -1;
    }
    const char* replacement = terminal->history_pos >= 0
        ? terminal->history[static_cast<size_t>(terminal->history_pos)].c_str() : "";
    data->DeleteChars(0, data->BufTextLen);
    data->InsertChars(0, replacement);
    return 0;
}
}

} // namespace schizo::editor
