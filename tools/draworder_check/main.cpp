// ============================================================================
// draworder_check — the radix front-to-back order must be exactly the order
// std::sort(depth, then material) produced, on the inputs that break sorts.
//
// draw_sort used std::sort with a comparator reading float depths through an
// index -- ~1 ms a frame at 15k draws. front_to_back_order (draw_order.h)
// replaces it with a radix sort on order-preserving 32-bit keys plus a
// tie-break pass. A faster sort that orders differently is a regression that
// shows up only as overdraw or descriptor churn, so the assertions compare the
// two orders element by element on the cases where a key mapping or a tie
// pass goes wrong:
//
//   * negative depths (behind the camera) -- the sign bit must invert order;
//   * -0.0 and +0.0, which compare equal as floats and must TIE, not sort;
//   * many exact ties, which must fall back to the material order;
//   * large inputs, so every radix byte actually varies.
// ============================================================================
#include "draw_order.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace gws::renderer::gpu;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

// The comparator draw_sort used before.
std::vector<uint32_t> reference(const std::vector<float>& depth, const std::vector<uint64_t>& tie) {
    std::vector<uint32_t> o(depth.size());
    for (size_t i = 0; i < o.size(); ++i) o[i] = static_cast<uint32_t>(i);
    std::sort(o.begin(), o.end(), [&](uint32_t a, uint32_t b) {
        if (depth[a] != depth[b]) return depth[a] < depth[b];
        return tie[a] < tie[b];
    });
    return o;
}

// Same order means the same (depth, tie) at every position. Indices with equal
// depth AND tie may legitimately swap -- std::sort does not order them either.
bool same_order(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b,
                const std::vector<float>& depth, const std::vector<uint64_t>& tie) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!(depth[a[i]] == depth[b[i]]) || tie[a[i]] != tie[b[i]]) return false;
    return true;
}

bool is_permutation_of_iota(std::vector<uint32_t> o) {
    std::sort(o.begin(), o.end());
    for (size_t i = 0; i < o.size(); ++i) if (o[i] != i) return false;
    return true;
}

bool agrees(const std::vector<float>& depth, const std::vector<uint64_t>& tie) {
    std::vector<uint32_t> got;
    DrawOrderScratch s;
    front_to_back_order(depth.data(), tie.data(), depth.size(), got, s);
    return is_permutation_of_iota(got) && same_order(got, reference(depth, tie), depth, tie);
}

}  // namespace

