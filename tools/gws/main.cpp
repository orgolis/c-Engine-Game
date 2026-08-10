// ============================================================================
// gws — the engine command line.
//
// One entry point to everything the engine can do without a GUI. This exists
// because three separate consumers all needed the same thing and none of them
// can drive an editor window:
//
//   * CI, which until now had to hand-roll a shell loop over the check binaries
//   * automation and scripts
//   * external AI agents (Phase 1 of PRODUCT_AND_ECOSYSTEM.md) — an agent that
//     can run the game, read the failure and try again is worth far more than
//     one that autocompletes, and this is the interface that makes it possible
//
// Design rules:
//   * every subcommand supports --json, because a machine is a first-class
//     caller here, not an afterthought
//   * exit code is the contract: 0 = success, non-zero = failure, always
//   * no subcommand lies about what it did — anything unimplemented says so and
//     exits non-zero rather than silently succeeding
// ============================================================================
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <string>
#include <vector>

#ifndef GWS_ENGINE_VERSION
#define GWS_ENGINE_VERSION "unknown"
#endif

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------- utilities

fs::path g_exe_dir;   // directory holding gws.exe — sibling tools live here too

std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    o += buf;
                } else {
                    o += c;
                }
        }
    }
    return o;
}

// Run a command, capture its combined output, return its exit code.
// Output is captured rather than inherited so that --json stays parseable.
int run_capture(const std::string& cmd, std::string& out) {
    out.clear();
    // `cmd` must arrive with its own internal quoting already correct.
    // 2>&1 so a tool that reports failures on stderr is not silently lost.
    std::string full = cmd + " 2>&1";
#ifdef _WIN32
    // cmd.exe strips the OUTERMOST pair of quotes from the command line. With a
    // quoted program path and a quoted argument, that strip lands on the wrong
    // pair and a path containing a space ("ProjectSchizo - Copy") breaks apart.
    // Wrapping the whole line in one more pair is the documented remedy.
    full = "\"" + full + "\"";
#endif
#ifdef _WIN32
    FILE* p = _popen(full.c_str(), "r");
#else
    FILE* p = popen(full.c_str(), "r");
#endif
    if (!p) return 127;
    char buf[4096];
    while (std::fgets(buf, sizeof buf, p)) out += buf;
#ifdef _WIN32
    const int rc = _pclose(p);
#else
    const int rc = pclose(p);
#endif
    return rc;
}

std::string last_line(const std::string& s) {
    if (s.empty()) return {};
    size_t end = s.find_last_not_of("\r\n");
    if (end == std::string::npos) return {};
    size_t begin = s.find_last_of('\n', end);
    return s.substr(begin == std::string::npos ? 0 : begin + 1,
                    end - (begin == std::string::npos ? 0 : begin));
}

bool has_flag(int argc, char** argv, const char* flag) {
    for (int i = 0; i < argc; ++i)
        if (std::strcmp(argv[i], flag) == 0) return true;
    return false;
}

