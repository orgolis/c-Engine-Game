// Game export -- see game_export.h.

#include "game_export.h"

#include <stb_image.h>   // declarations; the implementation is linked from the renderer (or the check)

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_set>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace schizo::editor::gexport {

namespace {

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool read_text(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool write_text(const fs::path& p, const std::string& text) {
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    return static_cast<bool>(f);
}

bool as_bool(const std::string& v) {
    const std::string t = lower(trim(v));
    return t == "1" || t == "true" || t == "yes" || t == "on";
}

// '*' and '?' within one path component (neither crosses '/').
bool glob(const char* p, const char* s) {
    const char* star_p = nullptr;
    const char* star_s = nullptr;
    while (*s) {
        if (*p == '*') { star_p = p++; star_s = s; continue; }
        if (*p && *p != '/' && *s != '/' && (*p == '?' || *p == *s)) { ++p; ++s; continue; }
        if (*p == '/' && *s == '/') { ++p; ++s; continue; }
        if (star_p && *star_s != '/') { p = star_p + 1; s = ++star_s; continue; }
        return false;
    }
    while (*p == '*') ++p;
    return *p == 0;
}

[[maybe_unused]] std::string human_size(uint64_t b) {
    char buf[32];
    if (b >= 1024ull * 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f GB", b / (1024.0 * 1024 * 1024));
    else if (b >= 1024ull * 1024)   std::snprintf(buf, sizeof buf, "%.1f MB", b / (1024.0 * 1024));
    else if (b >= 1024ull)          std::snprintf(buf, sizeof buf, "%.0f KB", b / 1024.0);
    else                            std::snprintf(buf, sizeof buf, "%llu bytes", static_cast<unsigned long long>(b));
    return buf;
}

// The first pattern of a list that matches, or "".
std::string first_match(const std::vector<std::string>& pats, const std::string& rel, bool is_dir) {
    for (const auto& p : pats)
        if (pattern_matches(p, rel, is_dir)) return p;
    return {};
}

// Which pattern leaves this path out ("" = kept). Checks the path itself only;
// the walk never enters a folder that is left out (unless an include is inside).
std::string exclusion_of(const Settings& s, const std::string& rel, bool is_dir) {
    if (!first_match(s.include, rel, is_dir).empty()) return {};
    std::string m = first_match(default_excludes(), rel, is_dir);
    if (m.empty()) m = first_match(s.exclude, rel, is_dir);
    return m;
}

// Is some include pattern a path inside this (left-out) folder?
bool include_inside(const Settings& s, const std::string& dir_rel) {
    const std::string prefix = lower(dir_rel) + "/";
    for (const auto& p : s.include) {
        const std::string lp = lower(trim(p));
        if (lp.rfind(prefix, 0) == 0) return true;
    }
    return false;
}

bool is_text_file(const fs::path& p, uint64_t size) {
    if (size == 0 || size > 4ull * 1024 * 1024) return false;
    std::ifstream f(p, std::ios::binary);
    char buf[4096];
    f.read(buf, sizeof buf);
    const std::streamsize n = f.gcount();
    for (std::streamsize i = 0; i < n; ++i)
        if (buf[i] == 0) return false;
    return true;
}

std::string ext_of(const std::string& rel) { return lower(fs::path(rel).extension().string()); }

// stb reads paths in the ANSI code page; reading the bytes first takes any path.
bool image_info(const fs::path& p, int& w, int& h) {
    std::string bytes;
    int n = 0;
    return read_text(p, bytes) && !bytes.empty() &&
           stbi_info_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), static_cast<int>(bytes.size()), &w, &h, &n);
}
[[maybe_unused]] bool image_rgba(const fs::path& p, std::vector<uint8_t>& rgba, int& w, int& h) {
    std::string bytes;
    int n = 0;
    if (!read_text(p, bytes) || bytes.empty()) return false;
    stbi_uc* px = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), static_cast<int>(bytes.size()), &w, &h, &n, 4);
    if (!px) return false;
    rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
    stbi_image_free(px);
    return true;
}

// A file Windows Defender is still scanning cannot be changed for a moment.
template <class F> bool with_retries(F&& f) {
    for (int i = 0; i < 6; ++i) {
        if (f()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(250 * (i + 1)));
    }
    return false;
}

// Files no game loads (authoring files, notes, launchers): a text that names
// one -- a model pack's list of its .blend sources -- is no missing dependency.
bool never_loaded(const std::string& rel) {
    static const char* exts[] = {".blend", ".blend1", ".psd", ".kra", ".xcf", ".spp", ".ztl", ".max", ".ma", ".mb",
                                 ".c4d", ".md", ".cmd", ".bat", ".ps1", ".sh", ".bak", ".tmp", ".orig", ".log", ".schizo"};
    const std::string e = lower(fs::path(rel).extension().string());
    for (const char* x : exts)
        if (e == x) return true;
    return false;
}

// Every path-looking token in a text: what a scene, a .ui or an .items file
// refers to. Normalised like the plan's file list (lower case, '/').
std::vector<std::string> path_tokens(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        if (cur.size() > 3 && cur.find('.') != std::string::npos) {
            std::string t = cur;
            std::replace(t.begin(), t.end(), '\\', '/');
            while (t.rfind("./", 0) == 0) t.erase(0, 2);
            out.push_back(lower(t));
        }
        cur.clear();
    };
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) || std::strchr("\"'=,;()[]{}<>|", c)) flush();
        else cur += c;
    }
    flush();
    return out;
}

}  // namespace

// ---- Settings -----------------------------------------------------------------

std::string settings_to_text(const Settings& s) {
    std::ostringstream o;
    o << "# Export settings (Project > Export Game). Patterns: docs/EXPORTING_A_GAME.md\n";
    o << "title = " << s.title << "\n";
    o << "version = " << s.version << "\n";
    o << "start_scene = " << s.start_scene << "\n";
    o << "icon = " << s.icon << "\n";
    o << "fullscreen = " << (s.fullscreen ? 1 : 0) << "\n";
    o << "zip = " << (s.make_zip ? 1 : 0) << "\n";
    o << "output = " << s.output_dir << "\n";
    for (const auto& e : s.exclude) o << "exclude = " << e << "\n";
    for (const auto& i : s.include) o << "include = " << i << "\n";
    return o.str();
}

