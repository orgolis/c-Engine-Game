#include "terminal_panel.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <deque>
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

    ~Impl() { stop(); }

    bool start() {
        int to_shell[2] = {-1, -1};
        int from_shell[2] = {-1, -1};
        if (pipe(to_shell) != 0 || pipe(from_shell) != 0) {
            spdlog::error("[terminal] Linux pipe creation failed: {}", std::strerror(errno));
            return false;
        }

        shell_pid = fork();
        if (shell_pid == 0) {
            setpgid(0, 0);
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
        std::string line = command + "\n";
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
                lines.push_back(std::move(partial));
                partial.clear();
                while (lines.size() > kMaxLines) lines.pop_front();
            } else if (ch == '\r') {
                partial.clear();
            } else if (static_cast<unsigned char>(ch) >= 0x20 || ch == '\t') {
                partial.push_back(ch);
            }
        }
    }

    void clear() { lines.clear(); partial.clear(); }
};

TerminalPanel::TerminalPanel() : impl_(std::make_unique<Impl>()) { impl_->start(); }
TerminalPanel::~TerminalPanel() = default;

void TerminalPanel::Render(bool* open) {
    if (!open || !*open) return;
    Impl& terminal = *impl_;
    terminal.drain();

    ImGui::Begin("Terminal", open);
    ImGui::TextUnformatted("/bin/bash");
    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
        terminal.stop();
        terminal.clear();
        terminal.start();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) terminal.clear();
    ImGui::SameLine();
    if (ImGui::Button("Ctrl+C") && terminal.shell_pid > 0)
        kill(-terminal.shell_pid, SIGINT);
    if (terminal.child_exited.load()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "[shell exited - Restart]");
    }
    ImGui::Separator();

    const float footer = ImGui::GetFrameHeightWithSpacing();
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

    ImGui::Separator();
    ImGui::TextUnformatted(">");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (terminal.focus_input) { ImGui::SetKeyboardFocusHere(); terminal.focus_input = false; }
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue |
                                      ImGuiInputTextFlags_CallbackHistory;
    if (ImGui::InputText("##linux_terminal_command", terminal.input,
                         sizeof(terminal.input), flags, InputHistoryCallback, &terminal)) {
        const std::string command = terminal.input;
        terminal.write_command(command);
        if (!command.empty() && (terminal.history.empty() || terminal.history.back() != command))
            terminal.history.push_back(command);
        terminal.history_pos = -1;
        terminal.input[0] = '\0';
        terminal.focus_input = true;
    }
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
