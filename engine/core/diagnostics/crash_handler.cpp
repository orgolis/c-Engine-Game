#include "crash_handler.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/ringbuffer_sink.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <dbghelp.h>
#endif

namespace gws::diag {

namespace {

CrashConfig                                    g_config;
std::atomic<bool>                              g_installed{false};
std::atomic<bool>                              g_in_handler{false};
std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> g_ring;

// spdlog is built with SPDLOG_WCHAR_FILENAMES here, so filename_t is std::wstring
// on Windows — convert our UTF-8 std::string path to the expected type.
spdlog::filename_t to_filename(const std::string& s) {
#if defined(_WIN32) && defined(SPDLOG_WCHAR_FILENAMES)
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
#else
    return s;
#endif
}

// A monotonic, allocation-light timestamp for filenames + report headers.
std::string timestamp(bool for_filename) {
#if defined(_WIN32)
    SYSTEMTIME t;
    GetLocalTime(&t);
    char buf[32];
    if (for_filename)
        std::snprintf(buf, sizeof buf, "%04d%02d%02d_%02d%02d%02d",
                      t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    else
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d",
                      t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return buf;
#else
    std::time_t tt = std::time(nullptr);
    std::tm tm{};
#  if defined(_MSC_VER)
    localtime_s(&tm, &tt);
#  else
    localtime_r(&tt, &tm);
#  endif
    char buf[32];
    std::strftime(buf, sizeof buf, for_filename ? "%Y%m%d_%H%M%S" : "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
#endif
}

void append_recent_log(std::ostringstream& out) {
    out << "\n--- recent log (newest last) ---\n";
    const auto lines = recent_log_lines();
    if (lines.empty()) { out << "(no ring-buffer sink installed)\n"; return; }
    for (const auto& l : lines) {
        out << l;
        if (l.empty() || l.back() != '\n') out << '\n';
    }
}

void append_system_info(std::ostringstream& out) {
    out << "\n--- system ---\n";
#if defined(_WIN32)
    // OS version straight from the kernel (GetVersionEx lies without a manifest).
    typedef LONG (WINAPI * RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW vi{};
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (HMODULE nt = GetModuleHandleW(L"ntdll.dll")) {
        if (auto p = reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(nt, "RtlGetVersion")))
            p(&vi);
    }
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);
    char buf[256];
    std::snprintf(buf, sizeof buf, "os:  Windows %lu.%lu build %lu\n",
                  vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
    out << buf;
    std::snprintf(buf, sizeof buf, "cpu: %lu logical processors\n", si.dwNumberOfProcessors);
    out << buf;
    std::snprintf(buf, sizeof buf, "ram: %.1f GB total, %.1f GB available\n",
                  mem.ullTotalPhys / (1024.0 * 1024 * 1024),
                  mem.ullAvailPhys / (1024.0 * 1024 * 1024));
    out << buf;
#else
    out << "os:  (non-Windows)\n";
#endif
}

#if defined(_WIN32)
// --- Windows stack walk + symbolization via DbgHelp -------------------------

std::string exception_code_string(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:      return "ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return "FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_ILLEGAL_INSTRUCTION:   return "ILLEGAL_INSTRUCTION";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "INT_DIVIDE_BY_ZERO";
        case EXCEPTION_PRIV_INSTRUCTION:      return "PRIV_INSTRUCTION";
        case EXCEPTION_STACK_OVERFLOW:        return "STACK_OVERFLOW";
        case EXCEPTION_IN_PAGE_ERROR:         return "IN_PAGE_ERROR";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
        default: {
            char b[32]; std::snprintf(b, sizeof b, "0x%08lX", code); return b;
        }
    }
}

// Append a symbolized call stack. `ctx_in` may be a crash context (from SEH) or
// a freshly captured one. Frames print module+RVA always (works on stripped
// builds → symbolize offline with addr2line) plus symbol/file:line when DbgHelp
// can resolve them.
void append_stack(std::ostringstream& out, CONTEXT ctx) {
    const HANDLE proc = GetCurrentProcess();
    const HANDLE thread = GetCurrentThread();

    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(proc, nullptr, TRUE);

    STACKFRAME64 frame{};
    frame.AddrPC.Offset    = ctx.Rip; frame.AddrPC.Mode    = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp; frame.AddrStack.Mode = AddrModeFlat;

    out << "\n--- call stack (crashing thread) ---\n";
    std::ostringstream rvas;   // collect module-relative addresses for the addr2line hint
    std::string main_module;

    alignas(SYMBOL_INFO) char sym_buf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(sym_buf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen   = MAX_SYM_NAME;

    for (int i = 0; i < 64; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thread, &frame, &ctx,
                         nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
        const DWORD64 addr = frame.AddrPC.Offset;
        if (addr == 0) break;

        // Module + RVA (reliable without symbols).
        HMODULE mod = nullptr;
        char modname[MAX_PATH] = "?";
        DWORD64 base = 0;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(addr), &mod) && mod) {
            char full[MAX_PATH];
            if (GetModuleFileNameA(mod, full, MAX_PATH)) {
                const char* slash = std::strrchr(full, '\\');
                std::snprintf(modname, sizeof modname, "%s", slash ? slash + 1 : full);
            }
            base = reinterpret_cast<DWORD64>(mod);
        }
        const DWORD64 rva = base ? (addr - base) : 0;

        char head[128];
        std::snprintf(head, sizeof head, "#%02d  %-20s +0x%08llX",
                      i, modname, static_cast<unsigned long long>(rva));
        out << head;

        // Best-effort symbol + line.
        DWORD64 disp = 0;
        if (SymFromAddr(proc, addr, &disp, sym))
            out << "  " << sym->Name;
        IMAGEHLP_LINE64 line{}; line.SizeOfStruct = sizeof(line);
        DWORD ld = 0;
        if (SymGetLineFromAddr64(proc, addr, &ld, &line)) {
            const char* slash = std::strrchr(line.FileName, '\\');
            out << "  (" << (slash ? slash + 1 : line.FileName) << ":" << line.LineNumber << ")";
        }
        char tail[48];
        std::snprintf(tail, sizeof tail, "   [0x%016llX]", static_cast<unsigned long long>(addr));
        out << tail << "\n";

        // Collect RVAs for the main app module so we can symbolize a stripped build.
        if (std::strstr(modname, ".exe")) {
            if (main_module.empty()) main_module = modname;
            char r[24]; std::snprintf(r, sizeof r, "0x%llX ", static_cast<unsigned long long>(rva));
            rvas << r;
        }
    }

    if (!main_module.empty()) {
        out << "\n--- symbolize offline (dev machine, matching UNSTRIPPED build) ---\n";
        out << "addr2line -f -C -e " << main_module << " " << rvas.str() << "\n";
    }
    SymCleanup(proc);
}

void write_minidump(const std::string& path, EXCEPTION_POINTERS* ep) {
    HANDLE f = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId          = GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers    = FALSE;
    const auto type = static_cast<MINIDUMP_TYPE>(
        MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory |
        MiniDumpWithDataSegs);
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f, type,
                      ep ? &mei : nullptr, nullptr, nullptr);
    CloseHandle(f);
}
#endif  // _WIN32

// The single place a report is built + written. `ep` is non-null only for SEH.
std::string do_write_report(const std::string& reason, void* ep_void) {
    std::ostringstream out;
    out << "=== " << g_config.app_name << " crash report ===\n";
    out << "time:    " << timestamp(false) << "\n";
    out << "app:     " << g_config.app_name << " " << g_config.version << "\n";
    if (!g_config.build_info.empty()) out << "build:   " << g_config.build_info << "\n";
    out << "reason:  " << reason << "\n";

    if (g_config.context_provider) {
        std::string ctx;
        try { ctx = g_config.context_provider(); } catch (...) {}
        if (!ctx.empty()) out << "\n--- app context ---\n" << ctx
                              << (ctx.back() == '\n' ? "" : "\n");
    }

    std::string dump_path;
#if defined(_WIN32)
    auto* ep = static_cast<EXCEPTION_POINTERS*>(ep_void);
    CONTEXT ctx{};
    if (ep && ep->ContextRecord) ctx = *ep->ContextRecord;
    else                          RtlCaptureContext(&ctx);
    append_stack(out, ctx);

    if (g_config.write_minidump && !g_config.report_dir.empty()) {
        dump_path = g_config.report_dir + "/crash_" + timestamp(true) + ".dmp";
        write_minidump(dump_path, ep);
        out << "\nminidump: " << dump_path << "\n";
    }
#else
    (void)ep_void;
    out << "\n(stack trace + minidump are Windows-only)\n";
#endif

    append_system_info(out);
    append_recent_log(out);

    // Write the report file.
    std::string report_path;
    if (!g_config.report_dir.empty())
        report_path = g_config.report_dir + "/crash_report_" + timestamp(true) + ".txt";
    const std::string text = out.str();
    if (!report_path.empty()) {
        std::ofstream f(report_path, std::ios::trunc);
        if (f) f << text;
    }
    // Always mirror to stderr so a terminal run shows it even if the file failed.
    std::fputs(text.c_str(), stderr);
    std::fflush(stderr);
    return report_path;
}

void flush_logs() {
    if (auto l = spdlog::default_logger()) l->flush();
}

// The terminal path shared by every handler: guard re-entrancy, build the
// report, flush, optionally show a dialog, and return the report path.
std::string handle_fatal(const std::string& reason, void* ep) {
    bool expected = false;
    if (!g_in_handler.compare_exchange_strong(expected, true)) return {};  // already crashing
    flush_logs();
    const std::string path = do_write_report(reason, ep);
    flush_logs();
#if defined(_WIN32)
    if (g_config.show_dialog) {
        std::string msg = g_config.app_name + " crashed.\n\n" + reason + "\n\n";
        msg += path.empty() ? "Could not write a crash report."
                            : ("A crash report was saved to:\n" + path);
        MessageBoxA(nullptr, msg.c_str(), (g_config.app_name + " — crash").c_str(),
                    MB_OK | MB_ICONERROR | MB_TOPMOST);
    }
#endif
    return path;
}

// ---- OS-level handlers ------------------------------------------------------
#if defined(_WIN32)
LONG WINAPI seh_filter(EXCEPTION_POINTERS* ep) {
    std::string reason = "SEH exception ";
    if (ep && ep->ExceptionRecord) {
        const auto& r = *ep->ExceptionRecord;
        reason += exception_code_string(r.ExceptionCode);
        if (r.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r.NumberParameters >= 2) {
            char b[96];
            std::snprintf(b, sizeof b, " %s 0x%016llX",
                          r.ExceptionInformation[0] == 1 ? "writing" :
                          r.ExceptionInformation[0] == 8 ? "executing" : "reading",
                          static_cast<unsigned long long>(r.ExceptionInformation[1]));
            reason += b;
        }
    } else {
        reason += "(no record)";
    }
    handle_fatal(reason, ep);
    return EXCEPTION_EXECUTE_HANDLER;  // let the process die after we've reported
}
#endif

void terminate_handler() {
    std::string reason = "std::terminate (uncaught exception)";
    if (auto e = std::current_exception()) {
        try { std::rethrow_exception(e); }
        catch (const std::exception& ex) { reason = std::string("uncaught std::exception: ") + ex.what(); }
        catch (...)                       { reason = "uncaught non-standard exception"; }
    }
    handle_fatal(reason, nullptr);
    std::_Exit(3);
}

void signal_handler(int sig) {
    const char* name = sig == SIGSEGV ? "SIGSEGV" : sig == SIGABRT ? "SIGABRT"
                     : sig == SIGFPE  ? "SIGFPE"  : sig == SIGILL  ? "SIGILL" : "signal";
    handle_fatal(std::string("signal ") + name, nullptr);
    std::_Exit(3);
}

}  // namespace