Settings settings_from_text(const std::string& text) {
    Settings s;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = lower(trim(t.substr(0, eq)));
        const std::string v = trim(t.substr(eq + 1));
        if (k == "title") s.title = v;
        else if (k == "version") s.version = v.empty() ? s.version : v;
        else if (k == "start_scene") s.start_scene = v;
        else if (k == "icon") s.icon = v;
        else if (k == "fullscreen") s.fullscreen = as_bool(v);
        else if (k == "zip") s.make_zip = as_bool(v);
        else if (k == "output") s.output_dir = v;
        else if (k == "exclude" && !v.empty()) s.exclude.push_back(v);
        else if (k == "include" && !v.empty()) s.include.push_back(v);
    }
    return s;
}

Settings load_settings(const fs::path& project_dir) {
    std::string text;
    if (!read_text(project_dir / kSettingsFile, text)) return Settings{};
    return settings_from_text(text);
}

bool save_settings(const fs::path& project_dir, const Settings& s) {
    return write_text(project_dir / kSettingsFile, settings_to_text(s));
}

// ---- Patterns -----------------------------------------------------------------

const std::vector<std::string>& default_excludes() {
    static const std::vector<std::string> d = {
        // version control and IDE folders
        ".git/", ".svn/", ".hg/", ".vs/", ".vscode/", ".idea/",
        // the developer's own: source art, saves, caches, editor layout
        "art_source/", "saves/", "script_cache/", "cache/", "diagnostics/", "exports/", "dist/",
        "editor.ini", "editor_layout.version", "__mp_session.scene", "__mp_session.scene.gameplay",
        // the manifest is written for the game; other manifests are launch variants
        "*.schizo",
        // this file
        "config/export.ini",
        // authoring files
        "*.blend", "*.blend1", "*.psd", "*.kra", "*.xcf", "*.spp", "*.ztl", "*.max", "*.ma", "*.mb", "*.c4d",
        // leftovers
        "*.bak", "*.bak_*", "*.tmp", "*.orig", "*.log", "Thumbs.db", ".DS_Store", "desktop.ini",
        // notes and launchers
        "*.md", "*.cmd", "*.bat", "*.ps1", "*.sh",
    };
    return d;
}

bool pattern_matches(const std::string& pattern_in, const std::string& rel_in, bool is_dir) {
    std::string p = lower(trim(pattern_in));
    std::string rel = lower(rel_in);
    std::replace(p.begin(), p.end(), '\\', '/');
    std::replace(rel.begin(), rel.end(), '\\', '/');
    while (p.rfind("./", 0) == 0) p.erase(0, 2);
    if (!p.empty() && p[0] == '/') p.erase(0, 1);
    if (p.empty()) return false;
    const bool dir_only = p.back() == '/';
    if (dir_only) p.pop_back();
    if (p.empty() || (dir_only && !is_dir)) return false;
    if (p.find('/') != std::string::npos) return glob(p.c_str(), rel.c_str());   // from the root
    const size_t slash = rel.rfind('/');
    const std::string name = slash == std::string::npos ? rel : rel.substr(slash + 1);
    return glob(p.c_str(), name.c_str());
}

bool excluded(const Settings& s, const std::string& rel_in, bool is_dir) {
    std::string rel = rel_in;
    std::replace(rel.begin(), rel.end(), '\\', '/');
    // A folder on the way that is left out leaves this out too, unless an
    // include names this path.
    if (!first_match(s.include, rel, is_dir).empty()) return false;
    size_t pos = 0;
    while ((pos = rel.find('/', pos)) != std::string::npos) {
        if (!exclusion_of(s, rel.substr(0, pos), true).empty()) return true;
        ++pos;
    }
    return !exclusion_of(s, rel, is_dir).empty();
}

std::string file_name_for(const std::string& title) {
    std::string out;
    for (unsigned char c : title) {
        const bool bad = c < 32 || std::strchr("<>:\"/\\|?*", c);
        if ((bad || c == ' ') && (out.empty() || out.back() == ' ')) continue;   // one space at most
        out += bad ? ' ' : static_cast<char>(c);
    }
    out = trim(out);
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();   // Windows drops them
    static const char* reserved[] = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "lpt1", "lpt2", "lpt3"};
    for (const char* r : reserved)
        if (lower(out) == r) out += "_game";
    return out.empty() ? "Game" : out;
}

// ---- The plan -----------------------------------------------------------------

