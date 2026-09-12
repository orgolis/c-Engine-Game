// Embedded Linux terminal for the editor.
//
// The shell is connected to a real pseudo-terminal (PTY). Keyboard input goes
// straight to that PTY and the shell echoes it back into the same terminal
// surface. This is intentionally not an ImGui command field: prompt, input and
// output all belong to the user's normal interactive shell.

#include "terminal_panel.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <pty.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace schizo::editor {

namespace {

constexpr ImU32 kDefaultColor = IM_COL32(220, 220, 220, 255);
constexpr size_t kMaxLines = 5000;

const ImU32 kPalette[8] = {
    IM_COL32( 60,  60,  60, 255), IM_COL32(205,  70,  60, 255),
    IM_COL32( 80, 190,  90, 255), IM_COL32(200, 185,  70, 255),
    IM_COL32( 80, 130, 220, 255), IM_COL32(190,  90, 195, 255),
    IM_COL32( 70, 190, 200, 255), IM_COL32(200, 200, 200, 255),
};

const ImU32 kBright[8] = {
    IM_COL32(130, 130, 130, 255), IM_COL32(240, 110, 100, 255),
    IM_COL32(120, 230, 130, 255), IM_COL32(240, 225, 110, 255),
    IM_COL32(120, 170, 250, 255), IM_COL32(230, 130, 235, 255),
    IM_COL32(110, 230, 240, 255), IM_COL32(245, 245, 245, 255),
};

ImU32 xterm256(int value) {
    value = std::max(0, value);
    if (value < 8) return kPalette[value];
    if (value < 16) return kBright[value - 8];
    if (value < 232) {
        value -= 16;
        constexpr int levels[6] = {0, 95, 135, 175, 215, 255};
        return IM_COL32(levels[(value / 36) % 6], levels[(value / 6) % 6],
                        levels[value % 6], 255);
    }
    const int gray = std::min(255, 8 + (value - 232) * 10);
    return IM_COL32(gray, gray, gray, 255);
}

struct Cell { char ch; ImU32 color; };
struct Span { std::string text; ImU32 color; };
using Line = std::vector<Span>;

Line coalesce(const std::vector<Cell>& cells) {
    Line line;
    for (const Cell& cell : cells) {
        if (line.empty() || line.back().color != cell.color)
            line.push_back(Span{std::string(1, cell.ch), cell.color});
        else
            line.back().text.push_back(cell.ch);
    }
    return line;
}

int encode_utf8(unsigned int codepoint, char output[4]) {
    if (codepoint <= 0x7f) {
        output[0] = static_cast<char>(codepoint);
        return 1;
    }
    if (codepoint <= 0x7ff) {
        output[0] = static_cast<char>(0xc0 | (codepoint >> 6));
        output[1] = static_cast<char>(0x80 | (codepoint & 0x3f));
        return 2;
    }
    if (codepoint <= 0xffff) {
        output[0] = static_cast<char>(0xe0 | (codepoint >> 12));
        output[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
        output[2] = static_cast<char>(0x80 | (codepoint & 0x3f));
        return 3;
    }
    output[0] = static_cast<char>(0xf0 | (codepoint >> 18));
    output[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f));
    output[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
    output[3] = static_cast<char>(0x80 | (codepoint & 0x3f));
    return 4;
}

} // namespace

struct TerminalPanel::Impl {
    pid_t shell_pid = -1;
    int pty_fd = -1;
    std::thread reader;
    std::atomic<bool> running{false};
    std::atomic<bool> child_exited{false};

    std::mutex inbox_mutex;
    std::string inbox;

    std::deque<Line> lines;
    std::vector<Cell> current_line;
    size_t cursor = 0;
    ImU32 current_color = kDefaultColor;

    enum class ParserState { Normal, Escape, Csi, Osc } parser_state = ParserState::Normal;
    std::string sequence;

    std::string shell_path;
    std::string shell_name;
    std::string working_directory;
    unsigned short pty_columns = 0;
    unsigned short pty_rows = 0;
    bool input_focused = false;

    ~Impl() { stop(); }

    bool start(unsigned short columns = 120, unsigned short rows = 30);
    void stop();
    void reader_loop();
    void write_bytes(const char* data, size_t size);
    void resize(unsigned short columns, unsigned short rows);
    void forward_keyboard_input(bool focused);

