// ====================
// userdirs_check — where per-user files live, on Windows and on Linux
// ====================
//
// Before this existed, every per-user file resolved %APPDATA% or %LOCALAPPDATA%
// and nothing else. On Linux those are unset, so render settings, the Vulkan
// pipeline cache and every crash and hang report fell back to the working
// directory: the repo root, when the editor is run from a checkout. Five such
// files were committed by accident in the merge that brought Linux support.
//
// The assertions target the ways this goes wrong quietly:
//
//   * a Windows path that MOVES. Nothing errors; the user's saved quality
//     preset is simply gone, and `gws crash` stops finding reports;
//   * an XDG value that is relative or empty being trusted, which puts files
//     back under the working directory, the exact bug being fixed;
//   * two kinds collapsing into one folder, so clearing a cache takes
//     settings or crash reports with it.
//
// Both OSes are resolved on either host: resolve_user_dir() takes the OS and
// the environment as inputs, so the Linux rules are checked on Windows CI too.

#include "gws/platform/user_dirs.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

using gws::platform::HostOs;
using gws::platform::UserDir;
using gws::platform::resolve_user_dir;

static int g_failures = 0;
static void check(const char* name, bool ok) {
    std::printf("  [%s] %s\n", ok ? "OK  " : "FAIL", name);
    if (!ok) ++g_failures;
}

// A fake environment: only the variables listed are set.
struct Env {
    std::map<std::string, std::string> vars;
    gws::platform::EnvLookup lookup() const {
        return [this](const char* name) -> const char* {
            auto it = vars.find(name);
            return it == vars.end() ? nullptr : it->second.c_str();
        };
    }
};

// Compare with forward slashes so a Posix answer computed on a Windows host
// (where path::operator/ inserts a backslash) compares equal.
static std::string g(const std::filesystem::path& p) { return p.generic_string(); }

static std::string win(UserDir d, const Env& e) { return g(resolve_user_dir(d, HostOs::Windows, e.lookup())); }
static std::string nix(UserDir d, const Env& e) { return g(resolve_user_dir(d, HostOs::Posix, e.lookup())); }

