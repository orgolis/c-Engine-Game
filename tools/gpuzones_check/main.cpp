// ====================
// gpuzones_check — the GPU-zone bookkeeping (performance audit item F2)
// ====================
//
// The bug this whole item exists to fix was not a wrong number, it was a
// CONFIDENT number that omitted the expensive work: the profiler timed five
// render-graph stages, the effect passes ran between them and were timed by
// nothing, and the reported total was the sum of only what it had been handed.
//
// So the assertions here are all about the ways a timestamp system reports
// something plausible and wrong:
//
//   * a name whose slot MOVES between frames reads another zone's timestamps;
//   * a full table that WRAPS aliases two zones onto one slot;
//   * a zone that stops running keeps reporting its last duration forever,
//     so the overlay shows time spent on work that is no longer happening.
//
// Each of those produces a believable overlay. None of them produces an error.

#include "gpu_zones.h"
#include "gpu_profiler.h"

#include <cstdint>
#include <cstdio>
#include <string>

using engine::vulkan::ZoneRegistry;
using engine::vulkan::GPUProfiler;

static int g_failures = 0;
static void check(const char* name, bool ok) {
    std::printf("  [%s] %s\n", ok ? "OK  " : "FAIL", name);
    if (!ok) ++g_failures;
}

int main() {
    std::printf("gpuzones_check — GPU zone bookkeeping (F2)\n");

    std::printf("\n[group] slots are stable, which is the whole contract\n");
    {
        ZoneRegistry r;
        const uint32_t a = r.index_of("Geometry");
        const uint32_t b = r.index_of("Lighting");
        const uint32_t c = r.index_of("SSR");

        check("distinct names get distinct slots", a != b && b != c && a != c);
        check("none of them is kInvalid", a != ZoneRegistry::kInvalid &&
                                          b != ZoneRegistry::kInvalid &&
                                          c != ZoneRegistry::kInvalid);
        check("re-asking gives the SAME slot", r.index_of("Geometry") == a &&
                                               r.index_of("SSR") == c);
        check("count reflects registrations", r.count() == 3);
        check("names round-trip", r.name_at(a) == "Geometry" && r.name_at(c) == "SSR");

        // Registration order must not matter across frames: a frame where SSR
        // is dispatched before Geometry must not renumber either of them.
        const uint32_t c2 = r.index_of("SSR");
        const uint32_t a2 = r.index_of("Geometry");
        check("a different call order does not renumber", c2 == c && a2 == a);

        check("find() does not register", r.find("Nope") == ZoneRegistry::kInvalid &&
                                          r.count() == 3);
        check("find() locates a known name", r.find("Lighting") == b);
    }

    std::printf("\n[group] a full table refuses rather than aliases\n");
    {
        ZoneRegistry r;
        for (uint32_t i = 0; i < ZoneRegistry::kMaxZones; ++i)
            r.index_of("zone_" + std::to_string(i));
        check("fills to capacity", r.count() == ZoneRegistry::kMaxZones && r.full());

        // Wrapping would put two zones on one slot and report one of them as
        // the other -- a confident wrong number, which is the exact failure
        // mode this system was built to remove.
        check("one past capacity is kInvalid, not a wrap",
              r.index_of("one_too_many") == ZoneRegistry::kInvalid);
        check("and nothing was added", r.count() == ZoneRegistry::kMaxZones);
        check("existing names still resolve after overflow",
              r.index_of("zone_0") == 0 && r.index_of("zone_7") == 7);
    }

    std::printf("\n[group] a pass that stops running reports nothing, not stale\n");
    {
        ZoneRegistry r;
        const uint32_t geo = r.index_of("Geometry");
        const uint32_t ssr = r.index_of("SSR");

        r.begin_frame();
        r.mark_ran(geo);
        r.mark_ran(ssr);
        check("both ran in frame 1", r.ran(geo) && r.ran(ssr));
        check("ran_count is 2", r.ran_count() == 2);

        // Frame 2: the user switched SSR off.
        r.begin_frame();
        r.mark_ran(geo);
        check("geometry still ran", r.ran(geo));
        check("SSR is NOT still marked from last frame", !r.ran(ssr));
        check("ran_count follows", r.ran_count() == 1);

        // And its slot is unchanged, so switching it back on lines up again.
        check("the disabled zone keeps its slot", r.index_of("SSR") == ssr);
        r.begin_frame();
        r.mark_ran(ssr);
        check("re-enabling reports it again", r.ran(ssr) && !r.ran(geo));
    }

    std::printf("\n[group] guards\n");
    {
        ZoneRegistry r;
        const uint32_t z = r.index_of("Only");
        r.begin_frame();
        // Out-of-range marks must not corrupt anything or crash.
        r.mark_ran(ZoneRegistry::kInvalid);
        r.mark_ran(9999);
        check("an out-of-range mark is ignored", r.ran_count() == 0);
        check("querying out of range is false", !r.ran(9999) && !r.ran(ZoneRegistry::kInvalid));

        r.mark_ran(z);
        check("a valid mark still works", r.ran(z) && r.ran_count() == 1);

        // Marking twice in one frame is not an error -- a pass may legitimately
        // be dispatched more than once -- but it counts once.
        r.mark_ran(z);
        check("marking twice counts once", r.ran_count() == 1);

        ZoneRegistry empty;
        empty.begin_frame();
        check("an empty registry has nothing to report", empty.ran_count() == 0 &&
                                                         empty.count() == 0);
        check("an empty name is still a name", empty.index_of("") == 0 && empty.count() == 1);
    }


    std::printf("\n[group] the profiler must not zero what nobody reported this frame\n");
    {
        // REGRESSION (v0.8.2, reported from the field as "all gpu times show
        // zeros"). The additive API reset the accumulator and zeroed every
        // unreported pass EVERY frame -- but the producers only report
        // INTERMITTENTLY: the render graph's resolve_timings() returns false
        // until a ring result is ready, and GpuZones::end_frame() returns early
        // for the same reason. So on any frame where neither had data, the whole
        // panel went to zero, and the GPU cost appeared to move into the CPU's
        // fence wait because that was the only number left.
        auto& p = GPUProfiler::instance();

        p.begin_gpu_frame();
        p.submit_partial({{"Geometry", 2.0f}, {"Lighting", 8.0f}});
        p.submit_partial({{"SSR", 3.0f}});
        p.end_gpu_frame();
        check("a reported pass shows its value",
              p.get_pass_timing("Lighting") && p.get_pass_timing("Lighting")->duration_ms == 8.0f);
        check("total is the sum of every producer", p.get_total_gpu_time_ms() == 13.0f);

        // The frame that broke it: nobody had a result ready.
        p.begin_gpu_frame();
        p.end_gpu_frame();
        check("an unreported pass KEEPS its last value, it does not blank",
              p.get_pass_timing("Lighting") && p.get_pass_timing("Lighting")->duration_ms == 8.0f);
        check("and the total survives too", p.get_total_gpu_time_ms() == 13.0f);

        // A pass that genuinely stopped must still reach zero -- but reported as
        // zero BY ITS PRODUCER, which is the only thing that knows.
        p.begin_gpu_frame();
        p.submit_partial({{"Geometry", 2.0f}, {"Lighting", 8.0f}});
        p.submit_partial({{"SSR", 0.0f}});
        p.end_gpu_frame();
        check("a producer reporting 0 does zero it",
              p.get_pass_timing("SSR") && p.get_pass_timing("SSR")->duration_ms == 0.0f);
        check("total follows", p.get_total_gpu_time_ms() == 10.0f);

        // One producer reporting must not blank the other's passes.
        p.begin_gpu_frame();
        p.submit_partial({{"SSR", 5.0f}});
        p.end_gpu_frame();
        check("the other producer's passes survive a partial frame",
              p.get_pass_timing("Lighting") && p.get_pass_timing("Lighting")->duration_ms == 8.0f);
        check("total counts everything on display", p.get_total_gpu_time_ms() == 15.0f);
    }

    std::printf("\n%s — %d failure(s)\n", g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