// ---- public API -------------------------------------------------------------

void install_crash_handler(const CrashConfig& cfg) {
    g_config = cfg;
#if defined(_WIN32)
    if (!g_config.report_dir.empty()) CreateDirectoryA(g_config.report_dir.c_str(), nullptr);
    SetUnhandledExceptionFilter(seh_filter);
    // Route CRT fatal errors (invalid-parameter, pure-virtual) through abort→SIGABRT.
    SetErrorMode(SetErrorMode(0) | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    std::set_terminate(terminate_handler);
    std::signal(SIGABRT, signal_handler);
    std::signal(SIGSEGV, signal_handler);
    std::signal(SIGFPE,  signal_handler);
    std::signal(SIGILL,  signal_handler);
    g_installed = true;
    spdlog::info("[diag] crash handler installed (reports -> {})",
                 g_config.report_dir.empty() ? "(stderr only)" : g_config.report_dir);
}

std::string write_report(const std::string& reason) {
    // Non-terminating: don't latch g_in_handler.
    flush_logs();
    return do_write_report("MANUAL: " + reason, nullptr);
}

// ---- logging plumbing -------------------------------------------------------

void init_logging(const std::string& log_dir, const std::string& app_name, size_t ring_capacity) {
    auto logger = spdlog::default_logger();
    if (!logger) return;

    // Ring buffer: the last N formatted lines, available crash-time.
    g_ring = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(ring_capacity);
    logger->sinks().push_back(g_ring);

    // Rotating file: survives the process, tail is the pre-crash history.
    if (!log_dir.empty()) {
        try {
#if defined(_WIN32)
            CreateDirectoryA(log_dir.c_str(), nullptr);
#endif
            const std::string path = log_dir + "/" + app_name + ".log";
            auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                to_filename(path), /*max_size*/ static_cast<size_t>(5 * 1024 * 1024),
                /*max_files*/ static_cast<size_t>(3));
            logger->sinks().push_back(file);
        } catch (const std::exception& e) {
            spdlog::warn("[diag] file logging unavailable: {}", e.what());
        }
    }
    logger->flush_on(spdlog::level::info);
}

std::vector<std::string> recent_log_lines(size_t count) {
    if (!g_ring) return {};
    return g_ring->last_formatted(count);
}

}  // namespace gws::diag
