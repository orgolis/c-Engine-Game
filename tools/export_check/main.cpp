// export_check -- editor/include/game_export.h: what an exported game contains
// and how it is made.
//
// The pure parts (settings, patterns, the plan, game.ini, the icon file) on
// their own; then a whole export of a small project made here, with this
// program standing in for the engine, read back: the folder, the manifest's
// start scene, the scripts built through the callback, the icon and version
// inside the copied program, no console window, the .zip, and a second export
// over the first.

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "game_export.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;
namespace gx = schizo::editor::gexport;

static int g_pass = 0, g_fail = 0;
static void check(const char* what, bool ok) {
    std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", what);
    (ok ? g_pass : g_fail)++;
}

static void write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}
static std::string read(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
static bool has(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& x : v) if (x == s) return true;
    return false;
}

// A 3x2 PNG would need an encoder; an uncompressed 24-bit BMP is readable by
// stb_image and enough to make an icon from.
static std::string tiny_bmp() {
    const int w = 3, h = 2, row = 12;   // 3 px * 3 bytes = 9, padded to 12
    std::string b(54 + row * h, '\0');
    auto p32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; ++i) b[at + i] = static_cast<char>(v >> (8 * i)); };
    b[0] = 'B'; b[1] = 'M';
    p32(2, static_cast<uint32_t>(b.size())); p32(10, 54); p32(14, 40); p32(18, w); p32(22, h);
    b[26] = 1; b[28] = 24; p32(34, static_cast<uint32_t>(row * h));
    for (int i = 54; i < static_cast<int>(b.size()); i += 3) { b[i] = '\x20'; b[i + 1] = '\x80'; b[i + 2] = '\xF0'; }
    return b;
}

