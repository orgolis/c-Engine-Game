// ============================================================================
// crash_check — verifies the gws_diagnostics crash handler + reporting.
// ============================================================================
//
// Parent mode (no args): tests the in-process pieces (ring-buffer log capture,
// non-terminating write_report), then spawns itself once per crash type
// (segv / abort / throw) and confirms each dying child left a crash report +
// minidump on disk.
//
// Child mode (`--crash <type> <report_dir>`): installs the handler and triggers
// the crash — the handler writes the report and terminates the process.

#include "diagnostics/crash_handler.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace fs = std::filesystem;

// ---- child: install + crash ------------------------------------------------
static int child_crash(const std::string& type, const std::string& dir) {
    gws::diag::CrashConfig cc;
    cc.app_name    = "crash_check";
    cc.version     = "test";
    cc.report_dir  = dir;
    cc.show_dialog = false;   // headless — never pop a MessageBox
    cc.context_provider = [] { return std::string("test-context-marker\n"); };
    gws::diag::init_logging(dir, "crash_check");
    gws::diag::install_crash_handler(cc);
    spdlog::info("crash_check child: about to crash via '{}'", type);

    if (type == "segv")  { volatile int* p = nullptr; *p = 42; }
    else if (type == "abort") { std::abort(); }
    else if (type == "throw") { throw std::runtime_error("boom from crash_check"); }
    return 0;  // unreachable if the crash + handler worked
}

// ---- child: hammer the log file -------------------------------------------
// Two of these run at once. Before one-process-per-file they shared a handle
// and tore each other's lines -- four such tears are visible in the shipped
// editor.log, one of them mid-timestamp, and past the rotation threshold the
// damage stops being cosmetic and starts deleting lines outright.
static int child_logspam(const std::string& dir, const std::string& tag) {
    gws::diag::init_logging(dir, "crash_check_log");
    for (int i = 0; i < 3000; ++i)
        spdlog::info("[{}] line {:04d} padding-padding-padding-padding", tag, i);
    if (auto l = spdlog::default_logger()) l->flush();
    return 0;
}

#if defined(_WIN32)
static void spawn_wait(const std::string& exe, const std::string& type, const std::string& dir) {
    std::string cmd = "\"" + exe + "\" --crash " + type + " \"" + dir + "\"";
    std::vector<char> buf(cmd.begin(), cmd.end()); buf.push_back('\0');
    STARTUPINFOA si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 20000);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
}
// Launch without waiting: the point is that both are inside init_logging and
// writing at the same time. spawn_wait would serialise them and prove nothing.
static HANDLE spawn_async(const std::string& exe, const std::string& args) {
    std::string cmd = "\"" + exe + "\" " + args;
    std::vector<char> buf(cmd.begin(), cmd.end()); buf.push_back('\0');
    STARTUPINFOA si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return nullptr;
    CloseHandle(pi.hThread);
    return pi.hProcess;
}
static std::string self_path() {
    char b[MAX_PATH]; DWORD n = GetModuleFileNameA(nullptr, b, MAX_PATH);
    return std::string(b, n);
}
#endif

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    if (argc >= 4 && std::string(argv[1]) == "--crash")
        return child_crash(argv[2], argv[3]);
    if (argc >= 4 && std::string(argv[1]) == "--logspam")
        return child_logspam(argv[2], argv[3]);

    std::printf("crash_check: crash handler + reporting\n");
    std::error_code ec;
    fs::path base = fs::temp_directory_path(ec) / "gws_crash_check";
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);

    int fails = 0;
    auto check = [&](const std::string& what, bool ok) {
        std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", what.c_str());
        if (!ok) ++fails;
    };

    // ---- 1) in-process: ring buffer + non-terminating write_report ----------
    {
        fs::path d = base / "manual";
        fs::create_directories(d, ec);
        gws::diag::init_logging(d.string(), "crash_check");
        spdlog::info("marker-line-alpha");
        spdlog::warn("marker-line-bravo");

        auto lines = gws::diag::recent_log_lines(50);
        bool has = false;
        for (const auto& l : lines) if (l.find("marker-line-bravo") != std::string::npos) has = true;
        check("ring buffer captures recent log lines", has);

        gws::diag::CrashConfig cc;
        cc.app_name = "crash_check"; cc.report_dir = d.string(); cc.show_dialog = false;
        gws::diag::install_crash_handler(cc);

        std::string rp = gws::diag::write_report("manual self-test");
        check("write_report() creates a report file", !rp.empty() && fs::exists(rp, ec));
        if (!rp.empty() && fs::exists(rp, ec)) {
            std::ifstream f(rp);
            std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            check("report embeds the recent log", all.find("marker-line-bravo") != std::string::npos);
            check("report embeds system info",     all.find("--- system ---") != std::string::npos);
        }
    }

    // ---- 2) child processes: each crash type leaves a report (+ minidump) ----
#if defined(_WIN32)
    const std::string self = self_path();
    for (const char* type : {"segv", "abort", "throw"}) {
        fs::path d = base / type;
        fs::create_directories(d, ec);
        std::printf("  spawning child (%s)...\n", type);
        spawn_wait(self, type, d.string());

        bool report = false, dump = false;
        for (auto& e : fs::directory_iterator(d, ec)) {
            const auto name = e.path().filename().string();
            if (name.rfind("crash_report_", 0) == 0 && e.path().extension() == ".txt") report = true;
            if (e.path().extension() == ".dmp") dump = true;
        }
        check(std::string("child '") + type + "' wrote a crash report", report);
        check(std::string("child '") + type + "' wrote a minidump",     dump);
    }
#else
    std::printf("  (child-process crash tests are Windows-only — skipped)\n");
#endif

    // ---- 3) two processes must not share one log file -----------------------
#if defined(_WIN32)
    {
        fs::path d = base / "lograce";
        fs::create_directories(d, ec);
        std::printf("  spawning two concurrent loggers...\n");
        HANDLE ha = spawn_async(self, "--logspam \"" + d.string() + "\" A");
        HANDLE hb = spawn_async(self, "--logspam \"" + d.string() + "\" B");
        if (ha) { WaitForSingleObject(ha, 30000); CloseHandle(ha); }
        if (hb) { WaitForSingleObject(hb, 30000); CloseHandle(hb); }

        std::vector<fs::path> logs;
        for (auto& e : fs::directory_iterator(d, ec))
            if (e.path().extension() == ".log") logs.push_back(e.path());
        check("two concurrent processes get two log files", logs.size() == 2);

        // The property that actually matters. A torn write is a line with a
        // timestamp somewhere other than the start -- one process's bytes
        // landing in the middle of another's line.
        int torn = 0, mixed = 0;
        for (const auto& lp : logs) {
            std::ifstream f(lp);
            std::string line; bool sawA = false, sawB = false;
            while (std::getline(f, line)) {
                if (line.find("[20", 1) != std::string::npos) ++torn;
                if (line.find("[A]") != std::string::npos) sawA = true;
                if (line.find("[B]") != std::string::npos) sawB = true;
            }
            if (sawA && sawB) ++mixed;
        }
        check("no torn writes across either log", torn == 0);
        check("neither log carries both processes' lines", mixed == 0);
    }
#endif

    fs::remove_all(base, ec);
    std::printf("crash_check: %s\n", fails == 0 ? "ALL OK" : "FAILED");
    return fails == 0 ? 0 : 1;
}
