// ====================
// bindless_check — slot bookkeeping for the bindless texture table.
//
// No device, no descriptors. The Vulkan half of bindless fails loudly: a bad
// descriptor write trips validation. The bookkeeping fails SILENTLY, and in
// two specific ways that a screenshot cannot show:
//
//   - a slot handed out twice makes two materials share one texture
//   - a slot never reused runs the table dry on a scene that streams, after
//     which everything samples whatever is in slot 0
//
// Both render something plausible. So the arithmetic is asserted here.
// ====================

#include "vulkan/bindless_index_allocator.h"

#include <cstdio>
#include <set>
#include <string>
#include <vector>
#include <cstdint>

using gws::renderer::gpu::BindlessIndexAllocator;

namespace {
int g_pass = 0, g_fail = 0;
void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "OK" : "FAIL", what.c_str());
    if (ok) ++g_pass; else ++g_fail;
}
}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== bindless_check: texture-table slot bookkeeping ===\n");

    // ---- 1. slots are unique while live -------------------------------------
    {
        BindlessIndexAllocator a(8);
        std::set<uint32_t> seen;
        bool unique = true;
        for (int i = 0; i < 8; ++i) {
            const uint32_t s = a.acquire();
            if (s == BindlessIndexAllocator::kInvalid || !seen.insert(s).second) unique = false;
        }
        check(unique, "eight acquires give eight distinct slots");
        check(a.live() == 8, "and all eight count as live");
    }

    // ---- 2. a full table refuses rather than wrapping ------------------------
    // Wrapping to 0 would be the worst failure available: every later texture
    // silently becomes the first one, and nothing errors.
    {
        BindlessIndexAllocator a(3);
        a.acquire(); a.acquire(); a.acquire();
        check(a.full(), "a table with every slot taken reports full");
        check(a.acquire() == BindlessIndexAllocator::kInvalid,
              "and the next acquire returns kInvalid, not slot 0");
    }

    // ---- 3. released slots come back ----------------------------------------
    // Without reuse a scene that loads and drops textures exhausts a table it
    // never simultaneously filled.
    {
        BindlessIndexAllocator a(4);
        const uint32_t s0 = a.acquire(), s1 = a.acquire();
        a.acquire(); a.acquire();
        check(a.full(), "table full at four");
        a.release(s1);
        check(!a.full(), "releasing one makes room again");
        check(a.acquire() == s1, "and the freed slot is the one handed back");
        a.release(s0);
        check(a.acquire() == s0, "the same holds for a slot freed later");
    }

    // ---- 4. churn does not leak ---------------------------------------------
    // A thousand load/drop cycles through a four-slot table: the pattern a
    // streaming scene actually produces.
    {
        BindlessIndexAllocator a(4);
        bool ok = true;
        for (int i = 0; i < 1000; ++i) {
            const uint32_t s = a.acquire();
            if (s == BindlessIndexAllocator::kInvalid) { ok = false; break; }
            a.release(s);
        }
        check(ok, "1000 acquire/release cycles never exhaust a 4-slot table");
        check(a.live() == 0, "and nothing is left live afterwards");
        check(a.high_water() == 1, "reuse keeps the written range at one slot");
    }

    // ---- 5. release is forgiving -------------------------------------------
    // Teardown paths call release() on slots whose acquire may itself have
    // failed. Turning that into a crash helps nobody.
    {
        BindlessIndexAllocator a(2);
        a.release(BindlessIndexAllocator::kInvalid);
        a.release(999);
        const uint32_t s = a.acquire();
        check(s == 0, "releasing kInvalid and out-of-range slots changes nothing");
    }

    // ---- 6. a zero-capacity table is not a trap -----------------------------
    // What a device reporting too few update-after-bind slots would produce.
    {
        BindlessIndexAllocator a(0);
        check(a.acquire() == BindlessIndexAllocator::kInvalid,
              "a zero-slot table refuses immediately");
        check(a.full(), "and reports itself full");
    }

    std::printf("\nbindless_check: %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) std::printf("bindless_check: ALL OK\n");
    return g_fail == 0 ? 0 : 1;
}