Plan make_plan(const fs::path& project_dir, const std::string& project_name,
               const std::string& default_scene, const Settings& s) {
    Plan plan;
    plan.title = trim(s.title).empty() ? project_name : trim(s.title);
    plan.version = trim(s.version).empty() ? "1.0.0" : trim(s.version);
    plan.game_name = file_name_for(plan.title);
    plan.start_scene = trim(s.start_scene).empty() ? default_scene : trim(s.start_scene);
    std::replace(plan.start_scene.begin(), plan.start_scene.end(), '\\', '/');

    std::error_code ec;
    if (!fs::is_directory(project_dir, ec)) {
        plan.errors.push_back("The project folder is missing: " + project_dir.string());
        return plan;
    }

    std::map<std::string, LeftOut> left;           // by pattern
    std::unordered_set<std::string> left_files;    // lower-case relative paths
    std::vector<std::string> text_files;
    std::vector<std::string> native_candidates, cs_candidates;
    std::unordered_set<std::string> referenced;    // every path a text file names (lower case)

    // The walk: a folder that is left out is measured, not entered for files
    // (unless an include pattern is inside it).
    std::function<void(const fs::path&, const std::string&, const std::string&)> walk;
    walk = [&](const fs::path& dir, const std::string& rel_dir, const std::string& excluded_by) {
        std::error_code wec;
        for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, wec), end;
             !wec && it != end; it.increment(wec)) {
            const fs::path p = it->path();
            const std::string name = p.filename().string();
            const std::string rel = rel_dir.empty() ? name : rel_dir + "/" + name;
            std::error_code sec;
            const bool is_dir = it->is_directory(sec) && !it->is_symlink(sec);
            // Inside a left-out folder (entered only for an include): left out
            // unless included; elsewhere, by the patterns.
            const std::string why = (!excluded_by.empty() && first_match(s.include, rel, is_dir).empty())
                                        ? excluded_by : exclusion_of(s, rel, is_dir);
            if (is_dir) {
                if (!why.empty() && !include_inside(s, rel)) {
                    // Measure it whole.
                    LeftOut& lo = left[why];
                    lo.what = why;
                    for (fs::recursive_directory_iterator r(p, fs::directory_options::skip_permission_denied, sec), re;
                         !sec && r != re; r.increment(sec)) {
                        std::error_code fec;
                        if (r->is_regular_file(fec)) {
                            lo.bytes += r->file_size(fec);
                            ++lo.files;
                            const std::string fr = rel + "/" + fs::relative(r->path(), p, fec).generic_string();
                            if (!never_loaded(fr)) left_files.insert(lower(fr));
                        }
                    }
                    continue;
                }
                walk(p, rel, why);
                continue;
            }
            if (!it->is_regular_file(sec)) continue;
            const uint64_t size = it->file_size(sec);
            if (!why.empty()) {
                LeftOut& lo = left[why];
                lo.what = why;
                lo.bytes += size;
                ++lo.files;
                if (!never_loaded(rel)) left_files.insert(lower(rel));
                continue;
            }
            plan.files.push_back(rel);
            plan.bytes += size;
            const std::string ext = ext_of(rel);
            // The C# SDK is compiled with every C# script, never on its own.
            if (ext == ".cpp" || ext == ".cc") native_candidates.push_back(rel);
            else if (ext == ".cs" && lower(fs::path(rel).filename().string()) != "schizoscript.cs") cs_candidates.push_back(rel);
            if (is_text_file(p, size)) text_files.push_back(rel);
        }
    };
    walk(project_dir, "", "");
    std::sort(plan.files.begin(), plan.files.end());
    for (auto& [k, v] : left) plan.left_out.push_back(v);
    std::sort(plan.left_out.begin(), plan.left_out.end(),
              [](const LeftOut& a, const LeftOut& b) { return a.bytes > b.bytes; });

    // The start scene must be there and go in.
    if (plan.start_scene.empty()) {
        plan.errors.push_back("No start scene: choose the scene the game starts in.");
    } else if (!fs::is_regular_file(project_dir / plan.start_scene, ec)) {
        plan.errors.push_back("The start scene is missing: " + plan.start_scene + " (save it first).");
    } else if (excluded(s, plan.start_scene, false)) {
        plan.errors.push_back("The start scene " + plan.start_scene + " is left out by a pattern.");
    }

    // The icon.
    if (!trim(s.icon).empty()) {
        int w = 0, h = 0;
        const fs::path ip = project_dir / trim(s.icon);
        if (!fs::is_regular_file(ip, ec))
            plan.warnings.push_back("The icon " + s.icon + " is missing; the game gets no icon.");
        else if (!image_info(ip, w, h))
            plan.warnings.push_back("The icon " + s.icon + " is not an image the engine reads (use a .png); the game gets no icon.");
    }

    // A file the game uses that a pattern leaves out: the game would miss it.
    {
        int shown = 0, more = 0;
        std::set<std::string> said;
        for (const auto& rel : text_files) {
            std::string text;
            if (!read_text(project_dir / rel, text)) continue;
            for (const auto& tok : path_tokens(text)) {
                referenced.insert(tok);
                if (!left_files.count(tok) || !said.insert(tok).second) continue;
                if (shown < 8) {
                    plan.warnings.push_back(rel + " uses " + tok + ", which is left out (add an include pattern, or it will be missing in the game).");
                    ++shown;
                } else {
                    ++more;
                }
            }
        }
        if (more > 0) plan.warnings.push_back("... and " + std::to_string(more) + " more files used but left out.");
    }

    // The scripts to build: the C++ and C# files the game names -- a scene's
    // script, a .ui's, another script's. A helper a script includes, or a file
    // nothing uses, is copied but not built on its own.
    for (const auto& c : native_candidates) if (referenced.count(lower(c))) plan.native_scripts.push_back(c);
    for (const auto& c : cs_candidates) if (referenced.count(lower(c))) plan.csharp_scripts.push_back(c);
    std::sort(plan.native_scripts.begin(), plan.native_scripts.end());
    std::sort(plan.csharp_scripts.begin(), plan.csharp_scripts.end());
    if (!plan.csharp_scripts.empty())
        plan.warnings.push_back("C# scripts: players need the .NET 9 runtime installed (the scripts are built now; no SDK needed).");

    if (!trim(s.output_dir).empty()) {
        const fs::path out = fs::absolute(fs::path(trim(s.output_dir))).lexically_normal();
        const fs::path proj = fs::absolute(project_dir).lexically_normal();
        const std::string outs = lower(out.generic_string()) + "/", projs = lower(proj.generic_string()) + "/";
        if (outs.rfind(projs, 0) == 0)
            plan.errors.push_back("Export to a folder outside the project (the export would contain itself).");
        plan.folder = out / plan.game_name;
        if (s.make_zip) plan.archive = out / (plan.game_name + "-" + file_name_for(plan.version) + "-windows-x86_64.zip");
    }
    return plan;
}

// ---- game.ini ------------------------------------------------------------------

std::string game_info_to_text(const GameInfo& g) {
    std::ostringstream o;
    o << "# This game, made with GameWorldshaper\n";
    o << "title = " << g.title << "\n";
    o << "version = " << g.version << "\n";
    o << "project = " << g.project_name << "\n";
    o << "engine = " << g.engine_version << "\n";
    o << "fullscreen = " << (g.fullscreen ? 1 : 0) << "\n";
    return o.str();
}

GameInfo game_info_from_text(const std::string& text) {
    GameInfo g;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = lower(trim(t.substr(0, eq)));
        const std::string v = trim(t.substr(eq + 1));
        if (k == "title") g.title = v;
        else if (k == "version") g.version = v;
        else if (k == "project") g.project_name = v;
        else if (k == "engine") g.engine_version = v;
        else if (k == "fullscreen") g.fullscreen = as_bool(v);
    }
    return g;
}

// ---- The icon file --------------------------------------------------------------

