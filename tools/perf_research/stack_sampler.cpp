// Samples the call stack of another process's main thread, from outside.
//
//   stack_sampler <pid> <seconds> <out.txt>
//
// A sampling profiler for code the engine's zones do not cover. Every couple of
// milliseconds it suspends the target's main thread, walks its stack with
// dbghelp (the unwind tables are in the executable; no debug info is needed),
// resumes it and writes the return addresses. sample_report.py turns the file
// into "share of wall time under each function".
//
// No debugger is attached. Attaching one distorts the picture: the process is
// stopped and restarted many times while the debugger enumerates threads and
// modules, so samples pile up on whatever runs right after a blocking wait.
//
// Build:  c++ -O2 stack_sampler.cpp -o stack_sampler.exe -ldbghelp -lpsapi -lwinmm
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

// The process's first thread: the one with the earliest creation time.
DWORD main_thread_of(DWORD pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te{};
    te.dwSize = sizeof te;
    DWORD     best   = 0;
    ULONGLONG best_t = ~0ull;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE h = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!h) continue;
        FILETIME c, e, k, u;
        if (GetThreadTimes(h, &c, &e, &k, &u)) {
            const ULONGLONG t = (static_cast<ULONGLONG>(c.dwHighDateTime) << 32) | c.dwLowDateTime;
            if (t < best_t) {
                best_t = t;
                best   = te.th32ThreadID;
            }
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
    return best;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: stack_sampler <pid> <seconds> <out.txt>\n");
        return 2;
    }
    const DWORD  pid     = static_cast<DWORD>(std::strtoul(argv[1], nullptr, 10));
    const double seconds = std::atof(argv[2]);

    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) {
        std::fprintf(stderr, "cannot open process %lu\n", pid);
        return 1;
    }
    const DWORD tid = main_thread_of(pid);
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!thread) {
        std::fprintf(stderr, "cannot open thread %lu\n", tid);
        return 1;
    }
    SymSetOptions(SYMOPT_DEFERRED_LOADS);
    if (!SymInitialize(process, nullptr, TRUE)) {
        std::fprintf(stderr, "SymInitialize failed\n");
        return 1;
    }
    FILE* out = std::fopen(argv[3], "w");
    if (!out) return 1;

    // Module table first, so the report can turn addresses into module + offset.
    HMODULE mods[1024];
    DWORD   needed = 0;
    if (EnumProcessModules(process, mods, sizeof mods, &needed)) {
        const DWORD n = needed / sizeof(HMODULE);
        for (DWORD i = 0; i < n && i < 1024; ++i) {
            MODULEINFO mi{};
            char       name[MAX_PATH] = {};
            GetModuleInformation(process, mods[i], &mi, sizeof mi);
            GetModuleFileNameExA(process, mods[i], name, MAX_PATH);
            std::fprintf(out, "module %llx %lx %s\n",
                         static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(mi.lpBaseOfDll)),
                         static_cast<unsigned long>(mi.SizeOfImage), name);
        }
    }

    // Sleep() is only as fine as the system timer; ask for 1 ms while sampling.
    timeBeginPeriod(1);

    constexpr int  kMaxFrames = 32;
    const ULONGLONG t_end = GetTickCount64() + static_cast<ULONGLONG>(seconds * 1000.0);
    unsigned       seed    = 12345;
    unsigned long  samples = 0;
    while (GetTickCount64() < t_end) {
        DWORD64 pcs[kMaxFrames];
        int     n = 0;
        if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(thread, &ctx)) {
                STACKFRAME64 sf{};
                sf.AddrPC.Offset    = ctx.Rip;
                sf.AddrPC.Mode      = AddrModeFlat;
                sf.AddrFrame.Offset = ctx.Rbp;
                sf.AddrFrame.Mode   = AddrModeFlat;
                sf.AddrStack.Offset = ctx.Rsp;
                sf.AddrStack.Mode   = AddrModeFlat;
                while (n < kMaxFrames &&
                       StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &sf, &ctx, nullptr,
                                   SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
                    if (sf.AddrPC.Offset == 0) break;
                    pcs[n++] = sf.AddrPC.Offset;
                }
            }
            ResumeThread(thread);
        }
        if (n > 0) {
            std::fprintf(out, "s");
            for (int i = 0; i < n; ++i) std::fprintf(out, " %llx", static_cast<unsigned long long>(pcs[i]));
            std::fprintf(out, "\n");
            ++samples;
        }
        // 2 to 4 ms, jittered so the sampling does not lock onto the frame rate.
        seed = seed * 1664525u + 1013904223u;
        Sleep(2 + (seed >> 16) % 3);
    }
    timeEndPeriod(1);
    std::fclose(out);
    SymCleanup(process);
    std::printf("%lu samples of thread %lu in process %lu\n", samples, tid, pid);
    return 0;
}
