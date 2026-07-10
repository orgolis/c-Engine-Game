// Embedded OS shell terminal (Windows ConPTY) for the editor.
//
// Spawns a real shell attached to a pseudo-console, reads its output on a
// background thread, parses the VT/ANSI stream into a colour-styled scrollback,
// and feeds typed commands back to it. Line-oriented model (handles colours,
// carriage-return overwrite, erase-line/clear-screen) — not a full TUI grid.

// ConPTY decls in <wincon.h> are gated on `NTDDI_VERSION >= 0x0A000006`
// (Windows 10 1809 / RS5), not just _WIN32_WINNT. The project sets no global
// version, so pin BOTH here before <windows.h> — _WIN32_WINNT alone defaults
// NTDDI_VERSION to the base Win10 value (0x0A000000), which gates ConPTY out.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000006   // NTDDI_WIN10_RS5
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "terminal_panel.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace schizo::editor {

namespace {

// ---- Colour palette (ANSI -> ImGui) ---------------------------------------
constexpr ImU32 kDefaultColor = IM_COL32(220, 220, 220, 255);

const ImU32 kPalette[8] = {
    IM_COL32( 60,  60,  60, 255), // 0 black
    IM_COL32(205,  70,  60, 255), // 1 red
    IM_COL32( 80, 190,  90, 255), // 2 green
    IM_COL32(200, 185,  70, 255), // 3 yellow
    IM_COL32( 80, 130, 220, 255), // 4 blue
    IM_COL32(190,  90, 195, 255), // 5 magenta
    IM_COL32( 70, 190, 200, 255), // 6 cyan
    IM_COL32(200, 200, 200, 255), // 7 white
};
const ImU32 kBright[8] = {
    IM_COL32(130, 130, 130, 255), // 8  bright black (gray)
    IM_COL32(240, 110, 100, 255), // 9  bright red
    IM_COL32(120, 230, 130, 255), // 10 bright green
    IM_COL32(240, 225, 110, 255), // 11 bright yellow
    IM_COL32(120, 170, 250, 255), // 12 bright blue
    IM_COL32(230, 130, 235, 255), // 13 bright magenta
    IM_COL32(110, 230, 240, 255), // 14 bright cyan
    IM_COL32(245, 245, 245, 255), // 15 bright white
};

ImU32 xterm256(int n) {
    if (n < 0) n = 0;
    if (n < 8)   return kPalette[n];
    if (n < 16)  return kBright[n - 8];
    if (n < 232) {
        n -= 16;
        static const int lv[6] = {0, 95, 135, 175, 215, 255};
        int r = lv[(n / 36) % 6], g = lv[(n / 6) % 6], b = lv[n % 6];
        return IM_COL32(r, g, b, 255);
    }
    int v = 8 + (n - 232) * 10;
    if (v > 255) v = 255;
    return IM_COL32(v, v, v, 255);
}

struct Cell { char ch; ImU32 color; };
struct Span { std::string text; ImU32 color; };
using Line = std::vector<Span>;

constexpr size_t kMaxLines = 5000;

// Coalesce a run of cells into colour spans for rendering.
Line coalesce(const std::vector<Cell>& cells) {
    Line line;
    for (const Cell& c : cells) {
        if (line.empty() || line.back().color != c.color)
            line.push_back(Span{std::string(1, c.ch), c.color});
        else
            line.back().text.push_back(c.ch);
    }
    return line;
}

} // namespace

// ---------------------------------------------------------------------------

struct TerminalPanel::Impl {
    // ConPTY + child process
    HPCON  hpc          = nullptr;
    HANDLE input_write  = nullptr;   // editor -> shell stdin
    HANDLE output_read  = nullptr;   // shell stdout -> editor
    PROCESS_INFORMATION pi{};
    STARTUPINFOEXW      si{};
    std::vector<char>   attr_buf;

    std::thread        reader;
    std::atomic<bool>  running{false};
    std::atomic<bool>  child_exited{false};

