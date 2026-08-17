#pragma once

// ============================================================================
// Crash + diagnostics system  (gws_diagnostics)
// ============================================================================
//
// Goal: NO engine crash is silent. When the process dies from a hardware fault
// (access violation — the usual Vulkan/null-deref crash), an uncaught C++
// exception, or an abort/signal, we capture what happened and write a
// self-contained crash report the developer can act on:
//
//   * the reason (exception code / signal / exception text) + faulting address
//   * a call stack (symbolized where symbols exist; module+RVA otherwise, so a
//     stripped release build can still be symbolized offline against the
//     matching unstripped editor.exe via addr2line)
//   * a Windows minidump (.dmp) for post-mortem debugging
//   * the last N log lines (from the in-memory ring buffer — what the engine
//     was doing right before it died)
//   * system info + caller-supplied app context (project / scene / GPU)
//
// Install ONCE, as early in main() as possible:
//
//   gws::diag::init_logging(log_dir, "editor");             // file + ring sinks
//   gws::diag::CrashConfig cfg;
//   cfg.app_name = "editor"; cfg.version = ENGINE_VERSION;
//   cfg.report_dir = crash_dir;
//   cfg.context_provider = []{ return "project: ...\nscene: ..."; };
//   gws::diag::install_crash_handler(cfg);
//
// Windows-only bodies (SEH / DbgHelp / MiniDump); on other platforms the calls
// compile to safe no-ops (signal + terminate handlers still installed).

#include <functional>
#include <string>
#include <vector>

namespace gws::diag {

struct CrashConfig {
    std::string app_name   = "app";
    std::string version;                 // e.g. engine version string
    std::string build_info;              // e.g. "Debug MinGW" (optional)
    std::string report_dir;              // dir for crash_report_*.txt / *.dmp (created if missing)
    bool        write_minidump = true;   // also emit a .dmp next to the report
    bool        show_dialog    = true;   // pop a MessageBox pointing at the report (Windows)
    // Called at crash time to append app-specific context to the report. MUST be
    // crash-safe: read-only, no locking, no heavy allocation. May be empty.
    std::function<std::string()> context_provider;
};

// Install the SEH filter, std::set_terminate handler, and signal handlers.
// Idempotent — a second call replaces the stored config.
void install_crash_handler(const CrashConfig& cfg);

// Write a crash-style report WITHOUT terminating (for a caught std::exception or
// a recoverable fatal condition). Returns the report path, or "" on failure.
std::string write_report(const std::string& reason);

// ---- hang watchdog ----
// A freeze is invisible to everything above: nothing throws, nothing signals,
// and a per-frame timer never fires because the frame never ends. So the only
// way to see one is from OUTSIDE the stuck thread.
//
// install_hang_watchdog() starts a thread that watches a heartbeat the main
// loop stamps once per frame. If the heartbeat goes stale for `seconds`, it
// suspends the main thread just long enough to grab its CONTEXT, then writes a
// hang_report_*.txt with the stack it was stuck in -- three samples a couple of
// seconds apart, so a spin (same stack every time) is distinguishable from slow
// progress (stack moves). Reports once per stall, then re-arms when the
// heartbeat resumes.
//
// Call hang_heartbeat() once per frame from the main thread. Cheap: one
// relaxed atomic store.
void install_hang_watchdog(double seconds = 5.0);
void hang_heartbeat();

// ---- logging plumbing (spdlog default logger) ----
// Attach a rotating file sink (<log_dir>/<app_name>.log) and an in-memory
// ring-buffer sink to spdlog's default logger. Safe to call once, early.
void init_logging(const std::string& log_dir, const std::string& app_name,
                  size_t ring_capacity = 400);

// The last <=count formatted log lines captured by the ring-buffer sink.
std::vector<std::string> recent_log_lines(size_t count = 400);

}  // namespace gws::diag