const char* opt_value(int argc, char** argv, const char* name) {
    for (int i = 0; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
    return nullptr;
}

// ------------------------------------------------------------------- version

int cmd_version(bool json) {
    if (json) std::printf("{\"engine\":\"%s\"}\n", GWS_ENGINE_VERSION);
    else      std::printf("GameWorldshaper engine %s\n", GWS_ENGINE_VERSION);
    return 0;
}

// ---------------------------------------------------------------------- test

// The GPU-requiring checks. They create a Vulkan instance and enumerate
// physical devices, so on a machine with no GPU or ICD they fail regardless of
// the engine's health. `test` skips them there rather than reporting a false
// failure — the same policy CI applies, now owned by the tool instead of by a
// copy of the list living in a YAML file.
bool needs_gpu(const std::string& name) {
    return name == "texture_check" || name == "model_check" || name == "skinning_check";
}

int cmd_test(int argc, char** argv) {
    const bool json     = has_flag(argc, argv, "--json");
    const bool list     = has_flag(argc, argv, "--list");
    const bool with_gpu = has_flag(argc, argv, "--gpu");
    const char* filter  = opt_value(argc, argv, "--filter");

    std::vector<std::string> checks;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(g_exe_dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::string stem = e.path().stem().string();
        if (stem.size() > 6 && stem.rfind("_check") == stem.size() - 6) {
            if (filter && stem.find(filter) == std::string::npos) continue;
            checks.push_back(stem);
        }
    }
    std::sort(checks.begin(), checks.end());

    if (checks.empty()) {
        std::fprintf(stderr, "gws test: no *_check binaries found next to %s\n",
                     g_exe_dir.string().c_str());
        return 1;
    }

    if (list) {
        if (json) {
            std::printf("{\"checks\":[");
            for (size_t i = 0; i < checks.size(); ++i)
                std::printf("%s\"%s\"", i ? "," : "", checks[i].c_str());
            std::printf("]}\n");
        } else {
            for (const auto& c : checks) std::printf("%s\n", c.c_str());
        }
        return 0;
    }

    struct Result { std::string name, summary; int rc = 0; bool skipped = false; };
    std::vector<Result> results;
    int pass = 0, fail = 0, skip = 0;

    for (const auto& name : checks) {
        Result r; r.name = name;
        if (needs_gpu(name) && !with_gpu) {
            r.skipped = true; r.summary = "needs a Vulkan device (pass --gpu to run anyway)";
            ++skip;
        } else {
            const fs::path exe = g_exe_dir / (name + ".exe");
            std::string out;
            r.rc = run_capture("\"" + exe.string() + "\"", out);
            r.summary = last_line(out);
            if (r.rc == 0) ++pass; else ++fail;
            if (!json) {
                std::printf("%-6s %-22s %s\n", r.rc == 0 ? "PASS" : "FAIL",
                            name.c_str(), r.summary.c_str());
                if (r.rc != 0) std::printf("%s\n", out.c_str());
            }
        }
        if (!json && r.skipped)
            std::printf("%-6s %-22s %s\n", "SKIP", name.c_str(), r.summary.c_str());
        results.push_back(std::move(r));
    }

    if (json) {
        std::printf("{\"passed\":%d,\"failed\":%d,\"skipped\":%d,\"checks\":[", pass, fail, skip);
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            std::printf("%s{\"name\":\"%s\",\"status\":\"%s\",\"exit\":%d,\"summary\":\"%s\"}",
                        i ? "," : "", r.name.c_str(),
                        r.skipped ? "skipped" : (r.rc == 0 ? "passed" : "failed"),
                        r.rc, json_escape(r.summary).c_str());
        }
        std::printf("]}\n");
    } else {
        std::printf("\n%d passed, %d failed, %d skipped\n", pass, fail, skip);
    }
    return fail == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------- cook

int cmd_cook(int argc, char** argv) {
    const bool json = has_flag(argc, argv, "--json");
    const std::string src = argc > 2 && argv[2][0] != '-' ? argv[2] : "assets/models";
    const std::string out = argc > 3 && argv[3][0] != '-' ? argv[3] : "cooked";

    const fs::path cooker = g_exe_dir / "assetcook.exe";
    if (!fs::exists(cooker)) {
        std::fprintf(stderr, "gws cook: assetcook not found next to gws (build it first)\n");
        return 1;
    }
    std::string output;
    const int rc = run_capture("\"" + cooker.string() + "\" \"" + src + "\" \"" + out + "\"", output);
    if (json) {
        std::printf("{\"ok\":%s,\"exit\":%d,\"src\":\"%s\",\"out\":\"%s\",\"summary\":\"%s\"}\n",
                    rc == 0 ? "true" : "false", rc,
                    json_escape(src).c_str(), json_escape(out).c_str(),
                    json_escape(last_line(output)).c_str());
    } else {
        std::printf("%s", output.c_str());
    }
    return rc == 0 ? 0 : 1;
}

// ------------------------------------------------------------------ validate

// Cheap integrity pass over a project: the manifest parses, and the files it
// points at exist. Deliberately does NOT load the engine — this must stay fast
// enough to run on every save, and usable from CI where there is no GPU.
int cmd_validate(int argc, char** argv) {
    const bool json = has_flag(argc, argv, "--json");
    const std::string dir = argc > 2 && argv[2][0] != '-' ? argv[2] : ".";

    std::vector<std::string> problems;
    const fs::path manifest = fs::path(dir) / "project.schizo";
    if (!fs::exists(manifest)) {
        problems.push_back("no project.schizo in " + dir);
    } else {
        // The manifest is line-oriented text; read the default scene out of it
        // without pulling in the editor's project library.
        FILE* f = std::fopen(manifest.string().c_str(), "rb");
        std::string body;
        if (f) {
            char buf[4096]; size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) body.append(buf, n);
            std::fclose(f);
        }
        const std::string key = "default_scene";
        const size_t k = body.find(key);
        if (k != std::string::npos) {
            size_t s = body.find_first_of("=:", k);
            size_t e = body.find_first_of("\r\n", k);
            if (s != std::string::npos && e != std::string::npos && s < e) {
                std::string scene = body.substr(s + 1, e - s - 1);
                while (!scene.empty() && (scene.front() == ' ' || scene.front() == '"')) scene.erase(scene.begin());
                while (!scene.empty() && (scene.back()  == ' ' || scene.back()  == '"')) scene.pop_back();
                if (!scene.empty() && !fs::exists(fs::path(dir) / scene))
                    problems.push_back("default_scene missing: " + scene);
            }
        }
    }

    if (json) {
        std::printf("{\"ok\":%s,\"problems\":[", problems.empty() ? "true" : "false");
        for (size_t i = 0; i < problems.size(); ++i)
            std::printf("%s\"%s\"", i ? "," : "", json_escape(problems[i]).c_str());
        std::printf("]}\n");
    } else if (problems.empty()) {
        std::printf("validate: ok\n");
    } else {
        for (const auto& p : problems) std::printf("problem: %s\n", p.c_str());
    }
    return problems.empty() ? 0 : 1;
}

