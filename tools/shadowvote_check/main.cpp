// ============================================================================
// shadowvote_check — the warp vote in the soft-shadow estimator must never
// cost a ray, must leave split warps exactly as they were, and must only
// reach GPUs that can run it.
//
// rayQueryShadow() (tools/lighting_pass.frag) traces each pixel's probe half
// of the cone, four rays at the High preset, before deciding whether the pixel
// is in a penumbra. Most pixels are not, so most of those rays only confirmed
// what the first one said. The vote: each pixel traces ONE ray of its probe
// half, the warp compares answers (subgroupAllEqual), and a unanimous warp of
// at least 16 pixels returns that answer -- one ray instead of four. With
// shadows that work (F15), Lighting fell 6.7-8.2 -> 2.5-2.7 ms on MyGame and
// 7.6-10.1 -> 4.1-4.2 ms at 20k entities, with 0.015% of pixels moving by 4+
// levels on a scene built to show penumbras (PERFORMANCE_AUDIT.md F14 (r)).
//
// Two properties no screenshot can show, so they are modelled here:
//   1. NEVER MORE RAYS. The probe must be a member of the probe half and be
//      reused by it. An odd probe index would add a ray to every pixel of
//      every split warp -- a pure regression, invisible in the image.
//   2. SPLIT WARPS ARE EXACT. A warp that does not agree must produce exactly
//      the old estimator's result for every pixel.
// And one about where it may run: subgroup vote/ballot in the fragment stage
// is optional in Vulkan, and SPIR-V declaring an unsupported capability is
// invalid usage a driver may answer by crashing (ray query did, on an RX
// 5700). So the vote lives in its own variant, selected by a device query --
// pinned below, with the shader lines the model mirrors.
// ============================================================================
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

// One pixel's eight cone rays: ray i is lit (1) or blocked (0).
using Rays = std::vector<int>;

struct Result { float value; int traced; };

// The estimator WITHOUT the vote (F13's split): the even half first; if it is
// unanimous, return it; otherwise trace the odd half and average all N.
Result estimate_old(const Rays& r) {
    const int n = static_cast<int>(r.size());
    int lit = 0, probes = 0, traced = 0;
    for (int i = 0; i < n; i += 2) { lit += r[i]; ++probes; ++traced; }
    if (n >= 4 && (lit == 0 || lit == probes)) return {float(lit) / float(probes), traced};
    for (int i = 1; i < n; i += 2) { lit += r[i]; ++traced; }
    return {float(lit) / float(n), traced};
}

// The pixel's probe index, as the shader picks it: an EVEN index, one of the
// probe half's members, chosen per pixel (`h` stands in for the hash).
int probe_index(int n, double h) {
    const int half_count = (n + 1) / 2;
    return 2 * std::min(int(h * double(half_count)), half_count - 1);
}

// The estimator WITH the vote, for a whole warp.
std::vector<Result> estimate_vote(const std::vector<Rays>& warp, const std::vector<double>& hashes) {
    const int n = static_cast<int>(warp[0].size());
    std::vector<int> k(warp.size());
    std::vector<int> probe(warp.size());
    for (size_t p = 0; p < warp.size(); ++p) {
        k[p] = probe_index(n, hashes[p]);
        probe[p] = warp[p][k[p]];
    }
    const bool all_equal = std::all_of(probe.begin(), probe.end(), [&](int v) { return v == probe[0]; });
    std::vector<Result> out;
    if (n >= 4 && warp.size() >= 16 && all_equal) {
        for (size_t p = 0; p < warp.size(); ++p) out.push_back({float(probe[p]), 1});
        return out;
    }
    for (size_t p = 0; p < warp.size(); ++p) {
        int lit = probe[p], probes = 1, traced = 1;
        for (int i = 0; i < n; i += 2) {
            if (i == k[p]) continue;
            lit += warp[p][i]; ++probes; ++traced;
        }
        if (n >= 4 && (lit == 0 || lit == probes)) { out.push_back({float(lit) / float(probes), traced}); continue; }
        for (int i = 1; i < n; i += 2) { lit += warp[p][i]; ++traced; }
        out.push_back({float(lit) / float(n), traced});
    }
    return out;
}

std::string read_file(const fs::path& p) {
    std::ifstream f(p);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

fs::path find_repo_root(const fs::path& exe_dir) {
    std::error_code ec;
    for (fs::path d = exe_dir; !d.empty(); d = d.parent_path()) {
        if (fs::exists(d / "tools" / "lighting_pass.frag", ec)) return d;
        if (d == d.parent_path()) break;
    }
    return {};
}

}  // namespace