    // raw bytes from the reader thread, drained on the main thread
    std::mutex  inbox_mtx;
    std::string inbox;

    // scrollback (main thread only)
    std::deque<Line>  lines;
    std::vector<Cell> cur;          // line currently being built
    size_t            cursor = 0;   // column within `cur`
    ImU32             cur_color = kDefaultColor;

    // VT parser state
    enum class P { Normal, Esc, Csi, Osc } pstate = P::Normal;
    std::string seq;                // CSI/OSC accumulator

    // UI
    char  input[1024] = {};
    std::vector<std::string> history;
    int   history_pos = -1;         // -1 = editing fresh line
    bool  focus_input = true;
    int   shell_choice = 0;         // 0 = PowerShell, 1 = cmd
    COORD pty_size{0, 0};

    ~Impl() { stop(); }

    std::wstring shell_cmdline() const {
        return shell_choice == 1 ? L"cmd.exe"
                                 : L"powershell.exe -NoLogo";
    }
    const char* shell_name() const {
        return shell_choice == 1 ? "cmd.exe" : "PowerShell";
    }

    bool start(SHORT cols, SHORT rows);
    void stop();
    void reader_loop();
    void write_bytes(const char* data, size_t n);

    // VT parsing
    void drain();
    void parse(const char* data, size_t n);
    void put(char c);
    void commit_line();
    void handle_csi(char final);
    void apply_sgr();
    void clear_scrollback();
};

bool TerminalPanel::Impl::start(SHORT cols, SHORT rows) {
    if (cols < 1) cols = 80;
    if (rows < 1) rows = 25;

    HANDLE in_read = nullptr, in_write = nullptr;
    HANDLE out_read = nullptr, out_write = nullptr;
    if (!CreatePipe(&in_read, &in_write, nullptr, 0) ||
        !CreatePipe(&out_read, &out_write, nullptr, 0)) {
        spdlog::error("[terminal] CreatePipe failed");
        return false;
    }

    COORD size{cols, rows};
    HRESULT hr = CreatePseudoConsole(size, in_read, out_write, 0, &hpc);
    // ConPTY duplicates the ends it keeps; release our copies of those.
    CloseHandle(in_read);
    CloseHandle(out_write);
    if (FAILED(hr)) {
        spdlog::error("[terminal] CreatePseudoConsole failed: 0x{:08x}",
                      static_cast<unsigned>(hr));
        CloseHandle(in_write);
        CloseHandle(out_read);
        hpc = nullptr;
        return false;
    }
    input_write = in_write;
    output_read = out_read;
    pty_size = size;

    // STARTUPINFOEX carrying the pseudo-console attribute.
    si = STARTUPINFOEXW{};
    si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    si.lpAttributeList = nullptr;   // set only after a successful Initialize, so
                                    // stop()'s DeleteProcThreadAttributeList can
                                    // never run on an uninitialized list.
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    attr_buf.assign(bytes, 0);
    auto* attr = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buf.data());
    if (!InitializeProcThreadAttributeList(attr, 1, 0, &bytes)) {
        spdlog::error("[terminal] InitializeProcThreadAttributeList failed");
        stop();
        return false;
    }
    si.lpAttributeList = attr;
    if (!UpdateProcThreadAttribute(si.lpAttributeList, 0,
                                   PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   hpc, sizeof(hpc), nullptr, nullptr)) {
        spdlog::error("[terminal] UpdateProcThreadAttribute failed");
        stop();
        return false;
    }

    std::wstring cmd = shell_cmdline();
    std::vector<wchar_t> cmdbuf(cmd.begin(), cmd.end());
    cmdbuf.push_back(0);

    BOOL ok = CreateProcessW(nullptr, cmdbuf.data(), nullptr, nullptr, FALSE,
                             EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                             &si.StartupInfo, &pi);
    if (!ok) {
        spdlog::error("[terminal] CreateProcessW('{}') failed: {}",
                      shell_name(), static_cast<unsigned>(GetLastError()));
        stop();
        return false;
    }

    running = true;
    child_exited = false;
    reader = std::thread([this] { reader_loop(); });
    spdlog::info("[terminal] started {} ({}x{})", shell_name(), cols, rows);
    return true;
}