// ---------------------------------------------------------------------- docs

// Generated from the authorable component registry — see tools/docgen. Kept as
// a separate binary because it must LINK the ECS to read the registry, while
// gws itself deliberately links nothing and only drives sibling tools.
// ---------------------------------------------------------------------------
// gws shaders — compile GLSL straight to the runtime's override directory.
//
// Editing a shader used to cost a measured ~18 s: regenerate the SPIR-V header
// with spv_to_header.py, then rebuild, because touching a generated header
// forces a recompile of everything including it plus a relink of the editor.
// The budget for shader-edit-to-screen is 2 s.
//
// The renderer now prefers `<exe_dir>/shaders/<name>.spv` over the array baked
// into the binary, so this command writes exactly there and the loop becomes
// `gws shaders && editor` -- about 1.2 s measured, inside budget.
//
// This does NOT change what ships. A release install has no override directory,
// so every shader still comes from the baked array. Headers remain the source of
// truth for a shipped build; regenerate them (spv_to_header.py) when a shader
// change is ready to land.
// ---------------------------------------------------------------------------
int cmd_shaders(int argc, char** argv) {
    const bool json  = has_flag(argc, argv, "--json");
    const bool clean = has_flag(argc, argv, "--clean");

    const fs::path out_dir = g_exe_dir / "shaders";

    if (clean) {
        std::error_code ec;
        const auto removed = fs::remove_all(out_dir, ec);
        if (!json)
            std::printf("removed %llu file(s) from %s — the editor is back on its baked shaders\n",
                        static_cast<unsigned long long>(removed), out_dir.string().c_str());
        else
            std::printf("{\"cleaned\":true,\"dir\":\"%s\"}\n", json_escape(out_dir.string()).c_str());
        return 0;
    }

    // Find glslangValidator. VULKAN_SDK is the reliable one; PATH is the
    // fallback for a system-packaged SDK.
    fs::path glslang;
    if (const char* sdk = std::getenv("VULKAN_SDK")) {
        std::error_code ec;
        const fs::path c = fs::path(sdk) / "Bin" / "glslangValidator.exe";
        if (fs::exists(c, ec)) glslang = c;
    }
    if (glslang.empty()) {
        std::string probe;
        if (run_capture("glslangValidator --version", probe) == 0) glslang = "glslangValidator";
    }
    if (glslang.empty()) {
        std::fprintf(stderr,
            "gws shaders: glslangValidator not found. Set VULKAN_SDK or put it on PATH.\n");
        return 1;
    }

    // Shader sources live in the repo, not next to the binary — so this only
    // works from a source tree, which is the only place it makes sense.
    fs::path src_dir;
    for (fs::path d = g_exe_dir; !d.empty(); d = d.parent_path()) {
        const fs::path c = d / "engine" / "renderer" / "gpu" / "vulkan" / "shaders";
        std::error_code ec;
        if (fs::is_directory(c, ec)) { src_dir = c; break; }
        if (d == d.parent_path()) break;
    }
    if (src_dir.empty()) {
        std::fprintf(stderr,
            "gws shaders: no shader sources found — this needs a source checkout, "
            "not an installed engine.\n");
        return 1;
    }

    std::error_code ec;
    fs::create_directories(out_dir, ec);

    int compiled = 0, failed = 0, skipped = 0;
    std::vector<std::string> errors;

    for (const auto& e : fs::directory_iterator(src_dir, ec)) {
        if (!e.is_regular_file()) continue;
        const std::string ext = e.path().extension().string();
        if (ext != ".vert" && ext != ".frag" && ext != ".comp" &&
            ext != ".geom" && ext != ".tesc" && ext != ".tese") continue;

        const std::string name = e.path().filename().string();
        const fs::path    dst  = out_dir / (name + ".spv");

        // Skip work that is already done: recompiling 11 unchanged shaders on
        // every invocation would put the cost back into the loop this exists to
        // shorten.
        std::error_code ec2;
        if (fs::exists(dst, ec2) &&
            fs::last_write_time(dst, ec2) > fs::last_write_time(e.path(), ec2)) {
            ++skipped;
            continue;
        }

        std::string outp;
        const std::string cmd = "\"" + glslang.string() + "\" -V \"" +
                                e.path().string() + "\" -o \"" + dst.string() + "\"";
        if (run_capture(cmd, outp) == 0) {
            ++compiled;
            if (!json) std::printf("  compiled  %s\n", name.c_str());
        } else {
            ++failed;
            // Keep the compiler's own diagnostics: a shader error is something
            // the developer has to read and fix, and paraphrasing it helps
            // nobody.
            errors.push_back(name + ": " + outp);
            fs::remove(dst, ec2);   // never leave a stale .spv shadowing a broken edit
            if (!json) std::printf("  FAILED    %s\n%s\n", name.c_str(), outp.c_str());
        }
    }

    if (json) {
        std::printf("{\"compiled\":%d,\"failed\":%d,\"skipped\":%d,\"dir\":\"%s\"}\n",
                    compiled, failed, skipped, json_escape(out_dir.string()).c_str());
    } else {
        std::printf("\n%d compiled, %d unchanged, %d failed -> %s\n",
                    compiled, skipped, failed, out_dir.string().c_str());
        if (!failed && (compiled || skipped))
            std::printf("Restart the editor to see them. `gws shaders --clean` reverts to "
                        "the shaders baked into the binary.\n");
        // Coverage, stated plainly. Most render passes keep their GLSL as
        // inline string literals inside the renderer .cpp files, not as files
        // here, so this command cannot reach them and editing those still costs
        // a full rebuild. Saying "11 compiled" without this reads as full
        // coverage, which would be the more expensive kind of wrong.
        std::printf("\nNote: only shaders that exist as FILES in engine/renderer/gpu/vulkan/shaders/\n"
                    "are covered. Several passes keep their GLSL inline in the renderer .cpp files;\n"
                    "those still need a rebuild. Tracked in issue #67.\n");
    }
    return failed ? 1 : 0;
}

