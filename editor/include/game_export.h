#pragma once
// ============================================================================
// Game export -- a project made into a game a player runs (Windows).
// ============================================================================
//
// The exported game is the engine's own program in game mode beside a copy of
// the project, so what a player sees is what Play showed. An export is:
//
//   <output>/<Game>/                 the game, ready to run
//       <Game>.exe                   the engine, renamed; the project's icon and
//                                    version inside; a windowed program (no console)
//       *.dll, glslangValidator.exe  what the engine needs beside it
//       game.ini                     title, version, how it starts (GameInfo)
//       game-export.marker           "this folder is a game" (the exe looks for it)
//       icon.png                     the window's icon
//       README.txt, licenses/        for the player
//       assets/scripts/              the engine's script headers
//       project/                     the project, without what a game does not need
//           script_cache/prebuilt/   every C++ and C# script, built at export
//   <output>/<Game>-<version>-windows-x86_64.zip   the folder, to share
//
// A project's choices are in config/export.ini (Settings). Nothing here
// changes the project itself: the start scene is written into the exported
// copy's manifest only.
//
// The pure parts (settings text, patterns, the plan, the icon file, game.ini)
// are tested by tools/export_check; run() is the whole export.

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace schizo::editor::gexport {

inline constexpr const char* kSettingsFile = "config/export.ini";
inline constexpr const char* kGameInfoFile = "game.ini";
inline constexpr const char* kMarkerFile   = "game-export.marker";
/// In an exported project: the scripts built at export, one per source.
inline constexpr const char* kPrebuiltDir  = "script_cache/prebuilt";

// ---- Settings (config/export.ini) ------------------------------------------

struct Settings {
    std::string title;                  // the game's name; "" = the project's name
    std::string version = "1.0.0";
    std::string start_scene;            // project-relative; "" = the project's default scene
    std::string icon;                   // project-relative image (.png); "" = none
    bool        fullscreen = true;      // how the game starts the first time
    bool        make_zip   = true;      // also pack the folder into a .zip
    std::vector<std::string> exclude;   // left out, on top of the defaults
    std::vector<std::string> include;   // kept although a default leaves them out
    std::string output_dir;             // the folder last exported to (absolute)
};

std::string settings_to_text(const Settings& s);
Settings    settings_from_text(const std::string& text);
/// <project>/config/export.ini, or the defaults when there is none.
Settings    load_settings(const std::filesystem::path& project_dir);
bool        save_settings(const std::filesystem::path& project_dir, const Settings& s);

// ---- What goes in -----------------------------------------------------------
//
// Patterns, one per line, like .gitignore: `*` and `?` within a name, a name
// alone matches at any depth (`*.md`, `Thumbs.db`), a pattern with a `/` inside
// is from the project root (`config/export.ini`), a trailing `/` matches a
// folder (`art_source/`). Case does not matter.

/// What every export leaves out: source art, saves, caches, editor files,
/// notes and launchers.
const std::vector<std::string>& default_excludes();
bool pattern_matches(const std::string& pattern, const std::string& rel_path, bool is_dir);
/// Is this project-relative path ('/'-separated) left out?
bool excluded(const Settings& s, const std::string& rel_path, bool is_dir);

/// A name Windows takes for a file or folder; spaces kept ("The Beacon").
std::string file_name_for(const std::string& title);
/// A prebuilt script's file name from its project-relative source path:
/// "assets/scripts/Boss.cpp" -> "assets_scripts_boss.cpp". Inline: the script
/// backends key their lookups with it and link nothing else from here.
/// A '\' is a separator on every platform (a scene saved on Windows may name
/// "assets\scripts\boss.cpp"; on Linux std::filesystem reads '\' as part of a
/// name, so "a\b/../c" lost its "a" there -- the same script, another key).
inline std::string prebuilt_key(const std::string& rel_source) {
    std::string src = rel_source;
    for (char& c : src) if (c == '\\') c = '/';
    std::string r = std::filesystem::path(src).lexically_normal().generic_string();
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    while (r.rfind("./", 0) == 0) r.erase(0, 2);
    std::string out;
    for (char c : r) out += (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_') ? c : '_';
    return out;
}

struct LeftOut {
    std::string what;       // "art_source/" or "*.md"
    uint64_t    bytes = 0;
    int         files = 0;
};

struct Plan {
    std::string game_name;                 // file_name_for(title)
    std::string title, version, start_scene;
    std::vector<std::string> files;        // project-relative, '/'-separated
    uint64_t    bytes = 0;
    std::vector<std::string> native_scripts;   // .cpp / .cc among the files
    std::vector<std::string> csharp_scripts;   // .cs among the files
    std::vector<LeftOut>     left_out;         // grouped, largest first
    std::vector<std::string> warnings;
    std::vector<std::string> errors;           // the export cannot run
    std::filesystem::path    folder, archive;  // where it goes (empty without output_dir)
};

/// What exporting this project would make. Reads the disk; changes nothing.
Plan make_plan(const std::filesystem::path& project_dir, const std::string& project_name,
               const std::string& default_scene, const Settings& s);

// ---- The export --------------------------------------------------------------

/// Builds one script for the game: `rel_source` (project-relative, the
/// process's current directory is the project) into `out` (a .dll for C++, a
/// folder for C#). false + `error` when it does not build.
using BuildFn = std::function<bool(const std::string& rel_source,
                                   const std::filesystem::path& out, std::string& error)>;

struct Request {
    std::filesystem::path project_dir;
    std::string           project_name, default_scene, engine_version;
    Settings              settings;
    std::filesystem::path runtime_exe;     // the engine program to copy (editor.exe)
    std::filesystem::path engine_root;     // has assets/scripts, LICENSE, THIRD_PARTY_NOTICES.md
    BuildFn build_native, build_csharp;
    std::function<void(float, const std::string&)> progress;   // 0..1, what it is doing
};

struct Result {
    bool ok = false;
    std::string message;                   // what was made, or why it failed
    std::filesystem::path folder, archive, exe;
    std::vector<std::string> warnings;
};

Result run(const Request& r);

// ---- The game's identity -----------------------------------------------------

/// game.ini beside an exported game's program.
struct GameInfo {
    std::string title, version, engine_version, project_name;
    bool        fullscreen = true;          // the first start; the player's choice wins after
};
std::string game_info_to_text(const GameInfo& g);
GameInfo    game_info_from_text(const std::string& text);

/// An .ico file for an RGBA image: 16, 24, 32, 48, 64 and 256 pixels, each a
/// 32-bit bitmap scaled from the image (any size, any aspect: centred).
std::vector<uint8_t> make_ico(const std::vector<uint8_t>& rgba, int w, int h);

/// Windows: put an icon (RT_GROUP_ICON 1) and version information into an
/// .exe, and make it a windowed program (no console window). Each false +
/// `error` on failure; the file is unchanged then.
bool embed_icon(const std::filesystem::path& exe, const std::vector<uint8_t>& rgba, int w, int h,
                std::string& error);
bool embed_version(const std::filesystem::path& exe, const std::string& product,
                   const std::string& version, const std::string& file_name, std::string& error);
bool set_windowed_program(const std::filesystem::path& exe, std::string& error);
/// Drop a development build's debug sections and symbol table (what
/// `strip --strip-debug` does); a release build is left as it is.
bool strip_debug_sections(const std::filesystem::path& exe, std::string& error);

}  // namespace schizo::editor::gexport
