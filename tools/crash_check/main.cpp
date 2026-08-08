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
static std::string self_path() {
    char b[MAX_PATH]; DWORD n = GetModuleFileNameA(nullptr, b, MAX_PATH);
    return std::string(b, n);
}
#endif

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    if (argc >= 4 && std::string(argv[1]) == "--crash")
        return child_crash(argv[2], argv[3]);

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

    fs::remove_all(base, ec);
    std::printf("crash_check: %s\n", fails == 0 ? "ALL OK" : "FAILED");
    return fails == 0 ? 0 : 1;
}