int main(int, char** argv) {
    std::printf("=== shadowvote_check ===\n\n");

    std::printf("The model, over random warps (every ray count, solid, penumbra and mixed):\n");
    {
        std::mt19937 rng(1234);
        std::uniform_real_distribution<double> u(0.0, 1.0);
        int warps = 0, unanimous = 0, never_more = 0, split_exact = 0, split_warps = 0;
        bool probes_even = true;
        for (int n = 1; n <= 8; ++n) {
            for (int trial = 0; trial < 4000; ++trial) {
                const int size = (trial % 5 == 0) ? 12 : 32;          // some warps below the 16 floor
                const double occl = (trial % 3 == 0) ? 0.0 : (trial % 3 == 1) ? 1.0 : u(rng);
                std::vector<Rays> warp(size, Rays(n));
                std::vector<double> hashes(size);
                for (int p = 0; p < size; ++p) {
                    hashes[p] = u(rng) * 0.999999;
                    for (int i = 0; i < n; ++i) warp[p][i] = u(rng) < occl ? 0 : 1;
                    probes_even = probes_even && probe_index(n, hashes[p]) % 2 == 0 &&
                                  probe_index(n, hashes[p]) < n;
                }
                const std::vector<Result> v = estimate_vote(warp, hashes);
                int old_rays = 0, new_rays = 0;
                bool exact = true;
                for (int p = 0; p < size; ++p) {
                    const Result o = estimate_old(warp[p]);
                    old_rays += o.traced;
                    new_rays += v[p].traced;
                    exact = exact && o.value == v[p].value;
                }
                const bool voted = std::all_of(v.begin(), v.end(), [](const Result& r) { return r.traced == 1; }) &&
                                   n >= 4 && size >= 16;
                ++warps;
                if (voted) ++unanimous;
                if (new_rays <= old_rays) ++never_more;
                if (!voted) { ++split_warps; if (exact) ++split_exact; }
            }
        }
        check(probes_even, "every probe is an EVEN index inside the cone (a member of the probe half)");
        char buf[200];
        std::snprintf(buf, sizeof buf, "never more rays than without the vote: %d of %d warps", never_more, warps);
        check(never_more == warps, buf);
        std::snprintf(buf, sizeof buf, "split warps are EXACT, pixel for pixel: %d of %d", split_exact, split_warps);
        check(split_exact == split_warps, buf);
        std::snprintf(buf, sizeof buf, "the vote actually fires on solid warps (%d unanimous)", unanimous);
        check(unanimous > 0, buf);
    }
    {
        // Below 16 voters, or below four rays, the vote never short-cuts.
        std::vector<Rays> warp(12, Rays(8, 1));
        std::vector<double> h(12, 0.3);
        const auto v = estimate_vote(warp, h);
        check(v[0].traced > 1, "a warp of 12 active pixels does not vote (too few to call it solid)");
        std::vector<Rays> warp3(32, Rays(3, 1));
        std::vector<double> h3(32, 0.3);
        check(estimate_vote(warp3, h3)[0].traced > 1, "three rays per pixel: no vote (as before, no shortcut)");
    }

    std::printf("\nThe source the model mirrors, and the gating:\n");
    const fs::path root = find_repo_root(fs::absolute(fs::path(argv[0])).parent_path());
    if (root.empty()) {
        std::printf("  SKIP needs a source checkout\n");
    } else {
        const std::string frag = read_file(root / "tools" / "lighting_pass.frag");
        const std::string regen = read_file(root / "tools" / "regen_lighting_spirv.sh");
        const std::string pass = read_file(root / "engine" / "renderer" / "gpu" / "vulkan" / "vulkan_lighting_pass.cpp");
        const std::string dev = read_file(root / "engine" / "renderer" / "gpu" / "vulkan" / "vulkan_device.cpp");
        check(frag.find("k = 2 * min(int(aoHash(gl_FragCoord.yx + vec2(41.0, 289.0)) * float(half_count)), half_count - 1);")
                  != std::string::npos, "the shader's probe index is even and inside the probe half");
        check(frag.find("if (i == k) continue;") != std::string::npos, "the probe half reuses the probe");
        check(frag.find("subgroupBallotBitCount(subgroupBallot(true)) >= 16u && subgroupAllEqual(probe)")
                  != std::string::npos, "a warp votes only with 16+ active pixels, all agreeing");
        const size_t vote_if = frag.find("#ifdef GWS_SUBGROUP_VOTE\n    int k = -1;");
        const size_t vote_else = frag.find("#else", vote_if);
        check(vote_if != std::string::npos && vote_else != std::string::npos,
              "the vote is compiled only under GWS_SUBGROUP_VOTE, with the plain probe loop otherwise");
        check(regen.find("-DGWS_RAY_QUERY -DGWS_SUBGROUP_VOTE -S frag -o lighting_pass.frag.spv") != std::string::npos &&
              regen.find("-DGWS_RAY_QUERY                     -S frag -o lighting_pass_rt_novote.frag.spv") != std::string::npos,
              "regen builds the vote variant AND a ray-query variant without it");
        check(pass.find("const bool vote = rt && device_->has_fragment_subgroup_vote();") != std::string::npos &&
              pass.find("kLightingPassFragRtNoVoteSpv") != std::string::npos,
              "the lighting pass picks the vote variant only where the device supports it");
        check(dev.find("(sg.supportedStages & VK_SHADER_STAGE_FRAGMENT_BIT) != 0") != std::string::npos &&
              dev.find("VK_SUBGROUP_FEATURE_VOTE_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT") != std::string::npos,
              "the device query requires vote AND ballot in the FRAGMENT stage");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail)
        std::printf("FAIL shadowvote_check — the shadow vote can cost rays, change split warps, or reach a GPU\n"
                    "     that cannot run it.\n");
    return g_fail ? 1 : 0;
}