int cmd_docs(int argc, char** argv) {
    const char* out = opt_value(argc, argv, "--out");
    const fs::path gen = g_exe_dir / "docgen.exe";
    if (!fs::exists(gen)) {
        std::fprintf(stderr, "gws docs: docgen not found next to gws (build it first)\n");
        return 1;
    }
    std::string cmd = "\"" + gen.string() + "\"";
    if (out) { cmd += " --out \""; cmd += out; cmd += "\""; }

    // The script API reference comes from parsing the shared C table's header —
    // it carries no runtime metadata, so it cannot be walked like the component
    // registry. Same single-source-of-truth guarantee, different mechanism.
    if (const char* sh = opt_value(argc, argv, "--script-api")) {
        cmd += " --script-api \""; cmd += sh; cmd += "\"";
        if (const char* so = opt_value(argc, argv, "--script-out")) {
            cmd += " --script-out \""; cmd += so; cmd += "\"";
        }
    }
    std::string output;
    const int rc = run_capture(cmd, output);
    std::printf("%s", output.c_str());
    return rc == 0 ? 0 : 1;
}

// --------------------------------------------------------------------- crash

// Crash reports -> a grouped, filable bug (issue #11).
//
// Deliberately NOT filed by the engine at crash time. Two reasons: a shipped
// game must not carry credentials, and the moment of a crash is the worst
// possible time to depend on the network. Instead the reports sit on disk and a
// developer (or CI) files them with `gh`, which is already authenticated.
//
// Grouping is by SIGNATURE — a hash of the crashing frames — so one bug is one
// issue no matter how many times it fires. Without that, an auto-filer is just
// a way to create a hundred duplicate issues.