std::vector<uint8_t> make_ico(const std::vector<uint8_t>& rgba, int w, int h) {
    static const int sizes[] = {16, 24, 32, 48, 64, 256};
    struct Img { int size; std::vector<uint8_t> dib; };
    std::vector<Img> imgs;
    if (w <= 0 || h <= 0 || rgba.size() < static_cast<size_t>(w) * h * 4) return {};
    for (int sz : sizes) {
        // Fit the image in sz x sz, centred, transparent around it; each target
        // pixel averages the source pixels under it (premultiplied, so a
        // transparent neighbour does not darken the edge).
        const float scale = std::min(static_cast<float>(sz) / w, static_cast<float>(sz) / h);
        const float dw = w * scale, dh = h * scale;
        const float ox = (sz - dw) * 0.5f, oy = (sz - dh) * 0.5f;
        std::vector<float> acc(static_cast<size_t>(sz) * sz * 4, 0.0f);
        for (int ty = 0; ty < sz; ++ty) {
            for (int tx = 0; tx < sz; ++tx) {
                const float sx0 = (tx - ox) / scale, sx1 = (tx + 1 - ox) / scale;
                const float sy0 = (ty - oy) / scale, sy1 = (ty + 1 - oy) / scale;
                const int x0 = std::max(0, static_cast<int>(std::floor(sx0))), x1 = std::min(w, static_cast<int>(std::ceil(sx1)));
                const int y0 = std::max(0, static_cast<int>(std::floor(sy0))), y1 = std::min(h, static_cast<int>(std::ceil(sy1)));
                double r = 0, g = 0, b = 0, a = 0, n = 0;
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x) {
                        const uint8_t* px = &rgba[(static_cast<size_t>(y) * w + x) * 4];
                        const double al = px[3] / 255.0;
                        r += px[0] * al; g += px[1] * al; b += px[2] * al; a += al; n += 1;
                    }
                float* o = &acc[(static_cast<size_t>(ty) * sz + tx) * 4];
                if (n > 0 && a > 0) {
                    // Coverage of the target pixel by the image (edges of a letterbox).
                    const float cx = std::clamp(std::min(sx1, static_cast<float>(w)) - std::max(sx0, 0.0f), 0.0f, sx1 - sx0) / (sx1 - sx0);
                    const float cy = std::clamp(std::min(sy1, static_cast<float>(h)) - std::max(sy0, 0.0f), 0.0f, sy1 - sy0) / (sy1 - sy0);
                    o[0] = static_cast<float>(r / a);
                    o[1] = static_cast<float>(g / a);
                    o[2] = static_cast<float>(b / a);
                    o[3] = static_cast<float>(a / n * 255.0) * cx * cy;
                }
            }
        }
        // BITMAPINFOHEADER (height doubled: XOR + AND), BGRA bottom-up, AND mask.
        const int mask_row = ((sz + 31) / 32) * 4;
        std::vector<uint8_t> dib(40 + static_cast<size_t>(sz) * sz * 4 + static_cast<size_t>(mask_row) * sz, 0);
        auto put32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) dib[at + i] = static_cast<uint8_t>(v >> (8 * i)); };
        auto put16 = [&](size_t at, uint16_t v) { dib[at] = static_cast<uint8_t>(v); dib[at + 1] = static_cast<uint8_t>(v >> 8); };
        put32(0, 40); put32(4, static_cast<uint32_t>(sz)); put32(8, static_cast<uint32_t>(sz * 2));
        put16(12, 1); put16(14, 32); put32(16, 0);
        put32(20, static_cast<uint32_t>(sz * sz * 4 + mask_row * sz));
        for (int y = 0; y < sz; ++y) {
            const int src_y = sz - 1 - y;   // bottom-up
            for (int x = 0; x < sz; ++x) {
                const float* a = &acc[(static_cast<size_t>(src_y) * sz + x) * 4];
                uint8_t* d = &dib[40 + (static_cast<size_t>(y) * sz + x) * 4];
                d[0] = static_cast<uint8_t>(std::clamp(a[2], 0.0f, 255.0f));
                d[1] = static_cast<uint8_t>(std::clamp(a[1], 0.0f, 255.0f));
                d[2] = static_cast<uint8_t>(std::clamp(a[0], 0.0f, 255.0f));
                d[3] = static_cast<uint8_t>(std::clamp(a[3], 0.0f, 255.0f));
            }
        }
        imgs.push_back({sz, std::move(dib)});
    }
    std::vector<uint8_t> ico;
    auto w16 = [&](uint16_t v) { ico.push_back(static_cast<uint8_t>(v)); ico.push_back(static_cast<uint8_t>(v >> 8)); };
    auto w32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) ico.push_back(static_cast<uint8_t>(v >> (8 * i))); };
    w16(0); w16(1); w16(static_cast<uint16_t>(imgs.size()));
    uint32_t offset = 6 + 16 * static_cast<uint32_t>(imgs.size());
    for (const auto& im : imgs) {
        ico.push_back(static_cast<uint8_t>(im.size >= 256 ? 0 : im.size));
        ico.push_back(static_cast<uint8_t>(im.size >= 256 ? 0 : im.size));
        ico.push_back(0); ico.push_back(0);
        w16(1); w16(32);
        w32(static_cast<uint32_t>(im.dib.size()));
        w32(offset);
        offset += static_cast<uint32_t>(im.dib.size());
    }
    for (const auto& im : imgs) ico.insert(ico.end(), im.dib.begin(), im.dib.end());
    return ico;
}

// ---- Windows: resources and the subsystem ------------------------------------------

#ifdef _WIN32

namespace {

std::string last_error_text(const char* what) {
    return std::string(what) + " failed (Windows error " + std::to_string(static_cast<unsigned long>(GetLastError())) + ")";
}

// VS_VERSIONINFO, built as nested blocks: each starts on a 4-byte boundary,
// wLength covers the block and its children.
struct VerWriter {
    std::vector<uint8_t> b;
    void align() { while (b.size() % 4) b.push_back(0); }
    size_t begin(const std::wstring& key, uint16_t value_length, uint16_t type) {
        align();
        const size_t at = b.size();
        put16(0); put16(value_length); put16(type);
        for (wchar_t c : key) put16(static_cast<uint16_t>(c));
        put16(0);
        align();
        return at;
    }
    void end(size_t at) {
        const size_t len = b.size() - at;
        b[at] = static_cast<uint8_t>(len);
        b[at + 1] = static_cast<uint8_t>(len >> 8);
    }
    void put16(uint16_t v) { b.push_back(static_cast<uint8_t>(v)); b.push_back(static_cast<uint8_t>(v >> 8)); }
    void put32(uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i))); }
    void string(const std::wstring& key, const std::wstring& value) {
        const size_t at = begin(key, static_cast<uint16_t>(value.size() + 1), 1);
        for (wchar_t c : value) put16(static_cast<uint16_t>(c));
        put16(0);
        end(at);
    }
};

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

