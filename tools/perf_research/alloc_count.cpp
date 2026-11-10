// Measurement-only allocation counter. Linked into a COPY of the editor with
//   -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=glfwPollEvents
// and static libstdc++, so every C and C++ allocation in the process's own
// code goes through here. Nothing in the engine source is touched.
//
// Counts allocations per frame (a frame = the span between two
// glfwPollEvents calls) over a window of steady-state frames, and records
// the call stacks that allocate inside that window.
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
void* __real_malloc(size_t);
void* __real_calloc(size_t, size_t);
void* __real_realloc(void*, size_t);
void  __real_free(void*);
void  __real_glfwPollEvents(void);
}

namespace {

constexpr int kFirst = 150, kLast = 250;      // frames (kFirst, kLast] are recorded
constexpr int kDepth = 12;

std::atomic<uint64_t> g_n{0}, g_bytes{0}, g_free{0}, g_n_main{0};
std::atomic<int>      g_frame{-1};
std::atomic<bool>     g_rec{false};
DWORD                 g_main_tid = 0;

uint64_t f_n[512], f_bytes[512], f_free[512], f_main[512];
uint64_t last_n = 0, last_bytes = 0, last_free = 0, last_main = 0;

struct Site {
    uint64_t hash;
    uint64_t n;
    uint64_t bytes;
    void*    pc[kDepth];
    int      depth;
};
constexpr size_t kSites = 1 << 14;
Site             g_sites[kSites];
std::atomic_flag g_lock = ATOMIC_FLAG_INIT;

inline void note(size_t sz) {
    g_n.fetch_add(1, std::memory_order_relaxed);
    g_bytes.fetch_add(sz, std::memory_order_relaxed);
    if (GetCurrentThreadId() == g_main_tid) g_n_main.fetch_add(1, std::memory_order_relaxed);
    if (!g_rec.load(std::memory_order_relaxed)) return;

    void*  pc[kDepth];
    USHORT d = RtlCaptureStackBackTrace(1, kDepth, pc, nullptr);
    uint64_t h = 1469598103934665603ull;
    for (USHORT i = 0; i < d && i < 7; ++i) {
        h ^= reinterpret_cast<uint64_t>(pc[i]);
        h *= 1099511628211ull;
    }
    if (h == 0) h = 1;
    while (g_lock.test_and_set(std::memory_order_acquire)) {}
    size_t i = h & (kSites - 1);
    for (size_t probe = 0; probe < kSites; ++probe, i = (i + 1) & (kSites - 1)) {
        Site& s = g_sites[i];
        if (s.n == 0) {
            s.hash  = h;
            s.depth = d;
            std::memcpy(s.pc, pc, d * sizeof(void*));
        }
        if (s.hash == h) {
            s.n++;
            s.bytes += sz;
            break;
        }
    }
    g_lock.clear(std::memory_order_release);
}

void dump() {
    const char* out = std::getenv("GWS_ALLOC_OUT");
    if (!out) return;
    FILE* f = std::fopen(out, "w");
    if (!f) return;
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    std::fprintf(f, "base %llx\n", static_cast<unsigned long long>(base));
    const int frames = kLast - kFirst;
    for (int i = 0; i < frames; ++i)
        std::fprintf(f, "frame %d allocs %llu bytes %llu frees %llu main %llu\n", kFirst + 1 + i,
                     (unsigned long long)f_n[i], (unsigned long long)f_bytes[i],
                     (unsigned long long)f_free[i], (unsigned long long)f_main[i]);
    for (size_t i = 0; i < kSites; ++i) {
        const Site& s = g_sites[i];
        if (s.n == 0) continue;
        std::fprintf(f, "site %llu %llu", (unsigned long long)s.n, (unsigned long long)s.bytes);
        for (int k = 0; k < s.depth; ++k)
            std::fprintf(f, " %llx", (unsigned long long)(reinterpret_cast<uintptr_t>(s.pc[k]) - base));
        std::fprintf(f, "\n");
    }
    std::fclose(f);
}

}  // namespace

extern "C" {

void* __wrap_malloc(size_t n) {
    note(n);
    return __real_malloc(n);
}
void* __wrap_calloc(size_t a, size_t b) {
    note(a * b);
    return __real_calloc(a, b);
}
void* __wrap_realloc(void* p, size_t n) {
    if (n) note(n);
    return __real_realloc(p, n);
}
void __wrap_free(void* p) {
    if (p) g_free.fetch_add(1, std::memory_order_relaxed);
    __real_free(p);
}

void __wrap_glfwPollEvents(void) {
    const int f = g_frame.fetch_add(1) + 1;
    if (f == 0) g_main_tid = GetCurrentThreadId();
    const uint64_t n = g_n.load(), b = g_bytes.load(), fr = g_free.load(), mn = g_n_main.load();
    if (f > kFirst && f <= kLast) {
        const int i = f - kFirst - 1;
        f_n[i]     = n - last_n;
        f_bytes[i] = b - last_bytes;
        f_free[i]  = fr - last_free;
        f_main[i]  = mn - last_main;
    }
    last_n = n; last_bytes = b; last_free = fr; last_main = mn;
    if (f == kFirst) g_rec.store(true);
    if (f == kLast) {
        g_rec.store(false);
        dump();
    }
    __real_glfwPollEvents();
}

}  // extern "C"