    void drain();
    void parse(const char* data, size_t size);
    void put(char character);
    void commit_line();
    void handle_csi(char final_character);
    void apply_sgr();
    void clear();
    void move_cursor_left(int count);
    void move_cursor_right(int count);
};

bool TerminalPanel::Impl::start(unsigned short columns, unsigned short rows) {
    std::error_code path_error;
    working_directory = std::filesystem::current_path(path_error).string();
    if (path_error) working_directory = ".";

    const char* configured_shell = std::getenv("SHELL");
    shell_path = configured_shell && configured_shell[0] != '\0'
        ? configured_shell : "/bin/bash";
    if (access(shell_path.c_str(), X_OK) != 0) shell_path = "/bin/bash";
    shell_name = std::filesystem::path(shell_path).filename().string();
    if (shell_name.empty()) shell_name = shell_path;

    columns = std::max<unsigned short>(columns, 1);
    rows = std::max<unsigned short>(rows, 1);
    winsize initial_size{};
    initial_size.ws_col = columns;
    initial_size.ws_row = rows;

    shell_pid = forkpty(&pty_fd, nullptr, nullptr, &initial_size);
    if (shell_pid == 0) {
        if (chdir(working_directory.c_str()) != 0) _exit(126);
        setenv("TERM", "xterm-256color", 1);
        execl(shell_path.c_str(), shell_name.c_str(), "-i", static_cast<char*>(nullptr));
        _exit(127);
    }
    if (shell_pid < 0) {
        pty_fd = -1;
        spdlog::error("[terminal] forkpty failed: {}", std::strerror(errno));
        return false;
    }

    pty_columns = columns;
    pty_rows = rows;
    running = true;
    child_exited = false;
    reader = std::thread([this] { reader_loop(); });
    spdlog::info("[terminal] started interactive {} in a Linux PTY at {}",
                 shell_path, working_directory);
    return true;
}

void TerminalPanel::Impl::stop() {
    running = false;

    const int old_pty = pty_fd;
    pty_fd = -1;
    if (old_pty >= 0) close(old_pty);

    if (shell_pid > 0) {
        // forkpty() creates a session for the shell. Signal its process group
        // and the shell itself so background jobs cannot keep the PTY alive.
        kill(-shell_pid, SIGHUP);
        kill(shell_pid, SIGTERM);
        waitpid(shell_pid, nullptr, 0);
        shell_pid = -1;
    }
    if (reader.joinable()) reader.join();
}

void TerminalPanel::Impl::reader_loop() {
    char buffer[8192];
    while (running.load()) {
        const ssize_t count = read(pty_fd, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;

        std::lock_guard<std::mutex> lock(inbox_mutex);
        inbox.append(buffer, static_cast<size_t>(count));
        constexpr size_t kMaxInbox = 1u << 20;
        if (inbox.size() > kMaxInbox) inbox.erase(0, inbox.size() - kMaxInbox);
    }
    child_exited = true;
}

void TerminalPanel::Impl::write_bytes(const char* data, size_t size) {
    if (pty_fd < 0 || size == 0) return;
    size_t written = 0;
    while (written < size) {
        const ssize_t count = write(pty_fd, data + written, size - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        written += static_cast<size_t>(count);
    }
}

void TerminalPanel::Impl::resize(unsigned short columns, unsigned short rows) {
    columns = std::max<unsigned short>(columns, 1);
    rows = std::max<unsigned short>(rows, 1);
    if (pty_fd < 0 || (columns == pty_columns && rows == pty_rows)) return;

    winsize size{};
    size.ws_col = columns;
    size.ws_row = rows;
    if (ioctl(pty_fd, TIOCSWINSZ, &size) == 0) {
        pty_columns = columns;
        pty_rows = rows;
    }
}

void TerminalPanel::Impl::forward_keyboard_input(bool focused) {
    if (!focused || pty_fd < 0) return;

    ImGuiIO& io = ImGui::GetIO();
    const bool paste = ImGui::IsKeyChordPressed(
        static_cast<ImGuiKeyChord>(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_V));
    if (paste) {
        if (const char* clipboard = ImGui::GetClipboardText())
            write_bytes(clipboard, std::strlen(clipboard));
    }

    // ImGui provides already composed Unicode characters here. Sending them
    // immediately lets the PTY's line editor echo them at the real prompt.
    if (!io.KeyCtrl && !io.KeyAlt && !io.KeySuper) {
        for (const ImWchar character : io.InputQueueCharacters) {
            if (character < 0x20 || character == 0x7f) continue;
            char utf8[4];
            const int count = encode_utf8(static_cast<unsigned int>(character), utf8);
            write_bytes(utf8, static_cast<size_t>(count));
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Enter))     { const char c = '\r'; write_bytes(&c, 1); }
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) { const char c = 0x7f; write_bytes(&c, 1); }
    if (ImGui::IsKeyPressed(ImGuiKey_Tab))       { const char c = '\t'; write_bytes(&c, 1); }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))    { const char c = 0x1b; write_bytes(&c, 1); }

    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))    write_bytes("\x1b[A", 3);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))  write_bytes("\x1b[B", 3);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) write_bytes("\x1b[C", 3);
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))  write_bytes("\x1b[D", 3);
    if (ImGui::IsKeyPressed(ImGuiKey_Home))       write_bytes("\x1b[H", 3);
    if (ImGui::IsKeyPressed(ImGuiKey_End))        write_bytes("\x1b[F", 3);
    if (ImGui::IsKeyPressed(ImGuiKey_Delete))     write_bytes("\x1b[3~", 4);

    // Ctrl+A ... Ctrl+Z are terminal control bytes. This enables the shell's
    // normal editing shortcuts, Ctrl+C interrupts, Ctrl+D exits, Ctrl+L clears.
    if (io.KeyCtrl && !io.KeyAlt && !io.KeySuper && !paste) {
        for (int index = 0; index < 26; ++index) {
            const ImGuiKey key = static_cast<ImGuiKey>(ImGuiKey_A + index);
            if (ImGui::IsKeyPressed(key)) {
                const char control = static_cast<char>(index + 1);
                write_bytes(&control, 1);
            }
        }
    }
}