void version_numbers(const std::string& v, uint16_t out[4]) {
    out[0] = out[1] = out[2] = out[3] = 0;
    int i = 0;
    std::string cur;
    for (char c : v + ".") {
        if (std::isdigit(static_cast<unsigned char>(c))) { cur += c; continue; }
        if (c == '.' || !cur.empty()) {
            if (i < 4) out[i++] = static_cast<uint16_t>(std::min(65535L, std::atol(cur.empty() ? "0" : cur.c_str())));
            cur.clear();
            if (c != '.') break;   // "1.2.0-beta": stop at the suffix
        }
    }
}

}  // namespace

bool embed_icon(const fs::path& exe, const std::vector<uint8_t>& rgba, int w, int h, std::string& error) {
    const std::vector<uint8_t> ico = make_ico(rgba, w, h);
    if (ico.size() < 6) { error = "the icon image is empty"; return false; }
    const uint16_t count = static_cast<uint16_t>(ico[4] | (ico[5] << 8));
    HANDLE upd = BeginUpdateResourceW(exe.wstring().c_str(), FALSE);
    if (!upd) { error = last_error_text("BeginUpdateResource"); return false; }
    const WORD lang = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
    // RT_GROUP_ICON (14): the .ico directory with a resource id in place of each offset.
    std::vector<uint8_t> group(ico.begin(), ico.begin() + 6);
    bool ok = true;
    for (uint16_t i = 0; i < count && ok; ++i) {
        const uint8_t* e = &ico[6 + 16 * i];
        const uint32_t size = e[8] | (e[9] << 8) | (e[10] << 16) | (static_cast<uint32_t>(e[11]) << 24);
        const uint32_t off = e[12] | (e[13] << 8) | (e[14] << 16) | (static_cast<uint32_t>(e[15]) << 24);
        if (off + size > ico.size()) { ok = false; break; }
        const WORD id = static_cast<WORD>(i + 1);
        ok = UpdateResourceW(upd, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(id), lang,
                             const_cast<uint8_t*>(&ico[off]), size) != FALSE;
        group.insert(group.end(), e, e + 12);          // width .. bytes in resource
        group.push_back(static_cast<uint8_t>(id));
        group.push_back(static_cast<uint8_t>(id >> 8));
    }
    if (ok)
        ok = UpdateResourceW(upd, MAKEINTRESOURCEW(14), MAKEINTRESOURCEW(1), lang, group.data(),
                             static_cast<DWORD>(group.size())) != FALSE;
    if (!ok) {
        error = last_error_text("UpdateResource (icon)");
        EndUpdateResourceW(upd, TRUE);
        return false;
    }
    if (!EndUpdateResourceW(upd, FALSE)) { error = last_error_text("EndUpdateResource (icon)"); return false; }
    return true;
}

bool embed_version(const fs::path& exe, const std::string& product, const std::string& version,
                   const std::string& file_name, std::string& error) {
    uint16_t v[4];
    version_numbers(version, v);
    VerWriter w;
    const size_t root = w.begin(L"VS_VERSION_INFO", 52, 0);
    // VS_FIXEDFILEINFO
    w.put32(0xFEEF04BD); w.put32(0x00010000);
    w.put32((static_cast<uint32_t>(v[0]) << 16) | v[1]); w.put32((static_cast<uint32_t>(v[2]) << 16) | v[3]);
    w.put32((static_cast<uint32_t>(v[0]) << 16) | v[1]); w.put32((static_cast<uint32_t>(v[2]) << 16) | v[3]);
    w.put32(0x3F); w.put32(0); w.put32(0x00040004 /* VOS_NT_WINDOWS32 */); w.put32(1 /* VFT_APP */);
    w.put32(0); w.put32(0); w.put32(0);
    const size_t sfi = w.begin(L"StringFileInfo", 0, 1);
    const size_t table = w.begin(L"040904B0", 0, 1);
    const std::wstring wp = widen(product), wv = widen(version), wf = widen(file_name);
    w.string(L"FileDescription", wp);
    w.string(L"FileVersion", wv);
    w.string(L"InternalName", wp);
    w.string(L"OriginalFilename", wf);
    w.string(L"ProductName", wp);
    w.string(L"ProductVersion", wv);
    w.string(L"Comments", L"Made with GameWorldshaper");
    w.end(table);
    w.end(sfi);
    const size_t vfi = w.begin(L"VarFileInfo", 0, 1);
    const size_t var = w.begin(L"Translation", 4, 0);
    w.put16(0x0409); w.put16(0x04B0);
    w.end(var);
    w.end(vfi);
    w.end(root);

    HANDLE upd = BeginUpdateResourceW(exe.wstring().c_str(), FALSE);
    if (!upd) { error = last_error_text("BeginUpdateResource"); return false; }
    if (!UpdateResourceW(upd, MAKEINTRESOURCEW(16), MAKEINTRESOURCEW(1), MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                         w.b.data(), static_cast<DWORD>(w.b.size()))) {
        error = last_error_text("UpdateResource (version)");
        EndUpdateResourceW(upd, TRUE);
        return false;
    }
    if (!EndUpdateResourceW(upd, FALSE)) { error = last_error_text("EndUpdateResource (version)"); return false; }
    return true;
}

