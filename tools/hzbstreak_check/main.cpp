// ============================================================================
// hzbstreak_check — the HZB hysteresis table must decide exactly what the
// sorted vector it replaced decided.
//
// The editor drops a draw only after it tested occluded two frames running.
// That state used to live in a vector of (key, streak), std::sort-ed every
// frame and searched with lower_bound; it is now HzbStreakTable (hzb_streak.h).
// This replays many frames of random occlusion through both, with duplicate
// keys (primitives share meshes, so distinct entities can hash alike), key 0,
// draws coming and going, and a table that has to grow because it was
// under-reserved -- and requires the same keep/drop verdict for every draw of
// every frame, and the same streaks.
// ============================================================================
#include "hzb_streak.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <utility>
#include <vector>

using gws::renderer::gpu::HzbStreakTable;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

// The previous implementation, verbatim in behaviour (editor main.cpp before
// this change): returns the keep verdict per draw and updates `table`.
std::vector<bool> reference_frame(const std::vector<uint64_t>& keys,
                                  const std::vector<bool>& visible,
                                  std::vector<std::pair<uint64_t, uint8_t>>& table) {
    std::vector<std::pair<uint64_t, uint8_t>> next;
    std::vector<bool> keep(keys.size());
    const auto by_key = [](const std::pair<uint64_t, uint8_t>& p, uint64_t k) { return p.first < k; };
    for (size_t i = 0; i < keys.size(); ++i) {
        if (visible[i]) { keep[i] = true; continue; }
        const auto sit = std::lower_bound(table.begin(), table.end(), keys[i], by_key);
        const uint8_t prev = (sit != table.end() && sit->first == keys[i]) ? sit->second : 0;
        const uint8_t streak = static_cast<uint8_t>(prev + 1);
        next.emplace_back(keys[i], std::min<uint8_t>(streak, 8));
        keep[i] = streak < 2;
    }
    std::sort(next.begin(), next.end());
    next.erase(std::unique(next.begin(), next.end(),
                           [](const auto& a, const auto& b) { return a.first == b.first; }),
               next.end());
    table.swap(next);
    return keep;
}

// The new implementation, shaped like the editor's: decide against last
// frame's table (read-only), then build this frame's.
std::vector<bool> table_frame(const std::vector<uint64_t>& keys,
                              const std::vector<bool>& visible,
                              HzbStreakTable& prev, HzbStreakTable& next,
                              size_t reserve) {
    std::vector<uint8_t> streak_of(keys.size());
    for (size_t i = 0; i < keys.size(); ++i)
        streak_of[i] = visible[i] ? 0 : static_cast<uint8_t>(prev.get(keys[i]) + 1);
    next.reset(reserve);
    std::vector<bool> keep(keys.size());
    for (size_t i = 0; i < keys.size(); ++i) {
        if (streak_of[i] != 0) next.put(keys[i], std::min<uint8_t>(streak_of[i], 8));
        keep[i] = streak_of[i] < 2;
    }
    std::swap(prev, next);
    return keep;
}

}  // namespace

int main() {
    std::printf("=== hzbstreak_check ===\n\n");

    // Basics.
    {
        HzbStreakTable t;
        check(t.get(42) == 0, "an empty, never-reset table reports no streak");
        t.reset(4);
        t.put(0, 3);
        t.put(7, 1);
        t.put(7, 2);   // overwrite
        check(t.get(0) == 3, "key 0 is an ordinary key (empty slots are marked by streak 0)");
        check(t.get(7) == 2 && t.size() == 2, "put overwrites an existing key");
        t.put(9, 0);
        check(t.get(9) == 0 && t.size() == 2, "a streak of 0 is never stored");
        for (uint64_t k = 100; k < 1100; ++k) t.put(k, 5);   // far past reset(4)
        bool all = t.get(0) == 3 && t.get(7) == 2;
        for (uint64_t k = 100; k < 1100; ++k) all = all && t.get(k) == 5;
        check(all && t.size() == 1002, "an under-reserved table grows and keeps every entry");
        t.reset(4);
        check(t.get(7) == 0 && t.get(500) == 0 && t.size() == 0, "reset empties it");
    }

    // Many frames, against the reference.
    std::mt19937_64 rng(12345);
    for (const size_t reserve_div : {1u, 8u}) {   // 1: reserved properly; 8: forced to grow
        std::vector<std::pair<uint64_t, uint8_t>> ref_table;
        HzbStreakTable prev, next;
        bool same_keep = true, same_streaks = true;
        size_t frames = 0, dropped = 0;
        size_t n = 5000;
        std::vector<uint64_t> keys;
        for (int frame = 0; frame < 200; ++frame, ++frames) {
            // Draws come and go: keep most keys, replace some, and make some
            // collide on purpose (the same key twice in one frame).
            if (keys.empty() || frame % 37 == 0) {
                n = 3000 + rng() % 4000;
                keys.resize(n);
                for (auto& k : keys) k = (rng() % 4 == 0) ? (rng() % 64) : rng();
            } else {
                for (int j = 0; j < 50; ++j) keys[rng() % keys.size()] = rng();
            }
            if (frame % 11 == 0) keys[0] = 0;
            // Occlusion: clustered, so streaks build up and break.
            std::vector<bool> vis(keys.size());
            const uint64_t phase = rng();
            for (size_t i = 0; i < keys.size(); ++i)
                vis[i] = ((keys[i] ^ phase) % 5) < 2 || (rng() % 10 == 0);

            const auto a = reference_frame(keys, vis, ref_table);
            const auto b = table_frame(keys, vis, prev, next, keys.size() / reserve_div);
            if (a != b) same_keep = false;
            for (bool k : a) dropped += k ? 0 : 1;
            if (prev.size() != ref_table.size()) same_streaks = false;
            for (const auto& [k, s] : ref_table)
                if (prev.get(k) != s) { same_streaks = false; break; }
        }
        const std::string tag = reserve_div == 1 ? " (reserved)" : " (under-reserved, grows)";
        check(same_keep, "same keep/drop verdict for every draw of " + std::to_string(frames) + " frames" + tag);
        check(same_streaks, "same streak for every key after every frame" + tag);
        check(dropped > 0, "the replay actually drops draws (" + std::to_string(dropped) + ")" + tag);
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL hzbstreak_check\n");
    return g_fail ? 1 : 0;
}