void TerminalPanel::Impl::put(char character) {
    if (cursor < current_line.size())
        current_line[cursor] = Cell{character, current_color};
    else
        current_line.push_back(Cell{character, current_color});
    ++cursor;
}

void TerminalPanel::Impl::commit_line() {
    lines.push_back(coalesce(current_line));
    while (lines.size() > kMaxLines) lines.pop_front();
    current_line.clear();
    cursor = 0;
}

void TerminalPanel::Impl::clear() {
    lines.clear();
    current_line.clear();
    cursor = 0;
}

void TerminalPanel::Impl::move_cursor_left(int count) {
    while (count-- > 0 && cursor > 0) {
        --cursor;
        while (cursor > 0 &&
               (static_cast<unsigned char>(current_line[cursor].ch) & 0xc0) == 0x80)
            --cursor;
    }
}

void TerminalPanel::Impl::move_cursor_right(int count) {
    while (count-- > 0 && cursor < current_line.size()) {
        ++cursor;
        while (cursor < current_line.size() &&
               (static_cast<unsigned char>(current_line[cursor].ch) & 0xc0) == 0x80)
            ++cursor;
    }
}

void TerminalPanel::Impl::apply_sgr() {
    std::vector<int> codes;
    int value = 0;
    bool has_value = false;
    for (const char character : sequence) {
        if (character >= '0' && character <= '9') {
            value = value * 10 + (character - '0');
            has_value = true;
        } else if (character == ';') {
            codes.push_back(has_value ? value : 0);
            value = 0;
            has_value = false;
        }
    }
    codes.push_back(has_value ? value : 0);

    for (size_t index = 0; index < codes.size(); ++index) {
        const int code = codes[index];
        if (code == 0 || code == 39) {
            current_color = kDefaultColor;
        } else if (code >= 30 && code <= 37) {
            current_color = kPalette[code - 30];
        } else if (code >= 90 && code <= 97) {
            current_color = kBright[code - 90];
        } else if (code == 38 && index + 2 < codes.size() && codes[index + 1] == 5) {
            current_color = xterm256(codes[index + 2]);
            index += 2;
        } else if (code == 38 && index + 4 < codes.size() && codes[index + 1] == 2) {
            current_color = IM_COL32(codes[index + 2], codes[index + 3],
                                      codes[index + 4], 255);
            index += 4;
        }
    }
}

void TerminalPanel::Impl::handle_csi(char final_character) {
    auto first_integer = [&](int fallback) {
        int value = 0;
        bool found = false;
        for (const char character : sequence) {
            if (character < '0' || character > '9') break;
            value = value * 10 + (character - '0');
            found = true;
        }
        return found ? value : fallback;
    };

    switch (final_character) {
        case 'm': apply_sgr(); break;
        case 'C': move_cursor_right(first_integer(1)); break;
        case 'D': move_cursor_left(first_integer(1)); break;
        case 'G': {
            cursor = 0;
            move_cursor_right(std::max(1, first_integer(1)) - 1);
            break;
        }
        case 'K': {
            const int mode = first_integer(0);
            if (mode == 0 && cursor < current_line.size()) current_line.resize(cursor);
            if (mode == 2) { current_line.clear(); cursor = 0; }
            break;
        }
        case 'J': {
            const int mode = first_integer(0);
            if (mode == 2 || mode == 3) clear();
            break;
        }
        default: break;
    }
}