// Frames from the OS, the driver and the validation layer are where a crash
// SURFACES, not where it comes from. Including them in the signature would make
// every unrelated crash inside ntdll look like the same bug.
bool is_system_module(const std::string& m) {
    static const char* sys[] = { "ntdll", "kernel32", "kernelbase", "user32", "gdi32",
                                 "nvoglv", "amdvlk", "igdrcl", "vklayer", "vulkan-1",
                                 "msvcrt", "ucrtbase", "combase", "win32u" };
    std::string lower;
    for (char c : m) lower += static_cast<char>(std::tolower(c));
    for (const char* s : sys)
        if (lower.find(s) != std::string::npos) return true;
    return false;
}

struct CrashReport {
    std::string path, time, app, reason, signature, top_frame;
};

// FNV-1a over the first few non-system "module+RVA" pairs. RVA rather than the
// absolute address, because ASLR makes absolute addresses differ every run —
// hashing those would give every crash a unique signature and group nothing.
std::string crash_signature(const std::vector<std::pair<std::string, std::string>>& frames) {
    uint64_t h = 1469598103934665603ULL;
    int used = 0;
    for (const auto& f : frames) {
        if (is_system_module(f.first)) continue;
        for (char c : f.first + "+" + f.second) {
            h ^= static_cast<unsigned char>(c);
            h *= 1099511628211ULL;
        }
        if (++used == 5) break;   // the top few frames identify the bug
    }
    if (used == 0) return "unknown";
    char buf[24];
    std::snprintf(buf, sizeof buf, "%08x", static_cast<unsigned>(h & 0xffffffffu));
    return buf;
}