void TerminalPanel::Impl::stop() {
    running = false;
    // 1. Kill the shell and WAIT for it to die — TerminateProcess is
    //    asynchronous, and conhost keeps the output pipe open while the
    //    client process lives.
    if (pi.hProcess && WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 0);
        WaitForSingleObject(pi.hProcess, 2000);
    }
    // 2. Close the pseudo-console WHILE the reader thread is still draining
    //    the pipe (ClosePseudoConsole only hangs when the pipe is full with no
    //    reader). conhost shuts down and closes its write end, which EOFs the
    //    reader's blocking ReadFile.
    //    NOTE: the previous order — CloseHandle(output_read) then join —
    //    DEADLOCKED at app exit: closing a pipe handle does NOT wake a thread
    //    already blocked in ReadFile on that handle, so join never returned.
    if (hpc) { ClosePseudoConsole(hpc); hpc = nullptr; }
    // 3. Reader sees EOF (or an error) and exits; now the join is safe.
    if (reader.joinable()) reader.join();
    if (output_read) { CloseHandle(output_read); output_read = nullptr; }
    if (input_write) { CloseHandle(input_write); input_write = nullptr; }
    if (si.lpAttributeList) {
        DeleteProcThreadAttributeList(si.lpAttributeList);
        si.lpAttributeList = nullptr;
    }
    attr_buf.clear();
    if (pi.hThread)  { CloseHandle(pi.hThread);  pi.hThread = nullptr; }
    if (pi.hProcess) { CloseHandle(pi.hProcess); pi.hProcess = nullptr; }
}

void TerminalPanel::Impl::reader_loop() {
    char buf[8192];
    DWORD n = 0;
    while (running.load()) {
        if (!ReadFile(output_read, buf, sizeof(buf), &n, nullptr) || n == 0)
            break;
        std::lock_guard<std::mutex> lk(inbox_mtx);
        inbox.append(buf, n);
        // Bound the inbox so a chatty child can't grow memory without bound
        // while the panel is hidden (drain() only runs from Render()).
        constexpr size_t kMaxInbox = 1u << 20;   // 1 MiB
        if (inbox.size() > kMaxInbox)
            inbox.erase(0, inbox.size() - kMaxInbox);
    }
    child_exited = true;
}

void TerminalPanel::Impl::write_bytes(const char* data, size_t n) {
    if (!input_write || n == 0) return;
    DWORD written = 0;
    WriteFile(input_write, data, static_cast<DWORD>(n), &written, nullptr);
}

// ---- VT parsing -----------------------------------------------------------

void TerminalPanel::Impl::put(char c) {
    if (cursor < cur.size()) cur[cursor] = Cell{c, cur_color};
    else                     cur.push_back(Cell{c, cur_color});
    ++cursor;
}

void TerminalPanel::Impl::commit_line() {
    lines.push_back(coalesce(cur));
    while (lines.size() > kMaxLines) lines.pop_front();
    cur.clear();
    cursor = 0;
}

void TerminalPanel::Impl::clear_scrollback() {
    lines.clear();
    cur.clear();
    cursor = 0;
}

void TerminalPanel::Impl::apply_sgr() {
    // Parse the ';'-separated numeric params in `seq`.
    std::vector<int> codes;
    int val = 0; bool has = false;
    for (char c : seq) {
        if (c >= '0' && c <= '9') { val = val * 10 + (c - '0'); has = true; }
        else if (c == ';') { codes.push_back(has ? val : 0); val = 0; has = false; }
    }
    codes.push_back(has ? val : 0);
    if (codes.empty()) codes.push_back(0);

    for (size_t i = 0; i < codes.size(); ++i) {
        int code = codes[i];
        if (code == 0 || code == 39) {
            cur_color = kDefaultColor;
        } else if (code >= 30 && code <= 37) {
            cur_color = kPalette[code - 30];
        } else if (code >= 90 && code <= 97) {
            cur_color = kBright[code - 90];
        } else if (code == 38) {
            if (i + 2 < codes.size() && codes[i + 1] == 5) {
                cur_color = xterm256(codes[i + 2]); i += 2;
            } else if (i + 4 < codes.size() && codes[i + 1] == 2) {
                cur_color = IM_COL32(codes[i + 2], codes[i + 3], codes[i + 4], 255);
                i += 4;
            }
        }
        // background colours (40-49/100-107) and attributes are ignored.
    }
}