int main(int, char** argv) {
    std::printf("export_check\n");

    // ---- settings ----------------------------------------------------------
    {
        gx::Settings s;
        s.title = "The Beacon";
        s.version = "1.2.3";
        s.start_scene = "scenes/start.scene";
        s.icon = "config/icon.png";
        s.fullscreen = false;
        s.make_zip = false;
        s.exclude = {"docs/", "*.wav"};
        s.include = {"docs/credits.md"};
        s.output_dir = "C:/Exports";
        const gx::Settings r = gx::settings_from_text(gx::settings_to_text(s));
        check("settings: written and read back the same",
              r.title == s.title && r.version == s.version && r.start_scene == s.start_scene && r.icon == s.icon &&
              r.fullscreen == s.fullscreen && r.make_zip == s.make_zip && r.exclude == s.exclude &&
              r.include == s.include && r.output_dir == s.output_dir);
        const gx::Settings d = gx::settings_from_text("");
        check("settings: none -> version 1.0.0, full screen, a zip", d.version == "1.0.0" && d.fullscreen && d.make_zip);
    }

    // ---- patterns ----------------------------------------------------------
    {
        check("pattern: *.md anywhere, any case",
              gx::pattern_matches("*.md", "notes/Plan.MD", false) && gx::pattern_matches("*.md", "README.md", false));
        check("pattern: a folder pattern matches folders only",
              gx::pattern_matches("art_source/", "art_source", true) && !gx::pattern_matches("art_source/", "art_source", false));
        check("pattern: with a / inside, from the root only",
              gx::pattern_matches("config/export.ini", "config/export.ini", false) &&
              !gx::pattern_matches("config/export.ini", "sub/config/export.ini", false));
        check("pattern: * stays within a name",
              gx::pattern_matches("scenes/*.scene", "scenes/a.scene", false) &&
              !gx::pattern_matches("scenes/*.scene", "scenes/x/a.scene", false));
        gx::Settings s;
        s.include = {"docs/credits.md"};
        s.exclude = {"docs/"};
        check("excluded: under a left-out folder", gx::excluded(s, "saves/slot1.sav", false) && gx::excluded(s, "art_source/a/b.png", false));
        check("excluded: an include wins", !gx::excluded(s, "docs/credits.md", false) && gx::excluded(s, "docs/other.md", false));
        check("excluded: game files stay",
              !gx::excluded(s, "scenes/start.scene", false) && !gx::excluded(s, "assets/scripts/boss.cpp", false) &&
              !gx::excluded(s, "assets/scripts/ai.py", false));
        check("excluded: every manifest (the game's is written fresh)", gx::excluded(s, "project.schizo", false) && gx::excluded(s, "leviathan.schizo", false));
        check("file name: Windows-safe, spaces kept, one at a time",
              gx::file_name_for("The Beacon: Part 2") == "The Beacon Part 2" && gx::file_name_for("  ") == "Game" &&
              gx::file_name_for("con") == "con_game" && gx::file_name_for("Ends.") == "Ends");
        check("prebuilt key: from the source path",
              gx::prebuilt_key("assets/scripts/Boss.cpp") == "assets_scripts_boss.cpp" &&
              gx::prebuilt_key("./assets\\scripts/../scripts/boss.cpp") == "assets_scripts_boss.cpp");
    }

    // ---- game.ini and the icon file ---------------------------------------
    {
        gx::GameInfo g;
        g.title = "The Beacon";
        g.version = "1.0.0";
        g.engine_version = "0.8.54";
        g.project_name = "TestGame";
        g.fullscreen = false;
        const gx::GameInfo r = gx::game_info_from_text(gx::game_info_to_text(g));
        check("game.ini: written and read back",
              r.title == g.title && r.version == g.version && r.engine_version == g.engine_version &&
              r.project_name == g.project_name && r.fullscreen == g.fullscreen);
        std::vector<uint8_t> rgba(40 * 20 * 4, 255);
        const auto ico = gx::make_ico(rgba, 40, 20);
        bool well_formed = ico.size() > 6 && ico[2] == 1 && ico[4] == 6;
        for (int i = 0; well_formed && i < 6; ++i) {
            const uint8_t* e = &ico[6 + 16 * i];
            const uint32_t size = e[8] | (e[9] << 8) | (e[10] << 16) | (static_cast<uint32_t>(e[11]) << 24);
            const uint32_t off = e[12] | (e[13] << 8) | (e[14] << 16) | (static_cast<uint32_t>(e[15]) << 24);
            well_formed = off + size <= ico.size() && e[4] == 1 && e[6] == 32;
        }
        check("icon: six sizes, each a 32-bit image inside the file", well_formed);
        // 40x20 centred in 16x16: the top rows are transparent, the middle opaque.
        const uint8_t* e16 = &ico[6];
        const uint32_t off16 = e16[12] | (e16[13] << 8) | (e16[14] << 16) | (static_cast<uint32_t>(e16[15]) << 24);
        const uint8_t* px = &ico[off16 + 40];
        const uint8_t top_alpha = px[(15 * 16 + 8) * 4 + 3];   // bottom-up: row 15 is the top
        const uint8_t mid_alpha = px[(8 * 16 + 8) * 4 + 3];
        check("icon: a wide image is letterboxed (transparent above, opaque in the middle)", top_alpha == 0 && mid_alpha == 255);
    }

    // ---- a project and its plan --------------------------------------------
    const fs::path root = fs::temp_directory_path() /
        ("gws_export_check_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000007));
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path proj = root / "MyGame";
    write(proj / "project.schizo", "name = MyGame\nengine_version = 0.8.0\ndefault_scene = scenes/main.scene\n");
    write(proj / "launch.schizo", "name = MyGame\ndefault_scene = scenes/arena.scene\n");
    write(proj / "scenes/main.scene", "ENTITY Player\nMESH assets/models/hero.glb\nTEXTURE art_source/ref.png\n");
    write(proj / "scenes/arena.scene", "SCRIPT assets/scripts/boss.cpp\n");
    write(proj / "assets/models/hero.glb", std::string(1000, 'x'));
    write(proj / "assets/scripts/boss.cpp", "// a native script\n");
    write(proj / "assets/scripts/ai.py", "import engine\n");
    write(proj / "assets/scripts/helper.cpp", "// included by boss.cpp; not a script\n");
    write(proj / "assets/scripts/SchizoScript.cs", "// the C# SDK\n");
    write(proj / "art_source/ref.png", std::string(5000, 'y'));
    write(proj / "art_source/hero.blend", std::string(9000, 'z'));
    write(proj / "saves/quick.sav", "save");
    write(proj / "NOTES.md", "notes");
    write(proj / "Play.cmd", "@echo off");
    write(proj / "editor.ini", "[layout]");
    write(proj / "script_cache/old.dll", "MZ");
    write(proj / "config/icon.bmp", tiny_bmp());

    gx::Settings s;
    s.start_scene = "scenes/arena.scene";
    s.icon = "config/icon.bmp";
    s.title = "My Game";
    s.version = "2.0";
    s.output_dir = (root / "out").string();
    {
        const gx::Plan p = gx::make_plan(proj, "MyGame", "scenes/main.scene", s);
        check("plan: no errors", p.errors.empty());
        check("plan: the game's files in",
              has(p.files, "scenes/main.scene") && has(p.files, "scenes/arena.scene") && has(p.files, "assets/models/hero.glb") &&
              has(p.files, "assets/scripts/boss.cpp") && has(p.files, "assets/scripts/ai.py") && has(p.files, "config/icon.bmp"));
        check("plan: the developer's files out",
              !has(p.files, "art_source/ref.png") && !has(p.files, "saves/quick.sav") && !has(p.files, "NOTES.md") &&
              !has(p.files, "Play.cmd") && !has(p.files, "editor.ini") && !has(p.files, "script_cache/old.dll") &&
              !has(p.files, "project.schizo") && !has(p.files, "launch.schizo"));
        check("plan: the C++ scripts the game names are built; a helper nothing names is not",
              p.native_scripts == std::vector<std::string>{"assets/scripts/boss.cpp"} && has(p.files, "assets/scripts/helper.cpp"));
        check("plan: the C# SDK is never built on its own", p.csharp_scripts.empty());
        bool art_first = !p.left_out.empty() && p.left_out[0].what == "art_source/" && p.left_out[0].files == 2 &&
                         p.left_out[0].bytes == 14000;
        check("plan: left out, grouped, largest first (art_source/: 2 files)", art_first);
        bool warned = false;
        for (const auto& w : p.warnings) warned |= w.find("art_source/ref.png") != std::string::npos;
        check("plan: a left-out file a scene uses is warned about", warned);
        check("plan: the folder and the zip named from the title and version",
              p.folder == fs::path(s.output_dir) / "My Game" &&
              p.archive == fs::path(s.output_dir) / "My Game-2.0-windows-x86_64.zip");
        gx::Settings bad = s;
        bad.start_scene = "scenes/missing.scene";
        check("plan: a missing start scene is an error", !gx::make_plan(proj, "MyGame", "", bad).errors.empty());
        bad = s;
        bad.output_dir = (proj / "exports").string();
        check("plan: exporting into the project is an error", !gx::make_plan(proj, "MyGame", "", bad).errors.empty());
    }

#ifdef _WIN32
    // ---- the whole export -----------------------------------------------------
    {
        const fs::path engine = root / "engine";
        const fs::path runtime = engine / "editor.exe";
        fs::create_directories(engine);
        fs::copy_file(fs::absolute(argv[0]).replace_extension(".exe"), runtime, fs::copy_options::overwrite_existing, ec);
        if (ec) fs::copy_file(fs::absolute(argv[0]), runtime, fs::copy_options::overwrite_existing, ec);
        write(engine / "helper.dll", "MZ not really");
        write(engine / "LICENSE", "licence");
        write(engine / "assets/scripts/schizo_script.h", "// api\n");

        gx::Request rq;
        rq.project_dir = proj;
        rq.project_name = "MyGame";
        rq.default_scene = "scenes/main.scene";
        rq.engine_version = "0.8.54";
        rq.settings = s;
        rq.runtime_exe = runtime;
        rq.engine_root = engine;
        std::vector<std::string> built;
        rq.build_native = [&](const std::string& rel, const fs::path& out, std::string&) {
            built.push_back(rel);
            write(out, "built " + rel);
            return true;
        };
        float last = -1.0f;
        bool monotonic = true;
        rq.progress = [&](float f, const std::string&) { monotonic &= f >= last; last = f; };
        const gx::Result r = gx::run(rq);
        if (!r.ok) std::printf("    export failed: %s\n", r.message.c_str());
        check("export: succeeds", r.ok);
        const fs::path g = root / "out" / "My Game";
        check("export: the program, renamed, with the engine's files beside it",
              fs::is_regular_file(g / "My Game.exe") && fs::is_regular_file(g / "helper.dll") &&
              fs::is_regular_file(g / "game-export.marker") && fs::is_regular_file(g / "assets/scripts/schizo_script.h") &&
              fs::is_regular_file(g / "licenses/GameWorldshaper-LICENSE.txt") && fs::is_regular_file(g / "README.txt"));
        check("export: the project's game files, not the developer's",
              fs::is_regular_file(g / "project/scenes/arena.scene") && fs::is_regular_file(g / "project/assets/scripts/ai.py") &&
              !fs::exists(g / "project/art_source") && !fs::exists(g / "project/saves") &&
              !fs::exists(g / "project/NOTES.md") && !fs::exists(g / "project/launch.schizo"));
        const std::string manifest = read(g / "project/project.schizo");
        check("export: its manifest starts the chosen scene, names this engine",
              manifest.find("default_scene = scenes/arena.scene") != std::string::npos &&
              manifest.find("engine_version = 0.8.54") != std::string::npos);
        check("export: the project's own manifest untouched",
              read(proj / "project.schizo").find("default_scene = scenes/main.scene") != std::string::npos);
        check("export: C++ scripts built through the callback into script_cache/prebuilt",
              built == std::vector<std::string>{"assets/scripts/boss.cpp"} &&
              fs::is_regular_file(g / "project/script_cache/prebuilt/assets_scripts_boss.cpp.dll"));
        const gx::GameInfo gi = gx::game_info_from_text(read(g / "game.ini"));
        check("export: game.ini", gi.title == "My Game" && gi.version == "2.0" && gi.project_name == "MyGame" && gi.fullscreen);
        check("export: the window icon beside the program", fs::is_regular_file(g / "icon.bmp"));
        check("export: progress only goes forward, to the end", monotonic && last == 1.0f);

        HMODULE mod = LoadLibraryExW((g / "My Game.exe").wstring().c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        const bool icon_in = mod && FindResourceW(mod, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(14)) != nullptr;
        const bool ver_in = mod && FindResourceW(mod, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(16)) != nullptr;
        if (mod) FreeLibrary(mod);
        check("export: the icon is inside the program", icon_in);
        check("export: version information is inside the program", ver_in);
        {
            DWORD handle = 0;
            const DWORD n = GetFileVersionInfoSizeW((g / "My Game.exe").wstring().c_str(), &handle);
            std::vector<char> buf(n);
            wchar_t* name = nullptr;
            UINT len = 0;
            const bool read_ok = n > 0 && GetFileVersionInfoW((g / "My Game.exe").wstring().c_str(), 0, n, buf.data()) &&
                                 VerQueryValueW(buf.data(), L"\\StringFileInfo\\040904B0\\ProductName",
                                                reinterpret_cast<void**>(&name), &len);
            check("export: Windows reads the product name (\"My Game\")", read_ok && name && std::wstring(name) == L"My Game");
        }
        {
            const std::string exe = read(g / "My Game.exe");
            const uint32_t pe = static_cast<uint8_t>(exe[0x3C]) | (static_cast<uint8_t>(exe[0x3D]) << 8);
            check("export: a windowed program (no console window)", exe.size() > pe + 100 && exe[pe + 4 + 20 + 68] == 2);
            DWORD bin = 0;
            check("export: still a runnable 64-bit program", GetBinaryTypeW((g / "My Game.exe").wstring().c_str(), &bin) && bin == SCS_64BIT_BINARY);
            // No debug sections or COFF symbol table left (a development build
            // has both; a resource update would have cut the table off).
            const uint32_t symtab = static_cast<uint8_t>(exe[pe + 12]) | (static_cast<uint8_t>(exe[pe + 13]) << 8) |
                                    (static_cast<uint8_t>(exe[pe + 14]) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(exe[pe + 15])) << 24);
            const uint16_t nsec = static_cast<uint16_t>(static_cast<uint8_t>(exe[pe + 6]) | (static_cast<uint8_t>(exe[pe + 7]) << 8));
            const uint16_t optsz = static_cast<uint16_t>(static_cast<uint8_t>(exe[pe + 20]) | (static_cast<uint8_t>(exe[pe + 21]) << 8));
            bool no_debug = symtab == 0;
            for (uint16_t i = 0; i < nsec; ++i) no_debug &= exe[pe + 24 + optsz + 40 * i] != '/';
            check("export: no debug sections or symbol table in the game's program", no_debug);
        }
        check("export: the .zip beside it", !r.archive.empty() && fs::is_regular_file(r.archive));
        {
            wchar_t sysdir[MAX_PATH]{};
            GetSystemDirectoryW(sysdir, MAX_PATH);
            const fs::path list = root / "zip.txt";
            const std::wstring cmd = L"cmd /c \"\"" + (fs::path(sysdir) / L"tar.exe").wstring() + L"\" -tf \"" +
                                     r.archive.wstring() + L"\" > \"" + list.wstring() + L"\"\"";
            _wsystem(cmd.c_str());
            const std::string l = read(list);
            check("export: the .zip holds the game folder", l.find("My Game/My Game.exe") != std::string::npos &&
                                                             l.find("My Game/project/scenes/arena.scene") != std::string::npos);
        }

        // Again, over the first: replaced, not refused.
        write(g / "stale.txt", "from the first export");
        const gx::Result r2 = gx::run(rq);
        check("export: a second export replaces the first", r2.ok && !fs::exists(g / "stale.txt") && fs::is_regular_file(g / "My Game.exe"));
        // A folder of the same name that is not an export is never deleted.
        fs::remove_all(g, ec);
        write(g / "precious.txt", "mine");
        const gx::Result r3 = gx::run(rq);
        check("export: a folder that is not an earlier export is left alone (and the export refused)",
              !r3.ok && fs::is_regular_file(g / "precious.txt"));
        // A failing script build leaves no half-made folder.
        fs::remove_all(g, ec);
        rq.build_native = [](const std::string&, const fs::path&, std::string& err) { err = "boss.cpp:1: error"; return false; };
        const gx::Result r4 = gx::run(rq);
        check("export: a script that does not build fails the export, leaving nothing",
              !r4.ok && r4.message.find("boss.cpp") != std::string::npos && !fs::exists(g) &&
              !fs::exists(root / "out" / "My Game.partial"));
    }
#endif

    fs::remove_all(root, ec);
    std::printf("\nexport_check: %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) std::printf("export_check: ALL OK\n");
    return g_fail == 0 ? 0 : 1;
}