bool parse_crash_report(const fs::path& p, CrashReport& out) {
    std::ifstream f(p);
    if (!f) return false;
    out.path = p.string();
    std::string line;
    std::vector<std::pair<std::string, std::string>> frames;
    while (std::getline(f, line)) {
        auto field = [&](const char* key, std::string& dst) {
            const std::string k = std::string(key) + ":";
            if (line.rfind(k, 0) == 0) {
                std::string v = line.substr(k.size());
                while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
                while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
                dst = v;
            }
        };
        field("time", out.time);
        field("app", out.app);
        field("reason", out.reason);

        // "#00  editor.exe  +0x00298541  symbol  [0x...]"
        if (line.size() > 3 && line[0] == '#') {
            std::istringstream is(line);
            std::string idx, mod, rva;
            is >> idx >> mod >> rva;
            if (!mod.empty() && rva.rfind("+0x", 0) == 0) {
                frames.emplace_back(mod, rva);
                if (out.top_frame.empty() && !is_system_module(mod))
                    out.top_frame = mod + " " + rva;
            }
        }
    }
    out.signature = crash_signature(frames);
    return !out.time.empty() || !frames.empty();
}

fs::path crash_dir(int argc, char** argv) {
    if (const char* d = opt_value(argc, argv, "--dir")) return d;
    if (const char* la = std::getenv("LOCALAPPDATA"))
        return fs::path(la) / "GameWorldshaper" / "diagnostics";
    return fs::path(".");
}

int cmd_crash(int argc, char** argv) {
    const std::string sub  = argc > 2 && argv[2][0] != '-' ? argv[2] : "list";
    const bool        json = has_flag(argc, argv, "--json");
    const fs::path    dir  = crash_dir(argc, argv);

    std::error_code ec;
    std::vector<CrashReport> reports;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string name = e.path().filename().string();
        if (name.rfind("crash_report_", 0) != 0) continue;
        CrashReport r;
        if (parse_crash_report(e.path(), r)) reports.push_back(std::move(r));
    }
    std::sort(reports.begin(), reports.end(),
              [](const CrashReport& a, const CrashReport& b) { return a.time > b.time; });

    if (sub == "list") {
        if (json) {
            std::printf("{\"dir\":\"%s\",\"reports\":[", json_escape(dir.string()).c_str());
            for (size_t i = 0; i < reports.size(); ++i) {
                const auto& r = reports[i];
                std::printf("%s{\"signature\":\"%s\",\"time\":\"%s\",\"app\":\"%s\","
                            "\"reason\":\"%s\",\"top_frame\":\"%s\",\"path\":\"%s\"}",
                            i ? "," : "", r.signature.c_str(), json_escape(r.time).c_str(),
                            json_escape(r.app).c_str(), json_escape(r.reason).c_str(),
                            json_escape(r.top_frame).c_str(), json_escape(r.path).c_str());
            }
            std::printf("]}\n");
        } else if (reports.empty()) {
            std::printf("no crash reports in %s\n", dir.string().c_str());
        } else {
            std::printf("%zu crash report(s) in %s\n\n", reports.size(), dir.string().c_str());
            for (const auto& r : reports)
                std::printf("  [%s]  %s  %-22s  %s\n", r.signature.c_str(), r.time.c_str(),
                            r.reason.c_str(), r.top_frame.c_str());
            std::printf("\ngrouped by signature: identical signatures are the same bug.\n");
        }
        return 0;
    }

    if (sub == "file") {
        const char* repo = opt_value(argc, argv, "--repo");
        if (!repo) repo = "orgolis/c-Engine-Game";
        if (reports.empty()) { std::printf("nothing to file\n"); return 0; }

        // One issue per signature, not per crash.
        std::vector<std::string> seen;
        int filed = 0, skipped = 0;
        for (const auto& r : reports) {
            if (std::find(seen.begin(), seen.end(), r.signature) != seen.end()) { ++skipped; continue; }
            seen.push_back(r.signature);

            std::string q;
            const int rc = run_capture("gh issue list --repo " + std::string(repo) +
                                       " --search \"crash-sig:" + r.signature +
                                       "\" --state all --json number --jq \".[0].number\"", q);
            const bool exists = (rc == 0 && !last_line(q).empty() && last_line(q) != "null");
            if (exists) {
                std::printf("  [%s] already filed as #%s\n", r.signature.c_str(), last_line(q).c_str());
                ++skipped;
                continue;
            }
            if (has_flag(argc, argv, "--dry-run")) {
                std::printf("  [%s] would file: %s in %s\n", r.signature.c_str(),
                            r.reason.c_str(), r.top_frame.c_str());
                ++filed;
                continue;
            }
            std::string body = "Auto-filed from a local crash report.\n\ncrash-sig:" + r.signature +
                               "\n\n| | |\n|---|---|\n| time | " + r.time + " |\n| app | " + r.app +
                               " |\n| reason | " + r.reason + " |\n| top frame | `" + r.top_frame +
                               "` |\n\nSymbolize against the matching release's symbols asset.\n";
            std::string bodyfile = (fs::temp_directory_path() / ("gws_crash_" + r.signature + ".md")).string();
            { std::ofstream bf(bodyfile); bf << body; }
            std::string out2;
            const int rc2 = run_capture("gh issue create --repo " + std::string(repo) +
                                        " --title \"Crash: " + r.reason + " [" + r.signature + "]\"" +
                                        " --body-file \"" + bodyfile + "\" --label bug", out2);
            std::printf("  [%s] %s\n", r.signature.c_str(),
                        rc2 == 0 ? last_line(out2).c_str() : "FAILED to file");
            if (rc2 == 0) ++filed;
        }
        std::printf("\n%d filed, %d skipped (already known or duplicate signature)\n", filed, skipped);
        return 0;
    }

    std::fprintf(stderr, "gws crash: unknown subcommand '%s' (expected list or file)\n", sub.c_str());
    return 2;
}

