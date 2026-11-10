// Measurement-only: which call stacks own the heap memory that is still alive
// once the editor has reached steady state. Linked into a copy of the editor
// with --wrap=malloc/calloc/realloc/free/glfwPollEvents (static libstdc++).
#include <windows.h>
#include <malloc.h>
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

constexpr int kDumpFrame = 150;
constexpr int kDepth     = 14;

struct Site {
    uint64_t hash;
    uint64_t n_total, bytes_total;
    int64_t  n_live, bytes_live;
    void*    pc[kDepth];
    int      depth;
};
constexpr size_t kSites = 1 << 16;
Site g_sites[kSites];

struct Ptr { void* p; uint32_t site; uint32_t size; };
constexpr size_t kPtrs = 1 << 23;                 // 8M slots, 128 MB of bss
Ptr* g_ptrs = nullptr;
void* const kTomb = reinterpret_cast<void*>(uintptr_t(1));

std::atomic_flag g_lock = ATOMIC_FLAG_INIT;
std::atomic<int> g_frame{-1};
bool     g_on = true;
uint64_t g_total_live = 0, g_peak_live = 0, g_events = 0, g_untracked_free = 0;

inline size_t ptr_hash(void* p) {
    uint64_t x = reinterpret_cast<uint64_t>(p) >> 4;
    x *= 0x9E3779B97F4A7C15ull;
    return static_cast<size_t>(x >> 41) & (kPtrs - 1);
}

void lock()   { while (g_lock.test_and_set(std::memory_order_acquire)) {} }
void unlock() { g_lock.clear(std::memory_order_release); }

void on_alloc(void* p, size_t sz) {
    if (!p || !g_on) return;
    void*  pc[kDepth];
    USHORT d = RtlCaptureStackBackTrace(1, kDepth, pc, nullptr);
    uint64_t h = 1469598103934665603ull;
    for (USHORT i = 0; i < d && i < 8; ++i) { h ^= reinterpret_cast<uint64_t>(pc[i]); h *= 1099511628211ull; }
    if (h == 0) h = 1;
    lock();
    if (!g_ptrs) g_ptrs = static_cast<Ptr*>(VirtualAlloc(nullptr, kPtrs * sizeof(Ptr), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    size_t si = h & (kSites - 1);
    for (size_t probe = 0; probe < kSites; ++probe, si = (si + 1) & (kSites - 1)) {
        Site& s = g_sites[si];
        if (s.hash == 0) { s.hash = h; s.depth = d; std::memcpy(s.pc, pc, d * sizeof(void*)); }
        if (s.hash == h) { s.n_total++; s.bytes_total += sz; s.n_live++; s.bytes_live += static_cast<int64_t>(sz); break; }
    }
    size_t i = ptr_hash(p);
    for (size_t probe = 0; probe < kPtrs; ++probe, i = (i + 1) & (kPtrs - 1)) {
        if (g_ptrs[i].p == nullptr || g_ptrs[i].p == kTomb) {
            g_ptrs[i] = Ptr{p, static_cast<uint32_t>(si), static_cast<uint32_t>(sz > 0xFFFFFFFFu ? 0xFFFFFFFFu : sz)};
            break;
        }
    }
    g_total_live += sz;
    if (g_total_live > g_peak_live) g_peak_live = g_total_live;
    ++g_events;
    unlock();
}

void on_free(void* p) {
    if (!p || !g_on) return;
    lock();
    if (g_ptrs) {
        size_t i = ptr_hash(p);
        bool found = false;
        for (size_t probe = 0; probe < kPtrs; ++probe, i = (i + 1) & (kPtrs - 1)) {
            if (g_ptrs[i].p == nullptr) break;
            if (g_ptrs[i].p == p) {
                Site& s = g_sites[g_ptrs[i].site];
                s.n_live--;
                s.bytes_live -= g_ptrs[i].size;
                g_total_live -= g_ptrs[i].size;
                g_ptrs[i].p = kTomb;
                found = true;
                break;
            }
        }
        if (!found) ++g_untracked_free;
    }
    unlock();
}

void dump() {
    const char* out = std::getenv("GWS_ALLOC_OUT");
    if (!out) return;
    g_on = false;
    FILE* f = std::fopen(out, "w");
    if (!f) return;
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    std::fprintf(f, "base %llx\n", static_cast<unsigned long long>(base));
    std::fprintf(f, "live_bytes %llu peak_bytes %llu events %llu untracked_frees %llu\n",
                 (unsigned long long)g_total_live, (unsigned long long)g_peak_live,
                 (unsigned long long)g_events, (unsigned long long)g_untracked_free);
    for (size_t i = 0; i < kSites; ++i) {
        const Site& s = g_sites[i];
        if (s.hash == 0) continue;
        std::fprintf(f, "site %lld %lld %llu %llu", (long long)s.bytes_live, (long long)s.n_live,
                     (unsigned long long)s.bytes_total, (unsigned long long)s.n_total);
        for (int k = 0; k < s.depth; ++k)
            std::fprintf(f, " %llx", (unsigned long long)(reinterpret_cast<uintptr_t>(s.pc[k]) - base));
        std::fprintf(f, "\n");
    }
    std::fclose(f);
}

}  // namespace

extern "C" {

void* __wrap_malloc(size_t n) {
    void* p = __real_malloc(n);
    on_alloc(p, n);
    return p;
}
void* __wrap_calloc(size_t a, size_t b) {
    void* p = __real_calloc(a, b);
    on_alloc(p, a * b);
    return p;
}
void* __wrap_realloc(void* old, size_t n) {
    on_free(old);
    void* p = __real_realloc(old, n);
    if (n) on_alloc(p, n);
    return p;
}
void __wrap_free(void* p) {
    on_free(p);
    __real_free(p);
}

void __wrap_glfwPollEvents(void) {
    const int f = g_frame.fetch_add(1) + 1;
    if (f == kDumpFrame) dump();
    __real_glfwPollEvents();
}

}  // extern "C"
