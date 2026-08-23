#include "crash_handler.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/ringbuffer_sink.h>

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <chrono>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <dbghelp.h>
#  include <tlhelp32.h>
#endif

namespace gws::diag {

namespace {

CrashConfig                                    g_config;
std::atomic<bool>                              g_installed{false};

// Create a directory tree (all intermediate components), no-throw.
void ensure_dir(const std::string& dir) {
    if (dir.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
}
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
    // Per-process too: a stall that eats memory is a very different bug
    // depending on whether the memory was ours.
    out << "mem: " << memory_summary() << "\n";
#else
    out << "os:  (non-Windows)\n";
#endif
}

// ---- breadcrumbs + frame history --------------------------------------------

std::mutex g_crumb_mutex;
struct Crumb { uint64_t at_ns; std::string text; };
std::vector<Crumb> g_crumbs;          // ring, newest appended
size_t             g_crumb_next = 0;
constexpr size_t   kMaxCrumbs = 96;

std::mutex          g_frame_mutex;
std::vector<double> g_frame_ms;       // ring of recent frame times
size_t              g_frame_next = 0;
constexpr size_t    kMaxFrames = 240;

uint64_t mono_ns();   // defined with the watchdog below

void append_breadcrumbs(std::ostringstream& out) {
    out << "\n--- breadcrumbs (newest last) ---\n";
    std::lock_guard<std::mutex> lk(g_crumb_mutex);
    if (g_crumbs.empty()) { out << "(none recorded)\n"; return; }
    const uint64_t now = mono_ns();
    // Walk the ring oldest-first.
    for (size_t i = 0; i < g_crumbs.size(); ++i) {
        const Crumb& c = g_crumbs[(g_crumb_next + i) % g_crumbs.size()];
        if (c.text.empty()) continue;
        char buf[32];
        std::snprintf(buf, sizeof buf, "-%7.2fs  ", double(now - c.at_ns) / 1.0e9);
        out << buf << c.text << "\n";
    }
}

void append_frame_history(std::ostringstream& out) {
    out << "\n--- frame times, most recent last (ms) ---\n";
    std::lock_guard<std::mutex> lk(g_frame_mutex);
    if (g_frame_ms.empty()) { out << "(none recorded)\n"; return; }
    double sum = 0.0, worst = 0.0;
    for (double v : g_frame_ms) { sum += v; if (v > worst) worst = v; }
    char buf[128];
    std::snprintf(buf, sizeof buf, "count %zu, mean %.2fms (%.0f fps), worst %.2fms\n",
                  g_frame_ms.size(), sum / double(g_frame_ms.size()),
                  1000.0 / (sum / double(g_frame_ms.size())), worst);
    out << buf;
    int printed = 0;
    for (size_t i = 0; i < g_frame_ms.size(); ++i) {
        const double v = g_frame_ms[(g_frame_next + i) % g_frame_ms.size()];
        std::snprintf(buf, sizeof buf, "%7.2f", v);
        out << buf;
        if (++printed % 12 == 0) out << "\n";
    }
    if (printed % 12 != 0) out << "\n";
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
// `thread` defaults to the caller, but the hang watchdog passes the SUSPENDED
// main thread's handle -- walking a stack that is not your own is the whole
// point when the thread you care about is the one that stopped responding.
void append_stack(std::ostringstream& out, CONTEXT ctx, HANDLE thread_in = nullptr,
                  const char* what = nullptr) {
    const HANDLE proc = GetCurrentProcess();
    const HANDLE thread = (thread_in != nullptr) ? thread_in : GetCurrentThread();

    // ONCE per process, not once per stack. SymInitialize with
    // fInvadeProcess=TRUE enumerates and loads symbols for every loaded
    // module -- including the ~100MB NVIDIA driver DLLs. A hang report walks
    // 3 main-thread samples plus up to 16 other threads, so calling it per
    // stack ran it 19 times and made the report itself consume gigabytes and
    // seconds, inflating the very stall it was measuring.
    static std::atomic<bool> sym_ready{false};
    if (!sym_ready.exchange(true)) {
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        SymInitialize(proc, nullptr, TRUE);
    }

    STACKFRAME64 frame{};
    frame.AddrPC.Offset    = ctx.Rip; frame.AddrPC.Mode    = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp; frame.AddrStack.Mode = AddrModeFlat;

    out << "\n--- call stack ("
        << (what != nullptr ? what : (thread_in != nullptr ? "hung thread" : "crashing thread"))
        << ") ---\n";
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

    append_frame_history(out);
    append_breadcrumbs(out);
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

// ---- hang watchdog ----------------------------------------------------------

std::atomic<uint64_t> g_heartbeat_ns{0};
std::atomic<bool>     g_watchdog_stop{false};
std::unique_ptr<std::thread> g_watchdog_thread;
#if defined(_WIN32)
HANDLE g_main_thread = nullptr;
#endif

uint64_t mono_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

#if defined(_WIN32)
// Grab the stuck thread's stack. Suspend only long enough to copy its CONTEXT
// -- walking the stack while it is suspended risks deadlocking on a CRT or
// heap lock the suspended thread happens to hold, and a hung thread's stack is
// not moving anyway.
// Best-effort thread name (Win10 1607+). Resolved dynamically because MinGW's
// import libs do not always carry it.
std::string thread_name_of(HANDLE th) {
    using GetThreadDescriptionFn = HRESULT (WINAPI*)(HANDLE, PWSTR*);
    static auto fn = reinterpret_cast<GetThreadDescriptionFn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription"));
    if (fn == nullptr) return "";
    PWSTR desc = nullptr;
    if (FAILED(fn(th, &desc)) || desc == nullptr) return "";
    char buf[128] = {};
    WideCharToMultiByte(CP_UTF8, 0, desc, -1, buf, sizeof buf - 1, nullptr, nullptr);
    LocalFree(desc);
    return buf;
}

// Every OTHER thread in the process. When the main thread is parked in
// vkWaitForFences or a job-system wait, its own stack says only "waiting" --
// the answer is on whichever thread is not finishing. Sampling only the stuck
// thread would have made this whole exercise say "it is waiting" and stop.
std::string sample_all_other_threads() {
    std::ostringstream out;
    const DWORD me = GetCurrentThreadId();          // the watchdog itself
    const DWORD main_tid = g_main_thread != nullptr ? GetThreadId(g_main_thread) : 0;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return "(thread snapshot failed)\n";

    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    int sampled = 0;
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != GetCurrentProcessId()) continue;
            if (te.th32ThreadID == me || te.th32ThreadID == main_tid) continue;
            if (++sampled > 16) break;              // keep the report bounded

            HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                   THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            if (th == nullptr) continue;

            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_FULL;
            if (SuspendThread(th) != DWORD(-1)) {
                const BOOL got = GetThreadContext(th, &ctx);
                ResumeThread(th);                   // resume BEFORE walking
                if (got) {
                    const std::string nm = thread_name_of(th);
                    out << "\n[thread " << te.th32ThreadID;
                    if (!nm.empty()) out << " \"" << nm << "\"";
                    out << "]\n";
                    append_stack(out, ctx, th, "other thread");
                }
            }
            CloseHandle(th);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    if (sampled == 0) out << "(no other threads)\n";
    return out.str();
}

std::string sample_hung_stack() {
    if (g_main_thread == nullptr) return "(no main thread handle)\n";
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_FULL;
    if (SuspendThread(g_main_thread) == DWORD(-1)) return "(SuspendThread failed)\n";
    const BOOL got = GetThreadContext(g_main_thread, &ctx);
    ResumeThread(g_main_thread);
    if (!got) return "(GetThreadContext failed)\n";

    std::ostringstream out;
    append_stack(out, ctx, g_main_thread);
    return out.str();
}
#endif

#if defined(_WIN32)
// Is the main thread merely PARKED, rather than stuck?
//
// The watchdog fires on "the main loop stopped ticking", and a modal Windows
// dialog stops it entirely legitimately -- every hang report on the reporting
// machine turned out to be this. All three showed the same innermost frame,
// and one showed the whole chain:
//
//     NtUserWaitMessage <- IsDialogMessageA <- DialogBoxIndirectParamW <- comdlg32
//
// which is a file-open dialog sitting there while somebody reads it. Each of
// those wrote an 8-9 MB minidump and a report titled HANG.
//
// NtUserWaitMessage is a sound discriminator rather than a guess: a thread
// inside it is blocked waiting for INPUT and is by definition consuming
// nothing. A genuine freeze parks somewhere else entirely -- a fence wait, a
// lock, or a spin -- and still reports.
//
// Costs one suspend-and-walk, versus the 4 s of sampling plus a minidump it
// avoids.
bool main_thread_is_parked_on_input() {
    const std::string top = sample_hung_stack();
    // Look only at the innermost frame: deeper frames are the app's own code,
    // which is where it will resume from and says nothing about being stuck.
    const size_t first = top.find("#00");
    if (first == std::string::npos) return false;
    const size_t eol = top.find('\n', first);
    const std::string frame0 = top.substr(first, eol == std::string::npos ? std::string::npos
                                                                          : eol - first);
    return frame0.find("NtUserWaitMessage") != std::string::npos ||
           frame0.find("NtUserMsgWaitForMultipleObjects") != std::string::npos;
}
#endif

void write_hang_report(double stalled_seconds) {
    // Announce BEFORE sampling. Collecting three stacks takes about four
    // seconds, and a frozen editor is usually killed by the user long before
    // that -- so the detection itself has to reach the log immediately, or the
    // whole report dies with the process and the freeze leaves no trace at all.
    spdlog::critical("[hang] main loop stalled {:.1f}s — sampling stacks...", stalled_seconds);
    if (auto l = spdlog::default_logger()) l->flush();

    std::ostringstream out;
    out << "=== " << g_config.app_name << " HANG report ===\n";
    out << "time:    " << timestamp(false) << "\n";
    out << "app:     " << g_config.app_name << " " << g_config.version << "\n";
    if (!g_config.build_info.empty()) out << "build:   " << g_config.build_info << "\n";
    out << "reason:  main loop stopped ticking for " << stalled_seconds << "s\n";
    out << "\nNOTE: this is a FREEZE, not a crash. Nothing threw and nothing\n"
           "signalled, so no crash report exists for it.\n";

    if (g_config.context_provider) {
        std::string c;
        try { c = g_config.context_provider(); } catch (...) {}
        if (!c.empty()) out << "\n--- app context ---\n" << c
                            << (c.back() == '\n' ? "" : "\n");
    }

#if defined(_WIN32)
    // Three samples: identical stacks mean a spin or a blocking wait; a moving
    // stack means it is grinding, not stuck.
    for (int i = 0; i < 3; ++i) {
        out << "\n=== sample " << (i + 1) << " of 3 ===\n";
        out << sample_hung_stack();
        if (i < 2) std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    // Once is enough for the rest: if the main thread is blocked, what matters
    // is which other thread is holding things up, not how it evolves.
    out << "\n=== other threads ===\n" << sample_all_other_threads();
    if (g_config.write_minidump && !g_config.report_dir.empty()) {
        const std::string dump = g_config.report_dir + "/hang_" + timestamp(true) + ".dmp";
        write_minidump(dump, nullptr);
        out << "\nminidump: " << dump << "\n";
    }
#else
    out << "\n(stack sampling is Windows-only)\n";
#endif

    append_frame_history(out);
    append_breadcrumbs(out);
    append_system_info(out);
    append_recent_log(out);

    std::string path;
    if (!g_config.report_dir.empty())
        path = g_config.report_dir + "/hang_report_" + timestamp(true) + ".txt";
    const std::string text = out.str();
    if (!path.empty()) {
        std::ofstream f(path, std::ios::trunc);
        if (f) f << text;
    }
    // Also name it in the log, so the ordinary editor.log says a hang happened
    // and where the full report went.
    spdlog::critical("[hang] main loop stalled {:.1f}s — report: {}",
                     stalled_seconds, path.empty() ? "(not written)" : path);
    if (auto l = spdlog::default_logger()) l->flush();
}

void watchdog_loop(double threshold_s) {
    bool reported_this_stall = false;
    uint64_t last_seen = g_heartbeat_ns.load(std::memory_order_relaxed);
    while (!g_watchdog_stop.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        const uint64_t beat = g_heartbeat_ns.load(std::memory_order_relaxed);
        if (beat == 0) continue;          // not started ticking yet
        if (beat != last_seen) {          // a new frame began; re-arm reporting
            last_seen = beat;
            reported_this_stall = false;
        }
        // Measured against the CURRENT beat on every poll, not only when the
        // beat is unchanged between two polls -- the earlier version needed a
        // stall to straddle two consecutive polls, so it slept straight
        // through a 735ms stall that the frame timer did report.
        const double stalled = double(mono_ns() - beat) / 1.0e9;
        if (stalled >= threshold_s && !reported_this_stall) {
            reported_this_stall = true;   // once per stall, not once per check
#if defined(_WIN32)
            if (main_thread_is_parked_on_input()) {
                // Not a hang: a modal dialog is open, or the window is simply
                // waiting for input. Say it once so the gap in the timeline is
                // explained, and write no report and no 8 MB minidump.
                spdlog::info("[hang] main loop idle {:.1f}s waiting on window messages "
                             "(modal dialog or no input) — not a hang, no report written",
                             stalled);
                continue;
            }
#endif
            write_hang_report(stalled);
        }
    }
}

}  // namespace

// ---- public API -------------------------------------------------------------

void install_crash_handler(const CrashConfig& cfg) {
    g_config = cfg;
    ensure_dir(g_config.report_dir);
#if defined(_WIN32)
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

std::string memory_summary() {
#if defined(_WIN32)
    // Per-process counters. K32GetProcessMemoryInfo lives in kernel32, so this
    // needs no extra link library.
    struct PMCEX {                       // PROCESS_MEMORY_COUNTERS_EX layout
        DWORD  cb; DWORD PageFaultCount;
        SIZE_T PeakWorkingSetSize, WorkingSetSize;
        SIZE_T QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage;
        SIZE_T QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage;
        SIZE_T PagefileUsage, PeakPagefileUsage, PrivateUsage;
    };
    using Fn = BOOL (WINAPI*)(HANDLE, void*, DWORD);
    static auto fn = reinterpret_cast<Fn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo"));

    double ws_gb = 0.0, priv_gb = 0.0;
    if (fn != nullptr) {
        PMCEX pmc{};
        pmc.cb = sizeof(pmc);
        if (fn(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            ws_gb   = double(pmc.WorkingSetSize) / (1024.0 * 1024 * 1024);
            priv_gb = double(pmc.PrivateUsage)   / (1024.0 * 1024 * 1024);
        }
    }
    MEMORYSTATUSEX ms{}; ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    char b[192];
    std::snprintf(b, sizeof b, "proc ws %.2f GB, private %.2f GB | sys %.1f/%.1f GB free",
                  ws_gb, priv_gb,
                  ms.ullAvailPhys / (1024.0 * 1024 * 1024),
                  ms.ullTotalPhys / (1024.0 * 1024 * 1024));
    return b;
#else
    return "(non-Windows)";
#endif
}

void install_hang_watchdog(double seconds) {
    if (g_watchdog_thread) return;   // idempotent
#if defined(_WIN32)
    // A pseudo-handle from GetCurrentThread() is only meaningful to the thread
    // that asked for it, so duplicate it into a real one the watchdog can use.
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                         GetCurrentProcess(), &g_main_thread,
                         0, FALSE, DUPLICATE_SAME_ACCESS)) {
        spdlog::warn("[hang] could not duplicate main thread handle; watchdog disabled");
        return;
    }
#endif
    g_heartbeat_ns.store(mono_ns(), std::memory_order_relaxed);
    g_watchdog_stop.store(false, std::memory_order_relaxed);
    g_watchdog_thread = std::make_unique<std::thread>(watchdog_loop, seconds);
    g_watchdog_thread->detach();      // outlives shutdown; process exit reaps it
    spdlog::info("[hang] watchdog armed — reports a stack if a frame takes over {:.1f}s",
                 seconds);
}

void breadcrumb(std::string text) {
    std::lock_guard<std::mutex> lk(g_crumb_mutex);
    if (g_crumbs.size() < kMaxCrumbs) {
        g_crumbs.push_back(Crumb{mono_ns(), std::move(text)});
        g_crumb_next = 0;                     // still filling; oldest is index 0
    } else {
        g_crumbs[g_crumb_next] = Crumb{mono_ns(), std::move(text)};
        g_crumb_next = (g_crumb_next + 1) % kMaxCrumbs;
    }
}

void record_frame_ms(double ms) {
    std::lock_guard<std::mutex> lk(g_frame_mutex);
    if (g_frame_ms.size() < kMaxFrames) {
        g_frame_ms.push_back(ms);
        g_frame_next = 0;
    } else {
        g_frame_ms[g_frame_next] = ms;
        g_frame_next = (g_frame_next + 1) % kMaxFrames;
    }
}

void hang_heartbeat() {
    g_heartbeat_ns.store(mono_ns(), std::memory_order_relaxed);
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
            ensure_dir(log_dir);
            std::string path = log_dir + "/" + app_name + ".log";

            // ONE PROCESS PER FILE. rotating_file_sink_mt is thread-safe, not
            // process-safe: two editors each hold their own handle and their own
            // mutex, so their writes interleave -- four torn lines are visible
            // in the shipped log, one of them mid-timestamp. Past the 5 MB
            // rotation threshold it stops being merely ugly and becomes lossy:
            // one process renames the file and opens a new one while the other
            // keeps writing into the renamed copy, and those lines are gone.
            //
            // That corrupted the evidence for a real investigation. A session
            // where two editors ran at once -- one throttled to 20 fps in the
            // background -- merged into what read as a single session stalling
            // 492 times, and the GPU-contention artefacts looked like the bug
            // being hunted.
            //
            // A named mutex rather than a lock file, because the OS releases it
            // when the process dies: a crashed editor never leaves a stale claim
            // that would push every later run onto a suffixed name.
            bool second_instance = false;
#if defined(_WIN32)
            {
                static HANDLE s_log_claim = nullptr;   // held for process lifetime
                const std::string claim = "Local\\GWS_log_" + app_name;
                s_log_claim = CreateMutexA(nullptr, TRUE, claim.c_str());
                if (s_log_claim == nullptr || GetLastError() == ERROR_ALREADY_EXISTS)
                    second_instance = true;
            }
            if (second_instance) {
                path = log_dir + "/" + app_name + "-" +
                       std::to_string(static_cast<unsigned long>(GetCurrentProcessId())) + ".log";
            }
#endif
            auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                to_filename(path), /*max_size*/ static_cast<size_t>(5 * 1024 * 1024),
                /*max_files*/ static_cast<size_t>(3));
            logger->sinks().push_back(file);
            if (second_instance) {
                // Say so in the log itself. Someone reading a suffixed file has
                // to know another editor was running, or the two half-timelines
                // are more misleading than one interleaved one.
                spdlog::warn("[diag] another {} is already running — this process logs to {} "
                             "(one process per file; see crash_handler.cpp)", app_name, path);
            }
        } catch (const std::exception& e) {
            spdlog::warn("[diag] file logging unavailable: {}", e.what());
        }
    }
    // flush_on(info) meant EVERY info/warning line hit the disk synchronously.
    // That is fine at a handful of lines a second and ruinous when something
    // logs per frame -- a stuck audio clip was logging at frame rate, so the
    // renderer was paying a disk flush every frame. Errors still flush
    // immediately (they precede the interesting failures), everything else is
    // flushed on a 1s timer, and the crash/hang paths flush explicitly before
    // writing a report, so nothing is lost where it matters.
    logger->flush_on(spdlog::level::err);
    spdlog::flush_every(std::chrono::seconds(1));
}

std::vector<std::string> recent_log_lines(size_t count) {
    if (!g_ring) return {};
    return g_ring->last_formatted(count);
}

}  // namespace gws::diag