// ------------------------------------------------------------------- project

// Read/modify gameplay state from outside the editor — the agent-facing
// surface. Separate binary because it links the ECS.
int cmd_project(int argc, char** argv) {
    const fs::path exe = g_exe_dir / "projectctl.exe";
    if (!fs::exists(exe)) {
        std::fprintf(stderr, "gws project: projectctl not found next to gws (build it first)\n");
        return 1;
    }
    std::string cmd = "\"" + exe.string() + "\"";
    for (int i = 2; i < argc; ++i) {
        cmd += " ";
        const std::string a = argv[i];
        cmd += (a.find(' ') != std::string::npos) ? ("\"" + a + "\"") : a;
    }
    std::string output;
    const int rc = run_capture(cmd, output);
    std::printf("%s", output.c_str());
    return rc == 0 ? 0 : 1;
}

// ----------------------------------------------------------------------- run

// Headless simulation. Separate binary because it must LINK the ECS; gws itself
// deliberately links nothing and drives sibling tools.
int cmd_run(int argc, char** argv) {
    const fs::path exe = g_exe_dir / "headless.exe";
    if (!fs::exists(exe)) {
        std::fprintf(stderr, "gws run: headless not found next to gws (build it first)\n");
        return 1;
    }
    std::string cmd = "\"" + exe.string() + "\"";
    // Forward everything after `run` except --headless, which is the default
    // and only mode: there is no windowed mode to select here.
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) continue;
        cmd += " ";
        const std::string a = argv[i];
        cmd += (a.find(' ') != std::string::npos) ? ("\"" + a + "\"") : a;
    }
    std::string output;
    const int rc = run_capture(cmd, output);
    std::printf("%s", output.c_str());
    return rc == 0 ? 0 : 1;
}

// --------------------------------------------------------------------- build