void TerminalPanel::Impl::handle_csi(char final) {
    auto first_int = [&](int def) {
        int v = 0; bool has = false;
        for (char c : seq) {
            if (c >= '0' && c <= '9') { v = v * 10 + (c - '0'); has = true; }
            else break;
        }
        return has ? v : def;
    };
    switch (final) {
        case 'm': apply_sgr(); break;
        case 'K': {                       // erase in line
            int p = first_int(0);
            if (p == 0)      { if (cursor < cur.size()) cur.resize(cursor); }
            else if (p == 2) { cur.clear(); cursor = 0; }
            break;
        }
        case 'J': {                       // erase in display
            int p = first_int(0);
            if (p == 2 || p == 3) clear_scrollback();
            break;
        }
        default: break;                   // cursor moves etc. — ignored
    }
}

void TerminalPanel::Impl::parse(const char* data, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = static_cast<unsigned char>(data[i]);
        switch (pstate) {
            case P::Normal:
                if (c == 0x1b) { pstate = P::Esc; }
                else if (c == '\n') { commit_line(); }
                else if (c == '\r') { cursor = 0; }
                else if (c == '\b') { if (cursor > 0) --cursor; }
                else if (c == '\t') { do { put(' '); } while (cursor % 8 != 0); }
                else if (c >= 0x20) { put(static_cast<char>(c)); }
                // other control bytes (BEL etc.) ignored
                break;
            case P::Esc:
                if (c == '[') { pstate = P::Csi; seq.clear(); }
                else if (c == ']') { pstate = P::Osc; seq.clear(); }
                else { pstate = P::Normal; }   // ignore other 2-byte escapes
                break;
            case P::Csi:
                if (c >= 0x40 && c <= 0x7e) { handle_csi(static_cast<char>(c)); pstate = P::Normal; }
                else { seq.push_back(static_cast<char>(c)); }
                break;
            case P::Osc:
                if (c == 0x07) { pstate = P::Normal; }      // BEL terminates
                else if (c == 0x1b) { pstate = P::Esc; }    // ST (ESC \) — drop
                // content (window title etc.) ignored
                break;
        }
    }
}

void TerminalPanel::Impl::drain() {
    std::string local;
    {
        std::lock_guard<std::mutex> lk(inbox_mtx);
        local.swap(inbox);
    }
    if (!local.empty()) parse(local.data(), local.size());
}

// ---------------------------------------------------------------------------

namespace {
int InputHistoryCallback(ImGuiInputTextCallbackData* data);
}

TerminalPanel::TerminalPanel() : impl_(std::make_unique<Impl>()) {
    impl_->start(120, 30);   // corrected to the panel size on first Render()
}

TerminalPanel::~TerminalPanel() = default;

