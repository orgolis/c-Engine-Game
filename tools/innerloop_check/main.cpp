// ============================================================================
// innerloop_check — measure the inner loop, so it can be defended.
//
// The premise (DEVELOPER_EXPERIENCE.md §1.1): the wait between making a change
// and seeing it is most of what a developer remembers about an engine, and it
// only ever gets worse silently. Unity's guidance for domain reload is to turn
// correctness OFF to win it back; one Unreal team measured ~45 s per iteration
// from a single shader-cache miss. Nobody plans for that — it accretes.
//
// HONEST SCOPE. Three rows — play-mode entry, asset-to-viewport,
// shader-to-screen — still need a driven editor, which this cannot do. They are
// printed as "unmeasured" on every run rather than dropped, because an
// unmeasured budget is exactly where a regression hides.
//
// Editor cold start USED to be in that list. It is measured now, via
// `editor --startup-probe`: the editor initialises, prints its timings and
// exits before the main loop. So the row is real on any machine with a Vulkan
// device and honestly unmeasured on a CI runner, which is better than being
// unmeasured everywhere.
//
// Report-only by default; `--enforce` makes a violation a non-zero exit.
// `--tolerance N` moves the failure line to budget * N — see opt_double below
// for why CI wants that and a developer's machine does not.
// ============================================================================
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using clk = std::chrono::steady_clock;

namespace {

fs::path g_exe_dir;

bool flag(int argc, char** argv, const char* n) {
    for (int i = 1; i < argc; ++i) if (std::strcmp(argv[i], n) == 0) return true;
    return false;
}

// `--tolerance N`: fail only above budget * N. A shared CI runner cannot hold a
// tight wall-clock budget — it is virtualised, noisily co-tenanted, and an order
// slower than a dev box on I/O. Gating at 1x there would fail honest PRs, and a
// gate that cries wolf gets ignored, which is the exact failure this phase
// exists to prevent. So CI gates on ORDER-OF-MAGNITUDE regressions while the
// numbers themselves are published on every run: the gate catches disasters,
// the published trend catches drift.
double opt_double(int argc, char** argv, const char* n, double dflt) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], n) == 0) {
            const double v = std::atof(argv[i + 1]);
            if (v > 0.0) return v;
        }
    return dflt;
}

int run_quiet(const std::string& cmd) {
#ifdef _WIN32
    return std::system(("\"" + cmd + " >nul 2>&1\"").c_str());
#else
    return std::system((cmd + " >/dev/null 2>&1").c_str());
#endif
}

double ms_since(clk::time_point t0) {
    return std::chrono::duration<double, std::milli>(clk::now() - t0).count();
}

struct Metric {
    std::string name;
    double      ms      = -1.0;   // -1 = not measured
    double      budget  = -1.0;   // -1 = tracked, not capped
    std::string note;
};

} // namespace