int cmd_build(int argc, char** argv) {
    const char* preset = opt_value(argc, argv, "--preset");
    const char* target = opt_value(argc, argv, "--target");
    std::string cmd = "cmake --build --preset ";
    cmd += preset ? preset : "windows-debug";
    if (target) { cmd += " --target "; cmd += target; }
    std::printf("+ %s\n", cmd.c_str());
    return std::system(cmd.c_str()) == 0 ? 0 : 1;
}

// ----------------------------------------------------------------- not built

int not_implemented(const char* name, const char* issue) {
    std::fprintf(stderr,
        "gws %s: not implemented yet (tracked by %s).\n"
        "This exits non-zero on purpose — a CLI that silently succeeds at\n"
        "something it did not do is worse than one that admits the gap.\n",
        name, issue);
    return 2;
}

int usage() {
    std::printf(
        "gws — GameWorldshaper engine CLI (%s)\n"
        "\n"
        "usage: gws <command> [options]\n"
        "\n"
        "commands:\n"
        "  version              print the engine version\n"
        "  test                 run the headless check suite\n"
        "     --json              machine-readable results\n"
        "     --list              list checks without running them\n"
        "     --filter <substr>   only checks whose name contains <substr>\n"
        "     --gpu               also run checks that need a Vulkan device\n"
        "  cook [src] [out]     run the asset pipeline (default assets/models -> cooked)\n"
        "  validate [dir]       check a project manifest and the files it references\n"
        "  docs                 generate the component reference from the registry\n"
        "  shaders              compile GLSL to the editor's override dir (--clean to revert)\n"
        "     --out <file>        write markdown to a file instead of stdout\n"
        "  project <sub>        read/modify gameplay state outside the editor\n"
        "     list    --load <f> [--json]\n"
        "     add|remove --load <f> --save <f> --entity <save-id> --component <name>\n"
        "     set     ... --field <name> --value <number>\n"
        "     (no --save means nothing is written: queries are read-only)\n"
        "  run [--headless]     run the gameplay simulation with no window\n"
        "     --frames <n>        frames to simulate (default 60)\n"
        "     --rate <hz>         fixed timestep rate (default 60)\n"
        "     --load <file>       load a .gameplay / save file\n"
        "     --project <dir>     load <dir>/scenes/main.gameplay if present\n"
        "     --demo              seed a small world, as a self-check\n"
        "  build                build via a CMake preset\n"
        "     --preset <name>     default: windows-debug\n"
        "     --target <name>     default: all\n"
        "  screenshot           NOT IMPLEMENTED — needs a Vulkan device (issue #64)\n"
        "\n"
        "every command accepts --json. exit code is the contract: 0 = success.\n",
        GWS_ENGINE_VERSION);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    g_exe_dir = fs::absolute(fs::path(argv[0])).parent_path();

    if (argc < 2 || has_flag(argc, argv, "--help") || has_flag(argc, argv, "-h"))
        return usage();

    const std::string cmd  = argv[1];
    const bool        json = has_flag(argc, argv, "--json");

    if (cmd == "version")    return cmd_version(json);
    if (cmd == "test")       return cmd_test(argc, argv);
    if (cmd == "cook")       return cmd_cook(argc, argv);
    if (cmd == "validate")   return cmd_validate(argc, argv);
    if (cmd == "docs")       return cmd_docs(argc, argv);
    if (cmd == "shaders")    return cmd_shaders(argc, argv);
    if (cmd == "project")    return cmd_project(argc, argv);
    if (cmd == "crash")      return cmd_crash(argc, argv);
    if (cmd == "build")      return cmd_build(argc, argv);
    if (cmd == "run")        return cmd_run(argc, argv);
    if (cmd == "screenshot") return not_implemented("screenshot", "issue #64");

    std::fprintf(stderr, "gws: unknown command '%s' (try --help)\n", cmd.c_str());
    return 2;
}
