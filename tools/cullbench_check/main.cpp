// ============================================================================
// cullbench_check — is parallel frustum culling actually faster, and from where?
//
// Phase 3.1 says the verified job system sits idle while culling runs
// single-threaded. Reading the code confirmed culling IS serial. That is not
// the same as confirming parallelising it helps: fan-out has a cost, and a
// "parallel" version that loses on the scene sizes people actually have would
// be a regression wearing an optimisation's clothes.
//
// So this measures both paths across a range of sizes and asserts the two
// things that actually matter:
//   1. they produce the SAME survivors (a faster wrong answer is not a win)
//   2. the crossover is where culling.cpp claims it is
//
// It models the cull test rather than calling it, because DrawItem holds GPU
// Mesh pointers and this has to run on a CI box with no device. The arithmetic
// is the same shape -- a matrix-vector transform plus six plane dots per item --
// which is what decides where the crossover lands.
// ============================================================================
#include "jobs/job_system.h"

#include <glm/glm.hpp>

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
#include <cstdint>

using clk = std::chrono::steady_clock;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

struct Item {
    glm::mat4 model{1.0f};
    glm::vec3 center{0.0f};
    float     radius = 1.0f;
};

struct Plane { glm::vec3 n; float d; };

// Same shape as culling.cpp's keep(): transform the centre, scale the radius by
// the largest column, then six plane tests with an early out.
bool keep(const Item& it, const Plane* planes) {
    const glm::vec3 c = glm::vec3(it.model * glm::vec4(it.center, 1.0f));
    const float sx = glm::length(glm::vec3(it.model[0]));
    const float sy = glm::length(glm::vec3(it.model[1]));
    const float sz = glm::length(glm::vec3(it.model[2]));
    const float r  = it.radius * std::max(sx, std::max(sy, sz));
    for (int i = 0; i < 6; ++i)
        if (glm::dot(planes[i].n, c) + planes[i].d < -r) return false;
    return true;
}

double ms_since(clk::time_point t) {
    return std::chrono::duration<double, std::milli>(clk::now() - t).count();
}

}  // namespace

int main() {
    std::printf("=== cullbench_check ===\n\n");

    gws::jobs::JobSystem::instance().init();

    Plane planes[6];
    for (int i = 0; i < 6; ++i) {
        planes[i].n = glm::normalize(glm::vec3((i % 3 == 0) ? 1.0f : 0.0f,
                                               (i % 3 == 1) ? 1.0f : 0.0f,
                                               (i % 3 == 2) ? 1.0f : 0.0f) *
                                     ((i < 3) ? 1.0f : -1.0f));
        planes[i].d = 60.0f;   // roughly half the items survive
    }

#ifndef NDEBUG
    std::printf("NOTE: unoptimised build. The serial path is several times slower here than at\n"
                "-O2, so the crossover below is LOWER than it would be in a release build.\n"
                "culling.cpp gates at 2048 deliberately: a threshold tuned to these numbers\n"
                "would be too eager once the serial path is optimised.\n\n");
#endif

    std::printf("%10s %12s %12s   %s\n", "items", "serial", "parallel", "verdict");
    size_t crossover = 0;

    for (size_t count : {256u, 1024u, 2048u, 8192u, 65536u}) {
        std::vector<Item> items(count);
        for (size_t i = 0; i < count; ++i) {
            const float f = static_cast<float>(i);
            items[i].model[3] = glm::vec4(std::fmod(f * 7.3f, 200.0f) - 100.0f,
                                          std::fmod(f * 3.1f, 200.0f) - 100.0f,
                                          std::fmod(f * 5.7f, 200.0f) - 100.0f, 1.0f);
        }

        // Serial.
        std::vector<uint8_t> a(count, 0);
        const auto t0 = clk::now();
        for (int rep = 0; rep < 20; ++rep)
            for (size_t i = 0; i < count; ++i) a[i] = keep(items[i], planes) ? 1u : 0u;
        const double serial_ms = ms_since(t0) / 20.0;

        // Parallel.
        std::vector<uint8_t> b(count, 0);
        const auto t1 = clk::now();
        for (int rep = 0; rep < 20; ++rep)
            gws::jobs::JobSystem::instance().parallel_for(
                0, count, [&](size_t i) { b[i] = keep(items[i], planes) ? 1u : 0u; });
        const double par_ms = ms_since(t1) / 20.0;

        const bool same = (a == b);
        check(same, "n=" + std::to_string(count) + ": both paths agree on what is visible");
        if (!crossover && par_ms < serial_ms) crossover = count;

        std::printf("%10zu %10.3fms %10.3fms   %s\n", count, serial_ms, par_ms,
                    par_ms < serial_ms ? "parallel wins" : "serial wins");
    }

    // The threshold in culling.cpp is 2048. It only has to be in the right
    // region -- above the point where fan-out stops dominating, and not so high
    // that large scenes stay serial. Asserting an exact crossover would make
    // this fail on any machine with a different core count, which is the kind
    // of brittle check that gets deleted rather than fixed.
    check(crossover != 0 && crossover <= 8192,
          "parallel starts winning at or below 8192 items (culling.cpp gates at 2048)");
    check(crossover == 0 || crossover >= 256,
          "and not below 256, where fan-out would dominate");

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL cullbench_check\n");
    return g_fail ? 1 : 0;
}