void TerminalPanel::Impl::parse(const char* data, size_t size) {
    for (size_t index = 0; index < size; ++index) {
        const unsigned char character = static_cast<unsigned char>(data[index]);
        switch (parser_state) {
            case ParserState::Normal:
                if (character == 0x1b) parser_state = ParserState::Escape;
                else if (character == '\n') commit_line();
                else if (character == '\r') cursor = 0;
                else if (character == '\b') move_cursor_left(1);
                else if (character == '\t') {
                    do { put(' '); } while (cursor % 8 != 0);
                } else if (character >= 0x20) {
                    put(static_cast<char>(character));
                }
                break;
            case ParserState::Escape:
                if (character == '[') {
                    parser_state = ParserState::Csi;
                    sequence.clear();
                } else if (character == ']') {
                    parser_state = ParserState::Osc;
                    sequence.clear();
                } else {
                    parser_state = ParserState::Normal;
                }
                break;
            case ParserState::Csi:
                if (character >= 0x40 && character <= 0x7e) {
                    handle_csi(static_cast<char>(character));
                    parser_state = ParserState::Normal;
                } else {
                    sequence.push_back(static_cast<char>(character));
                }
                break;
            case ParserState::Osc:
                if (character == 0x07) parser_state = ParserState::Normal;
                else if (character == 0x1b) parser_state = ParserState::Escape;
                break;
        }
    }
}

void TerminalPanel::Impl::drain() {
    std::string data;
    {
        std::lock_guard<std::mutex> lock(inbox_mutex);
        data.swap(inbox);
    }
    if (!data.empty()) parse(data.data(), data.size());
}

TerminalPanel::TerminalPanel() : impl_(std::make_unique<Impl>()) {
    impl_->start();
}

TerminalPanel::~TerminalPanel() = default;

bool TerminalPanel::HasInputFocus() const {
    return impl_ && impl_->input_focused;
}

void TerminalPanel::Render(bool* open) {
    if (!open || !*open) {
        impl_->input_focused = false;
        return;
    }
    Impl& terminal = *impl_;
    terminal.drain();

    ImGui::Begin("Terminal", open);

    ImGui::TextUnformatted(terminal.shell_name.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Restart")) {
        const unsigned short columns = terminal.pty_columns;
        const unsigned short rows = terminal.pty_rows;
        terminal.stop();
        terminal.clear();
        terminal.start(columns, rows);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) terminal.clear();
    ImGui::SameLine();
    if (ImGui::SmallButton("Ctrl+C")) {
        const char interrupt = 0x03;
        terminal.write_bytes(&interrupt, 1);
    }
    if (terminal.child_exited.load()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f),
                           "[shell exited - Restart]");
    }
    ImGui::Separator();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.060f, 0.070f, 1.0f));
    ImGui::BeginChild("##linux_terminal", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    const bool was_at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 1));

    auto draw_line = [](const Line& line) {
        if (line.empty()) {
            ImGui::TextUnformatted("");
            return;
        }
        bool first = true;
        for (const Span& span : line) {
            if (!first) ImGui::SameLine(0.0f, 0.0f);
            first = false;
            ImGui::PushStyleColor(ImGuiCol_Text, span.color);
            ImGui::TextUnformatted(span.text.c_str(), span.text.c_str() + span.text.size());
            ImGui::PopStyleColor();
        }
    };

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(terminal.lines.size()));
    while (clipper.Step()) {
        for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
            draw_line(terminal.lines[static_cast<size_t>(index)]);
    }
    clipper.End();

    const ImVec2 current_line_position = ImGui::GetCursorScreenPos();
    draw_line(coalesce(terminal.current_line));

    const bool terminal_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    terminal.input_focused = terminal_focused;
    if (terminal_focused && !terminal.child_exited.load() &&
        std::fmod(ImGui::GetTime(), 1.0) < 0.62) {
        std::string before_cursor;
        const size_t end = std::min(terminal.cursor, terminal.current_line.size());
        before_cursor.reserve(end);
        for (size_t index = 0; index < end; ++index)
            before_cursor.push_back(terminal.current_line[index].ch);
        const float cursor_x = current_line_position.x + ImGui::CalcTextSize(before_cursor.c_str()).x;
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(cursor_x, current_line_position.y),
            ImVec2(cursor_x + 2.0f, current_line_position.y + ImGui::GetTextLineHeight()),
            IM_COL32(225, 225, 225, 255));
    }

    const float character_width = ImGui::CalcTextSize("M").x;
    const float line_height = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 viewport = ImGui::GetWindowSize();
    if (character_width > 0.0f && line_height > 0.0f) {
        terminal.resize(
            static_cast<unsigned short>(std::max(1.0f, viewport.x / character_width)),
            static_cast<unsigned short>(std::max(1.0f, viewport.y / line_height)));
    }

    if (was_at_bottom) ImGui::SetScrollHereY(1.0f);
    if (ImGui::IsWindowHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);

    terminal.forward_keyboard_input(terminal_focused);

    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::End();
}

} // namespace schizo::editor