bool strip_debug_sections(const fs::path& exe, std::string& error) {
    std::string d;
    if (!read_text(exe, d) || d.size() < 0x40 || d[0] != 'M' || d[1] != 'Z') { error = "cannot read " + exe.string(); return false; }
    auto u16 = [&](size_t at) { return static_cast<uint32_t>(static_cast<uint8_t>(d[at]) | (static_cast<uint8_t>(d[at + 1]) << 8)); };
    auto u32 = [&](size_t at) { return u16(at) | (u16(at + 2) << 16); };
    auto w32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) d[at + i] = static_cast<char>(v >> (8 * i)); };
    const size_t pe = u32(0x3C);
    if (pe + 24 > d.size() || d.compare(pe, 4, std::string("PE\0\0", 4)) != 0) { error = "no PE header"; return false; }
    const size_t coff = pe + 4;
    uint32_t nsec = u16(coff + 2);
    const size_t opt = coff + 20;
    const size_t sec = opt + u16(coff + 16);
    const uint32_t sect_align = u32(opt + 32);
    // MinGW's DWARF sections have long names ("/29": an offset into the COFF
    // string table) and come last. Drop them from the end of the table.
    uint32_t keep = nsec;
    while (keep > 0 && d[sec + 40 * (keep - 1)] == '/') --keep;
    if (keep == nsec && u32(coff + 8) == 0) return true;   // nothing to strip
    // A kept section with a long name ("/4") names it in the string table that
    // goes: write the name itself (8 characters at most) into its header.
    const size_t strtab = static_cast<size_t>(u32(coff + 8)) + static_cast<size_t>(u32(coff + 12)) * 18;
    for (uint32_t i = 0; i < keep; ++i) {
        const size_t s = sec + 40 * i;
        if (d[s] != '/') continue;
        const size_t off = static_cast<size_t>(std::atol(std::string(&d[s + 1], 7).c_str()));
        std::string name = (u32(coff + 8) != 0 && strtab + off < d.size()) ? std::string(d.c_str() + strtab + off) : ".sect";
        name.resize(8, '\0');
        std::memcpy(&d[s], name.data(), 8);
    }
    uint32_t file_end = 0, image_end = 0;
    for (uint32_t i = 0; i < keep; ++i) {
        const size_t s = sec + 40 * i;
        const uint32_t vsize = u32(s + 8), va = u32(s + 12), rsize = u32(s + 16), rptr = u32(s + 20);
        if (rsize) file_end = std::max(file_end, rptr + rsize);
        image_end = std::max(image_end, va + ((std::max(vsize, rsize) + sect_align - 1) / sect_align) * sect_align);
    }
    for (uint32_t i = keep; i < nsec; ++i) std::memset(&d[sec + 40 * i], 0, 40);
    d[coff + 2] = static_cast<char>(keep);
    d[coff + 3] = static_cast<char>(keep >> 8);
    w32(coff + 8, 0);    // PointerToSymbolTable
    w32(coff + 12, 0);   // NumberOfSymbols
    w32(opt + 56, image_end);   // SizeOfImage
    if (file_end == 0 || file_end > d.size()) { error = "unexpected section layout"; return false; }
    d.resize(file_end);
    if (!write_text(exe, d)) { error = "cannot write " + exe.string(); return false; }
    return true;
}

bool set_windowed_program(const fs::path& exe, std::string& error) {
    std::fstream f(exe, std::ios::binary | std::ios::in | std::ios::out);
    if (!f) { error = "cannot open " + exe.string(); return false; }
    uint8_t dos[64];
    f.read(reinterpret_cast<char*>(dos), sizeof dos);
    if (!f || dos[0] != 'M' || dos[1] != 'Z') { error = "not a Windows program: " + exe.string(); return false; }
    const uint32_t pe = dos[0x3C] | (dos[0x3D] << 8) | (dos[0x3E] << 16) | (static_cast<uint32_t>(dos[0x3F]) << 24);
    uint8_t sig[4];
    f.seekg(pe);
    f.read(reinterpret_cast<char*>(sig), 4);
    if (!f || std::memcmp(sig, "PE\0\0", 4) != 0) { error = "no PE header in " + exe.string(); return false; }
    // Optional header after the 20-byte file header; Subsystem is at +68 for
    // both PE32 and PE32+.
    const std::streamoff subsystem_at = static_cast<std::streamoff>(pe) + 4 + 20 + 68;
    uint8_t sub[2];
    f.seekg(subsystem_at);
    f.read(reinterpret_cast<char*>(sub), 2);
    const uint16_t now = static_cast<uint16_t>(sub[0] | (sub[1] << 8));
    if (now != 2 /* GUI */ && now != 3 /* CUI */) { error = "unexpected subsystem " + std::to_string(now); return false; }
    const uint8_t gui[2] = {2, 0};
    f.seekp(subsystem_at);
    f.write(reinterpret_cast<const char*>(gui), 2);
    if (!f) { error = "cannot write " + exe.string(); return false; }
    return true;
}

#else

bool embed_icon(const fs::path&, const std::vector<uint8_t>&, int, int, std::string& error) {
    error = "icons are put into Windows programs only"; return false;
}
bool embed_version(const fs::path&, const std::string&, const std::string&, const std::string&, std::string& error) {
    error = "version information is put into Windows programs only"; return false;
}
bool set_windowed_program(const fs::path&, std::string& error) {
    error = "Windows programs only"; return false;
}
bool strip_debug_sections(const fs::path&, std::string& error) {
    error = "Windows programs only"; return false;
}

#endif

// ---- The export ------------------------------------------------------------------

namespace {

#ifdef _WIN32
// Run a program without a console window; `tick` is called while it runs.
int run_hidden(const std::wstring& cmdline, std::string& output, const std::function<void()>& tick) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring mut = cmdline;
    if (!CreateProcessW(nullptr, mut.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(rd);
        CloseHandle(wr);
        return -1;
    }
    CloseHandle(wr);
    std::thread reader([&] {
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(rd, buf, sizeof buf, &n, nullptr) && n > 0) {
            output.append(buf, n);
            if (output.size() > 65536) output.erase(0, output.size() - 65536);
        }
    });
    while (WaitForSingleObject(pi.hProcess, 200) == WAIT_TIMEOUT)
        if (tick) tick();
    reader.join();
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rd);
    return static_cast<int>(code);
}
#endif

[[maybe_unused]] bool copy_file_to(const fs::path& from, const fs::path& to, std::string& error) {
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) { error = "cannot copy " + from.string() + ": " + ec.message(); return false; }
    return true;
}

[[maybe_unused]] bool copy_tree(const fs::path& from, const fs::path& to, std::string& error) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec)) continue;
        if (!copy_file_to(it->path(), to / fs::relative(it->path(), from, fec), error)) return false;
    }
    return true;
}

[[maybe_unused]] bool has_marker(const fs::path& dir) {
    std::error_code ec;
    return fs::is_regular_file(dir / kMarkerFile, ec);
}

