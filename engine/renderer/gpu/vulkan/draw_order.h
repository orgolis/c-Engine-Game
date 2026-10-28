/**
 * @file draw_order.h
 * @brief Front-to-back draw order: ascending depth, ties broken by material.
 *
 * Free of Vulkan and of DrawItem, so the ordering can be checked against a
 * plain std::sort reference without a device (draworder_check) -- the same
 * split as present_mode_choice.h and ao_blur_params.h.
 *
 * Why not std::sort: the editor sorted ~15k draws a frame by comparing floats
 * through an index indirection, ~1 ms of a 20 ms frame at 20k entities. Depth
 * is a float, and a float maps to a 32-bit unsigned key whose integer order is
 * its numeric order, so the sort becomes a four-pass LSD radix sort -- linear,
 * branch-free, and stable. Stability is what makes the tie-break exact: after
 * the radix pass, equal keys sit together, and only those runs (almost always
 * of length one) need sorting by material.
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace gws::renderer::gpu {

/// A float as a uint32 whose unsigned order equals the float's numeric order.
/// Negative floats have their bits inverted, non-negative ones their sign bit
/// set. -0.0 is normalised to +0.0 first, because the two compare equal as
/// floats and must therefore tie here too.
inline uint32_t sortable_float_key(float f) {
    if (f == 0.0f) f = 0.0f;
    uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    return (u & 0x80000000u) ? ~u : (u | 0x80000000u);
}

/// Scratch kept between calls so a per-frame sort allocates nothing.
struct DrawOrderScratch {
    std::vector<uint32_t> keys, keys_tmp, idx_tmp;
};

/// Fill `order` with indices 0..n-1 sorted by ascending depth[i], ties broken
/// by ascending tie[i] (the editor passes the material pointer, so equal
/// materials draw back to back). Indices whose depth AND tie are both equal
/// come out in an unspecified order, exactly as with std::sort.
inline void front_to_back_order(const float* depth, const uint64_t* tie, size_t n,
                                std::vector<uint32_t>& order, DrawOrderScratch& s) {
    order.resize(n);
    s.keys.resize(n);
    s.keys_tmp.resize(n);
    s.idx_tmp.resize(n);
    for (size_t i = 0; i < n; ++i) {
        order[i]  = static_cast<uint32_t>(i);
        s.keys[i] = sortable_float_key(depth[i]);
    }

    // LSD radix sort, 8 bits per pass. Each pass is stable.
    uint32_t* k_in  = s.keys.data();
    uint32_t* k_out = s.keys_tmp.data();
    uint32_t* i_in  = order.data();
    uint32_t* i_out = s.idx_tmp.data();
    for (int shift = 0; shift < 32; shift += 8) {
        size_t count[257] = {};
        for (size_t i = 0; i < n; ++i) ++count[((k_in[i] >> shift) & 0xFFu) + 1];
        for (int b = 0; b < 256; ++b) count[b + 1] += count[b];
        for (size_t i = 0; i < n; ++i) {
            const size_t dst = count[(k_in[i] >> shift) & 0xFFu]++;
            k_out[dst] = k_in[i];
            i_out[dst] = i_in[i];
        }
        std::swap(k_in, k_out);
        std::swap(i_in, i_out);
    }
    // Four passes: the result is back in the original buffers (order, keys).

    // Equal depths are adjacent now; order each such run by material.
    for (size_t a = 0; a < n;) {
        size_t b = a + 1;
        while (b < n && s.keys[b] == s.keys[a]) ++b;
        if (b - a > 1)
            std::sort(order.begin() + a, order.begin() + b,
                      [tie](uint32_t x, uint32_t y) { return tie[x] < tie[y]; });
        a = b;
    }
}

}  // namespace gws::renderer::gpu