int main() {
    std::printf("=== draworder_check ===\n\n");

    std::printf("The key mapping preserves float order:\n");
    {
        const float v[] = {-1e30f, -5.0f, -1.0f, -1e-30f, 0.0f, 1e-30f, 1.0f, 5.0f, 1e30f};
        bool ok = true;
        for (size_t i = 0; i + 1 < sizeof v / sizeof v[0]; ++i)
            ok = ok && sortable_float_key(v[i]) < sortable_float_key(v[i + 1]);
        check(ok, "strictly increasing across negative, zero and positive");
        check(sortable_float_key(-0.0f) == sortable_float_key(0.0f), "-0.0 and +0.0 tie");
    }

    std::printf("\nThe order matches std::sort(depth, then material):\n");
    std::mt19937 rng(1234);
    {
        std::vector<float> d = {3.0f, -2.0f, 0.0f, -0.0f, 3.0f, 1.0f, -2.0f, 0.0f};
        std::vector<uint64_t> t = {9, 1, 5, 2, 4, 7, 0, 5};
        check(agrees(d, t), "a small hand-made case: negatives, signed zeros, ties");
    }
    {
        std::uniform_real_distribution<float> u(-500.0f, 500.0f);
        std::vector<float> d(20000);
        std::vector<uint64_t> t(20000);
        for (size_t i = 0; i < d.size(); ++i) { d[i] = u(rng); t[i] = rng() % 7; }
        check(agrees(d, t), "20,000 random depths, both signs (every radix byte varies)");
    }
    {
        // A grid of objects at identical distances: ties everywhere.
        std::vector<float> d(5000);
        std::vector<uint64_t> t(5000);
        for (size_t i = 0; i < d.size(); ++i) {
            d[i] = float(i % 13) * 2.5f - 10.0f;
            t[i] = (i * 2654435761u) % 5;
        }
        check(agrees(d, t), "5,000 draws on 13 depths -- ties fall back to material");
    }
    {
        std::vector<float> d(1000, 0.0f);
        for (size_t i = 0; i < d.size(); i += 2) d[i] = -0.0f;
        std::vector<uint64_t> t(1000);
        for (size_t i = 0; i < t.size(); ++i) t[i] = 999 - i;
        check(agrees(d, t), "all depths +/-0.0: one run, ordered purely by material");
    }
    // Passes whose byte is the same for every key are skipped, so the number
    // of real passes can be odd -- the result then lands in the scratch
    // buffers and is swapped back. Cover 1, 3 and 4 real passes, with ONE
    // scratch and output vector reused across calls, as the editor does.
    {
        std::vector<uint32_t> got;
        DrawOrderScratch s;
        auto agrees_reused = [&](const std::vector<float>& d, const std::vector<uint64_t>& t) {
            front_to_back_order(d.data(), t.data(), d.size(), got, s);
            return is_permutation_of_iota(got) && same_order(got, reference(d, t), d, t);
        };
        std::vector<float> one(3000), three(3000), four(3000);
        std::vector<uint64_t> t(3000);
        std::uniform_real_distribution<float> in12(1.0f, 2.0f), wide(-300.0f, 300.0f);
        for (size_t i = 0; i < t.size(); ++i) {
            uint32_t bits = 0x3F800000u + static_cast<uint32_t>(rng() % 256);  // 1.0f + up to 255 ulp
            std::memcpy(&one[i], &bits, sizeof bits);
            three[i] = in12(rng);                                            // one exponent
            four[i]  = wide(rng);
            t[i]     = rng() % 3;
        }
        bool ok = true;
        for (int round = 0; round < 3; ++round) {
            ok = ok && agrees_reused(one, t) && agrees_reused(three, t) && agrees_reused(four, t) &&
                 agrees_reused(three, t) && agrees_reused(one, t);
        }
        check(ok, "1, 3 and 4 real radix passes, alternating, one scratch reused (skipped passes)");
    }
    {
        std::vector<float> d;  std::vector<uint64_t> t;
        check(agrees(d, t), "an empty list");
        d = {42.0f}; t = {7};
        check(agrees(d, t), "a single draw");
    }

    std::printf("\nWhat it costs at 15k draws (reported, not asserted):\n");
    {
        std::uniform_real_distribution<float> u(0.0f, 400.0f);
        std::vector<float> d(15000);
        std::vector<uint64_t> t(15000);
        for (size_t i = 0; i < d.size(); ++i) { d[i] = u(rng); t[i] = rng() % 2; }
        std::vector<uint32_t> o;
        DrawOrderScratch s;
        using clk = std::chrono::steady_clock;
        const int reps = 50;
        auto t0 = clk::now();
        for (int r = 0; r < reps; ++r) front_to_back_order(d.data(), t.data(), d.size(), o, s);
        auto t1 = clk::now();
        size_t sink = 0;
        for (int r = 0; r < reps; ++r) sink += reference(d, t)[0];
        auto t2 = clk::now();
        std::printf("  radix %.3f ms, std::sort %.3f ms (sink %zu)\n",
                    std::chrono::duration<double, std::milli>(t1 - t0).count() / reps,
                    std::chrono::duration<double, std::milli>(t2 - t1).count() / reps, sink);
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail)
        std::printf("FAIL draworder_check — the radix draw order differs from depth-then-material.\n");
    return g_fail ? 1 : 0;
}