[[maybe_unused]] std::string set_manifest_key(const std::string& text, const std::string& key, const std::string& value) {
    std::istringstream in(text);
    std::ostringstream out;
    std::string line;
    bool done = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trim(line);
        const size_t eq = t.find('=');
        if (!done && eq != std::string::npos && lower(trim(t.substr(0, eq))) == key) {
            out << key << " = " << value << "\n";
            done = true;
            continue;
        }
        out << line << "\n";
    }
    if (!done) out << key << " = " << value << "\n";
    return out.str();
}

}  // namespace

Result run(const Request& r) {
    Result res;
    auto progress = [&](float f, const std::string& what) { if (r.progress) r.progress(std::clamp(f, 0.0f, 1.0f), what); };
#ifndef _WIN32
    res.message = "This exporter makes Windows games; on Linux, use the Linux exporter (tools/export_game_linux.sh).";
    return res;
#else
    progress(0.0f, "Checking the project");
    const Plan plan = make_plan(r.project_dir, r.project_name, r.default_scene, r.settings);
    res.warnings = plan.warnings;
    if (!plan.errors.empty()) {
        for (const auto& e : plan.errors) res.message += e + "\n";
        return res;
    }
    if (trim(r.settings.output_dir).empty()) { res.message = "Choose a folder to export to."; return res; }
    std::error_code ec;
    if (!fs::is_regular_file(r.runtime_exe, ec)) { res.message = "The engine program is missing: " + r.runtime_exe.string(); return res; }
    if (!plan.native_scripts.empty() && !r.build_native) { res.message = "This engine cannot build C++ scripts."; return res; }
    if (!plan.csharp_scripts.empty() && !r.build_csharp) { res.message = "This engine cannot build C# scripts."; return res; }

    const fs::path out = fs::absolute(fs::path(trim(r.settings.output_dir))).lexically_normal();
    const fs::path final_dir = plan.folder;
    const fs::path partial = out / (plan.game_name + ".partial");
    fs::create_directories(out, ec);
    if (!fs::is_directory(out, ec)) { res.message = "Cannot create the folder " + out.string(); return res; }
    if (fs::exists(final_dir, ec) && !has_marker(final_dir)) {
        res.message = "A folder named \"" + plan.game_name + "\" is already in " + out.string() +
                      " and is not an earlier export. Choose another folder, or move it.";
        return res;
    }
    if (fs::exists(partial, ec)) {
        if (!has_marker(partial)) { res.message = "Remove the unfinished folder " + partial.string() + " first."; return res; }
        fs::remove_all(partial, ec);
    }
    fs::create_directories(partial, ec);
    // The marker first: an unfinished export is always recognisable as one.
    if (!write_text(partial / kMarkerFile, "GameWorldshaper packaged game\n")) {
        res.message = "Cannot write to " + partial.string();
        return res;
    }
    auto fail = [&](const std::string& why) {
        std::error_code rec;
        fs::remove_all(partial, rec);
        res.ok = false;
        res.message = why;
        return res;
    };
    const fs::path proj_out = partial / "project";
    const fs::path exe = partial / (plan.game_name + ".exe");

    // 1. Scripts, built for the game (0.02 .. 0.35).
    {
        const size_t total = plan.native_scripts.size() + plan.csharp_scripts.size();
        size_t done = 0;
        auto build = [&](const std::vector<std::string>& list, const BuildFn& fn, bool csharp) -> std::string {
            for (const auto& rel : list) {
                progress(0.02f + 0.33f * static_cast<float>(done) / std::max<size_t>(1, total),
                         "Building script " + rel);
                const fs::path dst = proj_out / kPrebuiltDir / (prebuilt_key(rel) + (csharp ? "" : ".dll"));
                std::error_code cec;
                fs::create_directories(csharp ? dst : dst.parent_path(), cec);
                std::string err;
                if (!fn(rel, dst, err))
                    return "The script " + rel + " does not build:\n" + err +
                           "\n\n(If it is not a script but a file another script includes, leave it out in the export settings.)";
                ++done;
            }
            return {};
        };
        if (std::string e = build(plan.native_scripts, r.build_native, false); !e.empty()) return fail(e);
        if (std::string e = build(plan.csharp_scripts, r.build_csharp, true); !e.empty()) return fail(e);
    }

    // 2. The engine (0.35 .. 0.40).
    progress(0.35f, "Copying the engine");
    {
        std::string err;
        if (!copy_file_to(r.runtime_exe, exe, err)) return fail(err);
        const fs::path rt_dir = r.runtime_exe.parent_path();
        for (fs::directory_iterator it(rt_dir, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = lower(it->path().filename().string());
            if (it->is_regular_file() && (ext_of(name) == ".dll" || name == "glslangvalidator.exe"))
                if (!copy_file_to(it->path(), partial / it->path().filename(), err)) return fail(err);
        }
        const fs::path scripts = r.engine_root / "assets" / "scripts";
        if (fs::is_directory(scripts, ec) && !copy_tree(scripts, partial / "assets" / "scripts", err)) return fail(err);
        for (const char* lic : {"LICENSE", "THIRD_PARTY_NOTICES.md"}) {
            for (const fs::path& base : {r.engine_root, rt_dir}) {
                if (fs::is_regular_file(base / lic, ec)) {
                    if (!copy_file_to(base / lic, partial / "licenses" / (std::string(lic) == "LICENSE" ? "GameWorldshaper-LICENSE.txt" : lic), err)) return fail(err);
                    break;
                }
            }
        }
    }

    // 3. The project (0.40 .. 0.75), by bytes.
    {
        uint64_t copied = 0;
        auto last = std::chrono::steady_clock::now();
        for (const auto& rel : plan.files) {
            std::string err;
            const fs::path from = r.project_dir / rel;
            if (!copy_file_to(from, proj_out / rel, err)) return fail(err);
            copied += fs::file_size(from, ec);
            const auto now = std::chrono::steady_clock::now();
            if (now - last > std::chrono::milliseconds(100)) {
                last = now;
                progress(0.40f + 0.35f * static_cast<float>(copied) / std::max<uint64_t>(1, plan.bytes),
                         "Copying the project (" + human_size(copied) + " of " + human_size(plan.bytes) + ")");
            }
        }
        // The manifest: the chosen start scene, this engine.
        std::string manifest;
        read_text(r.project_dir / "project.schizo", manifest);
        manifest = set_manifest_key(manifest, "name", r.project_name);
        manifest = set_manifest_key(manifest, "default_scene", plan.start_scene);
        if (!r.engine_version.empty()) manifest = set_manifest_key(manifest, "engine_version", r.engine_version);
        if (!write_text(proj_out / "project.schizo", manifest)) return fail("Cannot write the game's project.schizo");
    }

    // 4. The program's identity (0.75 .. 0.78).
    progress(0.75f, "Naming the program");
    {
        std::string err;
        // A development build carries its debug information; a game does not
        // need it, and changing resources would cut its symbol table in half.
        if (!with_retries([&] { return strip_debug_sections(exe, err); }))
            res.warnings.push_back("The program's debug information could not be removed: " + err);
        if (!trim(r.settings.icon).empty()) {
            const fs::path ip = r.project_dir / trim(r.settings.icon);
            int w = 0, h = 0;
            std::vector<uint8_t> rgba;
            if (image_rgba(ip, rgba, w, h)) {
                if (!with_retries([&] { return embed_icon(exe, rgba, w, h, err); }))
                    res.warnings.push_back("The icon could not be put into the program: " + err);
                std::string cerr;
                if (!copy_file_to(ip, partial / ("icon" + lower(ip.extension().string())), cerr))
                    res.warnings.push_back("The window icon was not copied: " + cerr);
            }
        }
        if (!with_retries([&] { return embed_version(exe, plan.title, plan.version, plan.game_name + ".exe", err); }))
            res.warnings.push_back("Version information could not be put into the program: " + err);
        if (!with_retries([&] { return set_windowed_program(exe, err); })) res.warnings.push_back("The program still opens a console window: " + err);
    }

    // 5. What the player reads (0.78 .. 0.80).
    {
        GameInfo gi;
        gi.title = plan.title;
        gi.version = plan.version;
        gi.engine_version = r.engine_version;
        gi.project_name = r.project_name;
        gi.fullscreen = r.settings.fullscreen;
        if (!write_text(partial / kGameInfoFile, game_info_to_text(gi))) return fail("Cannot write game.ini");
        std::ostringstream rd;
        rd << plan.title << " " << plan.version << "\n\n"
           << "Start: " << plan.game_name << ".exe (keep it in this folder with the other files).\n"
           << "Full screen: F11 or Alt+Enter switches between full screen and a window.\n"
           << "Your settings and saves: %APPDATA%\\GameWorldshaper\\games\\" << r.project_name << "\\\n\n"
           << "Needs: 64-bit Windows 10 or 11 and a graphics card with Vulkan.\n";
        if (!plan.csharp_scripts.empty()) rd << "Also needs: the .NET 9 runtime (https://dotnet.microsoft.com/download).\n";
        rd << "\nMade with GameWorldshaper " << r.engine_version << " -- licences in the licenses folder.\n";
        if (!write_text(partial / "README.txt", rd.str())) return fail("Cannot write README.txt");
    }

    // 6. In place of an earlier export.
    progress(0.80f, "Finishing the folder");
    if (fs::exists(final_dir, ec)) {
        with_retries([&] { ec.clear(); fs::remove_all(final_dir, ec); return !ec; });
        if (ec) return fail("Cannot replace the earlier export " + final_dir.string() + " (is the game running?): " + ec.message());
    }
    with_retries([&] { ec.clear(); fs::rename(partial, final_dir, ec); return !ec; });
    if (ec) return fail("Cannot rename the export folder: " + ec.message());
    res.folder = final_dir;
    res.exe = final_dir / (plan.game_name + ".exe");

    // 7. The zip (0.80 .. 1.0).
    if (r.settings.make_zip && !plan.archive.empty()) {
        progress(0.80f, "Packing the .zip");
        fs::remove(plan.archive, ec);
        uint64_t folder_bytes = 0;
        for (fs::recursive_directory_iterator it(final_dir, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code fec;
            if (it->is_regular_file(fec)) folder_bytes += it->file_size(fec);
        }
        wchar_t sysdir[MAX_PATH]{};
        GetSystemDirectoryW(sysdir, MAX_PATH);
        const fs::path tar = fs::path(sysdir) / L"tar.exe";
        std::string output;
        auto tick = [&] {
            std::error_code zec;
            const uint64_t z = fs::exists(plan.archive, zec) ? fs::file_size(plan.archive, zec) : 0;
            // A packed game is roughly half its size; never claim to be done early.
            progress(0.80f + 0.19f * std::min(1.0f, static_cast<float>(z) / std::max<uint64_t>(1, folder_bytes / 2)),
                     "Packing the .zip (" + human_size(z) + ")");
        };
        int code = -1;
        if (fs::is_regular_file(tar, ec)) {
            const std::wstring cmd = L"\"" + tar.wstring() + L"\" -a -c -f \"" + plan.archive.wstring() +
                                     L"\" -C \"" + out.wstring() + L"\" \"" + final_dir.filename().wstring() + L"\"";
            code = run_hidden(cmd, output, tick);
        }
        if (code != 0) {
            fs::remove(plan.archive, ec);
            output.clear();
            const std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -Command \"Compress-Archive -LiteralPath '" +
                                     final_dir.wstring() + L"' -DestinationPath '" + plan.archive.wstring() + L"'\"";
            code = run_hidden(cmd, output, tick);
        }
        if (code != 0 || !fs::is_regular_file(plan.archive, ec)) {
            res.warnings.push_back("The .zip could not be made (the game folder is complete): " + trim(output));
        } else {
            res.archive = plan.archive;
        }
    }

    progress(1.0f, "Done");
    res.ok = true;
    std::ostringstream m;
    m << plan.title << " " << plan.version << " is ready.\n\n"
      << "Game folder: " << res.folder.string() << "\n";
    if (!res.archive.empty()) m << "To share: " << res.archive.string() << " (" << human_size(fs::file_size(res.archive, ec)) << ")\n";
    m << "\n" << plan.files.size() << " project files (" << human_size(plan.bytes) << ")";
    if (!plan.native_scripts.empty() || !plan.csharp_scripts.empty())
    {
        const size_t n = plan.native_scripts.size() + plan.csharp_scripts.size();
        m << ", " << n << (n == 1 ? " script" : " scripts") << " built";
    }
    m << ".";
    res.message = m.str();
    return res;
#endif
}

}  // namespace schizo::editor::gexport
