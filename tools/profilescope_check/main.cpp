// ============================================================================
// profilescope_check — a profiler zone must be the first statement in its own
// block, or it measures the wrong thing and says so confidently.
//
// GWS_PROFILE_ZONE declares a local whose LIFETIME is the measurement. Write
// it as a bare statement and it does not measure the call beneath it — it
// measures everything from there to the end of the enclosing block. In the
// editor's frame loop that enclosing block is the entire frame.
//
// Four zones were written that way and every one of them reported a plausible,
// wrong number:
//
//   acquire_swapchain_image   11.637 ms   (of a 13.1 ms frame)
//   stage_shadow              10.515 ms   -> 0.363 ms once scoped
//   stage_transparent          9.145 ms   -> 0.125 ms
//   queue_submit               8.778 ms   -> 0.184 ms
//
// Nothing errors, nothing crashes, and the overlay looks reasonable: the
// zones are simply nested without anyone saying so, each one running to the
// same end point, so they come out sorted in a tidy descending sequence that
// reads exactly like a real profile. That cost a whole session of chasing a
// "mysterious" frame cost that turned out to be a 75 Hz monitor.
//
// This is a source-text check. It cannot be a runtime one: the defect is
// lexical scope, which is gone by the time anything runs.
// ============================================================================
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

std::string strip(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}

// A line that opens no scope of its own and is not a comment or blank.
bool is_code(const std::string& t) {
    return !t.empty() && t.rfind("//", 0) != 0 && t.rfind("*", 0) != 0 &&
           t.rfind("/*", 0) != 0;
}

fs::path find_repo_root(const fs::path& exe_dir) {
    std::error_code ec;
    for (fs::path d = exe_dir; !d.empty(); d = d.parent_path()) {
        if (fs::is_directory(d / "editor" / "src", ec) &&
            fs::is_directory(d / "tools", ec))
            return d;
        if (d == d.parent_path()) break;
    }
    return {};
}

// The rule: on the zone's own line there is a '{' before it, OR the previous
// code line ends with '{'. Either way the zone is first in a fresh block and
// its lifetime is that block.
bool properly_scoped(const std::vector<std::string>& lines, size_t i) {
    const std::string t = strip(lines[i]);
    const size_t macro = t.find("GWS_PROFILE_ZONE");
    if (macro != std::string::npos && t.substr(0, macro).find('{') != std::string::npos)
        return true;
    for (size_t k = i; k-- > 0;) {
        const std::string prev = strip(lines[k]);
        if (!is_code(prev)) continue;
        return !prev.empty() && prev.back() == '{';
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    std::printf("=== profilescope_check ===\n\n");
    (void)argc;

    const fs::path root = find_repo_root(fs::absolute(fs::path(argv[0])).parent_path());
    if (root.empty()) {
        std::printf("SKIP profilescope_check: needs a source checkout\n");
        return 0;
    }

    // Every file that uses the macro. Today that is one; the scan is written
    // over a list so adding instrumentation elsewhere is covered automatically.
    const char* sources[] = { "editor/src/main.cpp" };

    for (const char* rel : sources) {
        const fs::path p = root / rel;
        std::ifstream in(p);
        if (!in) { check(false, std::string(rel) + ": cannot be read"); continue; }

        std::vector<std::string> lines;
        for (std::string l; std::getline(in, l); ) lines.push_back(l);

        std::vector<std::string> bad;
        int seen = 0;
        for (size_t i = 0; i < lines.size(); ++i) {
            const std::string t = strip(lines[i]);
            if (t.find("GWS_PROFILE_ZONE(") == std::string::npos) continue;
            if (t.rfind("//", 0) == 0) continue;            // a mention in prose
            if (t.find("#  define") != std::string::npos) continue;
            ++seen;
            if (!properly_scoped(lines, i))
                bad.push_back(std::string(rel) + ":" + std::to_string(i + 1) + "  " + t);
        }

        char head[160];
        std::snprintf(head, sizeof head,
                      "%s: all %d profiler zones open their own block", rel, seen);
        if (bad.empty()) {
            check(true, head);
        } else {
            std::string msg = head;
            msg += "\n       these run to the end of the enclosing block instead:";
            for (const auto& b : bad) msg += "\n         " + b;
            msg += "\n       wrap each as { GWS_PROFILE_ZONE(\"name\"); ... }";
            check(false, msg);
        }

        // A scan that silently matched nothing would pass forever.
        char cnt[96];
        std::snprintf(cnt, sizeof cnt, "%s: the scan found zones to check (%d)", rel, seen);
        check(seen > 0, cnt);
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail)
        std::printf("FAIL profilescope_check — a profiler zone measures more than it names.\n");
    return g_fail ? 1 : 0;
}