int main() {
    std::printf("userdirs_check — per-user directories\n");

    std::printf("\n[group] Windows paths are unchanged (existing files stay found)\n");
    {
        Env e{{{"APPDATA", "C:/Users/u/AppData/Roaming"},
               {"LOCALAPPDATA", "C:/Users/u/AppData/Local"},
               {"USERPROFILE", "C:/Users/u"}}};
        check("config = %APPDATA%/GameWorldshaper",
              win(UserDir::Config, e) == "C:/Users/u/AppData/Roaming/GameWorldshaper");
        check("cache = %LOCALAPPDATA%/GameWorldshaper/cache",
              win(UserDir::Cache, e) == "C:/Users/u/AppData/Local/GameWorldshaper/cache");
        check("diagnostics = %LOCALAPPDATA%/GameWorldshaper/diagnostics",
              win(UserDir::Diagnostics, e) == "C:/Users/u/AppData/Local/GameWorldshaper/diagnostics");
    }
    {
        // The render-settings fallback that has always existed.
        Env e{{{"USERPROFILE", "C:/Users/u"}}};
        check("config without %APPDATA% falls back to %USERPROFILE%/.gameworldshaper",
              win(UserDir::Config, e) == "C:/Users/u/.gameworldshaper");
    }
    {
        // Windows must not start honouring Linux variables: a stray HOME (Git
        // Bash sets one) would otherwise move every file on a real user's machine.
        Env e{{{"HOME", "/home/u"}, {"XDG_CACHE_HOME", "/x/cache"}}};
        check("Windows ignores HOME and XDG_* for the cache", win(UserDir::Cache, e).empty());
        check("Windows ignores HOME and XDG_* for diagnostics", win(UserDir::Diagnostics, e).empty());
        check("Windows ignores HOME and XDG_* for config", win(UserDir::Config, e).empty());
    }
    {
        Env e;
        check("Windows with nothing set -> empty (caller decides)", win(UserDir::Config, e).empty());
    }

    std::printf("\n[group] Linux follows XDG\n");
    {
        Env e{{{"HOME", "/home/u"}}};
        // Same directory project.cpp already uses for recent projects, so a
        // user finds all of the engine's settings in one place.
        check("config = ~/.config/gameworldshaper",
              nix(UserDir::Config, e) == "/home/u/.config/gameworldshaper");
        check("cache = ~/.cache/gameworldshaper",
              nix(UserDir::Cache, e) == "/home/u/.cache/gameworldshaper");
        check("diagnostics = ~/.local/state/gameworldshaper/diagnostics",
              nix(UserDir::Diagnostics, e) == "/home/u/.local/state/gameworldshaper/diagnostics");
    }
    {
        Env e{{{"HOME", "/home/u"},
               {"XDG_CONFIG_HOME", "/cfg"},
               {"XDG_CACHE_HOME", "/cch"},
               {"XDG_STATE_HOME", "/st"}}};
        check("XDG_CONFIG_HOME wins over HOME", nix(UserDir::Config, e) == "/cfg/gameworldshaper");
        check("XDG_CACHE_HOME wins over HOME",  nix(UserDir::Cache, e) == "/cch/gameworldshaper");
        check("XDG_STATE_HOME wins over HOME",
              nix(UserDir::Diagnostics, e) == "/st/gameworldshaper/diagnostics");
    }

    std::printf("\n[group] invalid XDG values are ignored, not trusted\n");
    {
        // The spec: a relative path in an XDG variable is invalid and must be
        // ignored. Trusting it resolves against the working directory, which
        // is the bug this file exists to prevent.
        Env e{{{"HOME", "/home/u"},
               {"XDG_CONFIG_HOME", "relative/cfg"},
               {"XDG_CACHE_HOME", ""},
               {"XDG_STATE_HOME", "./st"}}};
        check("relative XDG_CONFIG_HOME ignored",
              nix(UserDir::Config, e) == "/home/u/.config/gameworldshaper");
        check("empty XDG_CACHE_HOME ignored",
              nix(UserDir::Cache, e) == "/home/u/.cache/gameworldshaper");
        check("'./' XDG_STATE_HOME ignored",
              nix(UserDir::Diagnostics, e) == "/home/u/.local/state/gameworldshaper/diagnostics");
    }
    {
        Env e{{{"HOME", ""}}};
        check("empty HOME -> empty (caller decides)", nix(UserDir::Config, e).empty());
    }
    {
        Env e{{{"HOME", "relative-home"}}};
        check("relative HOME -> empty, not a path under the working directory",
              nix(UserDir::Cache, e).empty());
    }
    {
        // Wine and some CI images export Windows names on Linux; the Posix
        // rules must not pick them up.
        Env e{{{"HOME", "/home/u"}, {"APPDATA", "C:/x"}, {"LOCALAPPDATA", "C:/y"}}};
        check("Posix ignores APPDATA / LOCALAPPDATA",
              nix(UserDir::Cache, e) == "/home/u/.cache/gameworldshaper");
    }

    std::printf("\n[group] the kinds never share a folder\n");
    {
        Env w{{{"APPDATA", "C:/r"}, {"LOCALAPPDATA", "C:/l"}}};
        Env p{{{"HOME", "/home/u"}}};
        check("Windows: cache != diagnostics", win(UserDir::Cache, w) != win(UserDir::Diagnostics, w));
        check("Windows: config != cache",      win(UserDir::Config, w) != win(UserDir::Cache, w));
        check("Linux: cache != diagnostics",   nix(UserDir::Cache, p) != nix(UserDir::Diagnostics, p));
        check("Linux: config != cache",        nix(UserDir::Config, p) != nix(UserDir::Cache, p));
        check("Linux: config != diagnostics",  nix(UserDir::Config, p) != nix(UserDir::Diagnostics, p));
    }

    std::printf("\n[group] this host\n");
    {
#ifdef _WIN32
        const char* home_var = "LOCALAPPDATA";
#else
        const char* home_var = "HOME";
#endif
        // Every CI runner and every real machine has one of these. When it is
        // set, the real resolver must produce an absolute path, never one that
        // quietly resolves against the working directory.
        if (std::getenv(home_var)) {
            const auto d = gws::platform::user_dir(UserDir::Diagnostics);
            check("user_dir(Diagnostics) is non-empty on this host", !d.empty());
            check("user_dir(Diagnostics) is absolute on this host", d.is_absolute());
        } else {
            std::printf("  [SKIP] %s is not set on this host\n", home_var);
        }
    }

    std::printf("\n%s — %d failure(s)\n", g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