void TerminalPanel::Render(bool* open) {
    Impl& t = *impl_;
    t.drain();

    ImGui::Begin("Terminal", open);   // docked window = child; always End() below
    {
        // ---- toolbar ----
        ImGui::SetNextItemWidth(120.0f);
        const char* shells[] = { "PowerShell", "cmd" };
        ImGui::Combo("##shell", &t.shell_choice, shells, 2);
        ImGui::SameLine();
        if (ImGui::Button("Restart")) {
            t.stop();
            t.clear_scrollback();
            t.start(t.pty_size.X, t.pty_size.Y);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear")) t.clear_scrollback();
        ImGui::SameLine();
        if (ImGui::Button("Ctrl+C")) { char c = 0x03; t.write_bytes(&c, 1); }
        if (t.child_exited.load()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f),
                               "[shell exited — Restart]");
        }
        ImGui::Separator();

        // ---- scrollback ----
        const float footer = ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("##scroll", ImVec2(0, -footer), false,
                          ImGuiWindowFlags_HorizontalScrollbar);
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 1));

            auto draw_line = [](const Line& line) {
                if (line.empty()) { ImGui::TextUnformatted(""); return; }
                bool first = true;
                for (const Span& s : line) {
                    if (!first) ImGui::SameLine(0.0f, 0.0f);
                    first = false;
                    ImGui::PushStyleColor(ImGuiCol_Text, s.color);
                    ImGui::TextUnformatted(s.text.c_str(),
                                           s.text.c_str() + s.text.size());
                    ImGui::PopStyleColor();
                }
            };

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(t.lines.size()));
            while (clipper.Step())
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                    draw_line(t.lines[static_cast<size_t>(i)]);
            clipper.End();

            // the in-progress line (prompt / partial output)
            if (!t.cur.empty()) draw_line(coalesce(t.cur));

            // resize the pseudo-console to match the visible area (in cells)
            const float cw = ImGui::CalcTextSize("M").x;
            const float lh = ImGui::GetTextLineHeightWithSpacing();
            ImVec2 avail = ImGui::GetWindowSize();
            if (cw > 0.0f && lh > 0.0f) {
                SHORT cols = static_cast<SHORT>(std::max(1.0f, avail.x / cw));
                SHORT rows = static_cast<SHORT>(std::max(1.0f, avail.y / lh));
                if ((cols != t.pty_size.X || rows != t.pty_size.Y) && t.hpc) {
                    t.pty_size = COORD{cols, rows};
                    ResizePseudoConsole(t.hpc, t.pty_size);
                }
            }

            // Follow the tail: pin to the bottom whenever the user is already
            // at (or near) it, so streaming output (builds, git, long-running
            // commands) stays visible without needing an Enter press.
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
                ImGui::SetScrollHereY(1.0f);

            ImGui::PopStyleVar();
        }
        ImGui::EndChild();

        // ---- input line ----
        ImGui::Separator();
        ImGui::TextUnformatted(">");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGuiInputTextFlags flags =
            ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_CallbackHistory;
        if (t.focus_input) { ImGui::SetKeyboardFocusHere(); t.focus_input = false; }
        if (ImGui::InputText("##cmd", t.input, sizeof(t.input), flags,
                             &InputHistoryCallback, &t)) {
            std::string cmd = t.input;
            t.write_bytes(cmd.data(), cmd.size());
            char cr = '\r';
            t.write_bytes(&cr, 1);
            if (!cmd.empty() &&
                (t.history.empty() || t.history.back() != cmd))
                t.history.push_back(cmd);
            t.history_pos = -1;
            t.input[0] = '\0';
            t.focus_input = true;     // keep typing
        }
    }
    ImGui::End();
}

namespace {
int InputHistoryCallback(ImGuiInputTextCallbackData* data) {
    auto* t = static_cast<TerminalPanel::Impl*>(data->UserData);
    if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory) return 0;
    if (t->history.empty()) return 0;

    const int prev = t->history_pos;
    if (data->EventKey == ImGuiKey_UpArrow) {
        if (t->history_pos < 0) t->history_pos = static_cast<int>(t->history.size()) - 1;
        else if (t->history_pos > 0) --t->history_pos;
    } else if (data->EventKey == ImGuiKey_DownArrow) {
        if (t->history_pos >= 0) ++t->history_pos;
        if (t->history_pos >= static_cast<int>(t->history.size())) t->history_pos = -1;
    }
    if (prev != t->history_pos) {
        const char* repl = (t->history_pos >= 0)
                               ? t->history[static_cast<size_t>(t->history_pos)].c_str()
                               : "";
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, repl);
    }
    return 0;
}
} // namespace

} // namespace schizo::editor