int main(int argc, char** argv) {
    g_exe_dir = fs::absolute(fs::path(argv[0])).parent_path();
    const bool   json      = flag(argc, argv, "--json");
    const bool   enforce   = flag(argc, argv, "--enforce");
    const double tolerance = opt_double(argc, argv, "--tolerance", 1.0);

    std::vector<Metric> metrics;

    // ---- 1. process startup ------------------------------------------------
    // A proxy for "how long before the engine's tooling responds at all". It
    // bounds every scripted workflow and every agent round-trip.
    {
        const fs::path gws = g_exe_dir / "gws.exe";
        if (fs::exists(gws)) {
            run_quiet("\"" + gws.string() + "\" version");          // warm the cache
            const auto t0 = clk::now();
            for (int i = 0; i < 5; ++i) run_quiet("\"" + gws.string() + "\" version");
            metrics.push_back({"cli_startup", ms_since(t0) / 5.0, 300.0,
                               "average of 5 runs of `gws version`"});
        } else {
            metrics.push_back({"cli_startup", -1.0, 300.0, "gws not built"});
        }
    }

    // ---- 2. simulation throughput -----------------------------------------
    // How fast the gameplay frame actually ticks, headless. This is the closest
    // measurable stand-in for play-mode responsiveness, and unlike the editor
    // rows it needs no GPU.
    {
        const fs::path hl = g_exe_dir / "headless.exe";
        if (fs::exists(hl)) {
            run_quiet("\"" + hl.string() + "\" --demo --frames 60");   // warm
            const auto t0 = clk::now();
            const int rc = run_quiet("\"" + hl.string() + "\" --demo --frames 3600");
            const double total = ms_since(t0);
            if (rc == 0) {
                // 3600 frames = 60 s of simulated time. Report the wall-clock
                // cost of simulating one second, which is the number that tells
                // you whether the sim can keep up with real time.
                metrics.push_back({"sim_1s_of_gameplay", total / 60.0, 16.6,
                                   "wall-clock ms to simulate 1 s of gameplay (3600 frames / 60)"});
            } else {
                metrics.push_back({"sim_1s_of_gameplay", -1.0, 16.6, "headless run failed"});
            }
        } else {
            metrics.push_back({"sim_1s_of_gameplay", -1.0, 16.6, "headless not built"});
        }
    }

    // ---- 3. check-suite latency -------------------------------------------
    // How long the feedback loop takes when a developer runs the tests. If this
    // creeps past a couple of minutes people stop running them locally, and the
    // suite stops being part of the inner loop at all.
    {
        const fs::path gws = g_exe_dir / "gws.exe";
        if (fs::exists(gws)) {
            const auto t0 = clk::now();
            run_quiet("\"" + gws.string() + "\" test --filter world");
            metrics.push_back({"single_check", ms_since(t0), 5000.0,
                               "`gws test --filter world` end to end"});
        }
    }

    // ---- 4. editor cold start ----------------------------------------------
    // Was one of the four "needs a GPU, cannot measure" rows. `editor
    // --startup-probe` initialises, prints its timings as JSON and exits before
    // the main loop, so this row is now REAL wherever a Vulkan device exists —
    // a developer's machine — and honestly unmeasured where one does not, which
    // is every CI runner. Better to measure it somewhere than nowhere.
    {
        const fs::path ed = g_exe_dir / "editor.exe";
        if (fs::exists(ed)) {
            const fs::path out = fs::temp_directory_path() / "gws_innerloop_probe.json";
            std::error_code ec;
            fs::remove(out, ec);
#ifdef _WIN32
            const std::string cmd = "\"\"" + ed.string() + "\" --startup-probe > \"" +
                                    out.string() + "\" 2>nul\"";
#else
            const std::string cmd = "\"" + ed.string() + "\" --startup-probe > \"" +
                                    out.string() + "\" 2>/dev/null";
#endif
            const int rc = std::system(cmd.c_str());
            double ms = -1.0;
            if (rc == 0) {
                std::ifstream f(out);
                std::string j((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
                const auto k = j.find("\"startup_ms\":");
                if (k != std::string::npos) ms = std::atof(j.c_str() + k + 13);
            }
            fs::remove(out, ec);
            if (ms > 0.0)
                metrics.push_back({"editor_cold_start", ms, 5000.0,
                                   "`editor --startup-probe`, init to ready"});
            else
                metrics.push_back({"editor_cold_start", -1.0, 5000.0,
                                   "UNMEASURED — probe failed (no Vulkan device?)"});
        } else {
            metrics.push_back({"editor_cold_start", -1.0, 5000.0, "UNMEASURED — editor not built"});
        }
    }

    // ---- rows this still cannot measure ------------------------------------
    // Named explicitly so the gap is visible in every run.
    metrics.push_back({"play_mode_entry",    -1.0, 1000.0, "UNMEASURED — needs a running editor"});
    metrics.push_back({"asset_to_viewport",  -1.0, 2000.0, "UNMEASURED — needs a running editor"});
    metrics.push_back({"shader_to_screen",   -1.0, 2000.0, "UNMEASURED — needs an editor + GPU"});

    int over = 0, measured = 0, unmeasured = 0, failing = 0;
    for (const auto& m : metrics) {
        if (m.ms < 0.0) { ++unmeasured; continue; }
        ++measured;
        if (m.budget > 0.0 && m.ms > m.budget)             ++over;
        if (m.budget > 0.0 && m.ms > m.budget * tolerance) ++failing;
    }

    if (json) {
        std::printf("{\"measured\":%d,\"unmeasured\":%d,\"over_budget\":%d,"
                    "\"tolerance\":%.2f,\"failing\":%d,\"metrics\":[",
                    measured, unmeasured, over, tolerance, failing);
        for (size_t i = 0; i < metrics.size(); ++i) {
            const auto& m = metrics[i];
            std::printf("%s{\"name\":\"%s\",\"ms\":%.2f,\"budget_ms\":%.2f,\"note\":\"%s\"}",
                        i ? "," : "", m.name.c_str(), m.ms, m.budget, m.note.c_str());
        }
        std::printf("]}\n");
    } else {
        std::printf("=== innerloop_check: the inner loop, measured ===\n\n");
        std::printf("%-22s %10s %10s   %s\n", "metric", "actual", "budget", "note");
        for (const auto& m : metrics) {
            if (m.ms < 0.0) {
                std::printf("%-22s %10s %10.0f   %s\n", m.name.c_str(), "--", m.budget, m.note.c_str());
            } else {
                const bool bad = (m.budget > 0.0 && m.ms > m.budget);
                std::printf("%-22s %9.1fms %9.0fms %s %s\n", m.name.c_str(), m.ms, m.budget,
                            bad ? "OVER " : "ok   ", m.note.c_str());
            }
        }
        std::printf("\n%d measured, %d unmeasured, %d over budget\n", measured, unmeasured, over);
        if (tolerance > 1.0)
            std::printf("Gating at %.1fx budget: %d metric(s) past that line.\n", tolerance, failing);
        if (unmeasured)
            std::printf("\nThe unmeasured rows need a running editor and a GPU. They are listed every run\n"
                        "rather than dropped, because an unmeasured budget is where regressions hide.\n");
        if (!enforce && over)
            std::printf("\nReport-only: pass --enforce to make a budget violation fail. Timing on a shared\n"
                        "runner is noisy, and a gate that cries wolf gets ignored.\n");
    }

    // Never fail on a metric that could not be taken — only on a real overrun,
    // and only when asked to enforce.
    if (enforce && failing > 0) return 1;
    return 0;
}
