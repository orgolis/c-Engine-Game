#include "ai_runtime_manager.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>

#ifndef _WIN32
#include <sys/stat.h>

#include <cerrno>
#endif

namespace fs = std::filesystem;

namespace schizo::editor {
namespace {

fs::path environment_path(const char* name) {
    const char* value = std::getenv(name);
    return value && *value ? fs::path(value) : fs::path{};
}

fs::path home_directory() {
#ifdef _WIN32
    fs::path home = environment_path("USERPROFILE");
    if (!home.empty())
        return home;
    const fs::path drive = environment_path("HOMEDRIVE");
    const fs::path path = environment_path("HOMEPATH");
    return drive.empty() || path.empty() ? fs::path{} : drive / path;
#else
    return environment_path("HOME");
#endif
}

fs::path managed_runtime_root() {
#ifdef _WIN32
    fs::path root = environment_path("LOCALAPPDATA");
    if (!root.empty())
        return root / "GameWorldshaper" / "ai-runtime";
    root = home_directory();
    return root.empty() ? fs::path{} : root / ".gameworldshaper" / "ai-runtime";
#elif defined(__APPLE__)
    const fs::path home = home_directory();
    return home.empty() ? fs::path{} : home / "Library" / "Application Support" / "GameWorldshaper" / "ai-runtime";
#else
    fs::path root = environment_path("XDG_DATA_HOME");
    if (!root.empty() && root.is_absolute())
        return root / "gameworldshaper" / "ai-runtime";
    const fs::path home = home_directory();
    return home.empty() ? fs::path{} : home / ".local" / "share" / "gameworldshaper" / "ai-runtime";
#endif
}

fs::path managed_codex_path() {
    const fs::path root = managed_runtime_root();
    if (root.empty())
        return {};
    fs::path path = root / "codex" / "bin" / "codex";
#ifdef _WIN32
    path += ".exe";
#endif
    return path;
}

fs::path default_claude_path() {
    const fs::path home = home_directory();
    if (home.empty())
        return {};
    fs::path path = home / ".local" / "bin" / "claude";
#ifdef _WIN32
    path += ".exe";
#endif
    return path;
}

fs::path executable_on_path(const std::string& name) {
    const char* raw_path = std::getenv("PATH");
    if (!raw_path)
        return {};
#ifdef _WIN32
    constexpr char separator = ';';
    const char* extensions[] = {"", ".exe", ".cmd", ".bat"};
#else
    constexpr char separator = ':';
    const char* extensions[] = {""};
#endif
    std::stringstream paths(raw_path);
    std::string directory;
    while (std::getline(paths, directory, separator)) {
        if (directory.empty())
            continue;
        for (const char* extension : extensions) {
            std::error_code ec;
            const fs::path candidate = fs::path(directory) / (name + extension);
            if (fs::is_regular_file(candidate, ec))
                return candidate;
        }
    }
    return {};
}

std::string shell_quote(const std::string& value) {
#ifdef _WIN32
    std::string result = "\"";
    for (char c : value) {
        if (c == '"')
            result += "\\\"";
        else
            result += c;
    }
    return result + "\"";
#else
    std::string result = "'";
    for (char c : value) {
        if (c == '\'')
            result += "'\\''";
        else
            result += c;
    }
    return result + "'";
#endif
}

#ifdef _WIN32
std::string powershell_quote(const std::string& value) {
    std::string result = "'";
    for (char c : value) {
        if (c == '\'')
            result += "''";
        else
            result += c;
    }
    return result + "'";
}

fs::path make_private_temp_directory() {
    std::random_device random;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 16; ++attempt) {
        const fs::path candidate =
            fs::temp_directory_path() / ("worldshaper-runtime-" + std::to_string(stamp) + "-" +
                                         std::to_string(random()) + "-" + std::to_string(attempt));
#ifdef _WIN32
        std::error_code ec;
        if (fs::create_directory(candidate, ec))
            return candidate;
#else
        if (::mkdir(candidate.c_str(), S_IRWXU) == 0)
            return candidate;
        if (errno != EEXIST)
            return {};
#endif
    }
    return {};
}

bool write_file(const fs::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return static_cast<bool>(output);
}
#endif

bool create_private_directory(const fs::path& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec)
        return false;
#ifndef _WIN32
    fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace, ec);
    if (ec)
        return false;
#endif
    return true;
}

#ifdef _WIN32
int run_powershell_script(const std::string& script) {
    const fs::path temp = make_private_temp_directory();
    if (temp.empty())
        return -1;
    const fs::path script_path = temp / "install.ps1";
    if (!write_file(script_path, script)) {
        std::error_code ec;
        fs::remove_all(temp, ec);
        return -1;
    }

    const fs::path powershell = executable_on_path("powershell.exe");
    if (powershell.empty()) {
        std::error_code ec;
        fs::remove_all(temp, ec);
        return -1;
    }
    const std::string command = shell_quote(powershell.string()) +
                                " -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " +
                                shell_quote(script_path.string()) + " > NUL 2>&1";
    const int exit_code = std::system(command.c_str());
    std::error_code ec;
    fs::remove_all(temp, ec);
    return exit_code;
}
#endif

}  // namespace

fs::path FindAiRuntime(AiRuntimeProvider provider) {
    std::error_code ec;
    if (provider == AiRuntimeProvider::Codex) {
        const fs::path managed = managed_codex_path();
        if (!managed.empty() && fs::is_regular_file(managed, ec))
            return managed;
        return executable_on_path("codex");
    }

    const fs::path from_path = executable_on_path("claude");
    if (!from_path.empty())
        return from_path;
    const fs::path official = default_claude_path();
    return !official.empty() && fs::is_regular_file(official, ec) ? official : fs::path{};
}

AiRuntimeInstallResult InstallAiRuntime(AiRuntimeProvider provider) {
    AiRuntimeInstallResult result;
#ifdef _WIN32
    std::string script = "$ErrorActionPreference = 'Stop'\r\n";
    if (provider == AiRuntimeProvider::Codex) {
        const fs::path managed_root = managed_runtime_root();
        if (managed_root.empty()) {
            result.error = "Could not find the per-user data directory.";
            return result;
        }
        const fs::path root = managed_root / "codex";
        const fs::path bin = root / "bin";
        const fs::path state = root / "state";
        if (!create_private_directory(bin) || !create_private_directory(state)) {
            result.error = "Could not create the private AI runtime directory.";
            return result;
        }
        script += "$env:CODEX_NON_INTERACTIVE = '1'\r\n";
        script += "$env:CODEX_INSTALL_DIR = " + powershell_quote(bin.string()) + "\r\n";
        script += "$env:CODEX_HOME = " + powershell_quote(state.string()) + "\r\n";
        script +=
            "Invoke-Expression (Invoke-RestMethod -Uri "
            "'https://chatgpt.com/codex/install.ps1')\r\n";
    } else {
        script +=
            "Invoke-Expression (Invoke-RestMethod -Uri "
            "'https://claude.ai/install.ps1')\r\n";
    }
    if (run_powershell_script(script) != 0) {
        result.error = "The official runtime download or installation failed.";
        return result;
    }
#else
    const fs::path curl = executable_on_path("curl");
    if (curl.empty()) {
        result.error = "The system download helper (curl) is unavailable.";
        return result;
    }

    std::string command;
    if (provider == AiRuntimeProvider::Codex) {
        const fs::path managed_root = managed_runtime_root();
        if (managed_root.empty()) {
            result.error = "Could not find the per-user data directory.";
            return result;
        }
        const fs::path root = managed_root / "codex";
        const fs::path bin = root / "bin";
        const fs::path state = root / "state";
        if (!create_private_directory(bin) || !create_private_directory(state)) {
            result.error = "Could not create the private AI runtime directory.";
            return result;
        }
        command = shell_quote(curl.string()) +
                  " -fsSL https://chatgpt.com/codex/install.sh | "
                  "env CODEX_NON_INTERACTIVE=1 CODEX_INSTALL_DIR=" +
                  shell_quote(bin.string()) + " CODEX_HOME=" + shell_quote(state.string()) + " /bin/sh";
    } else {
        const fs::path bash = fs::is_regular_file("/bin/bash") ? "/bin/bash" : executable_on_path("bash");
        if (bash.empty()) {
            result.error = "The shell required by the official Claude installer is unavailable.";
            return result;
        }
        command = shell_quote(curl.string()) + " -fsSL https://claude.ai/install.sh | " + shell_quote(bash.string());
    }
    command = "(" + command + ") > /dev/null 2>&1";
    if (std::system(command.c_str()) != 0) {
        result.error = "The official runtime download or installation failed.";
        return result;
    }
#endif

    if (FindAiRuntime(provider).empty()) {
        result.error = "The installer completed, but the runtime executable was not found.";
        return result;
    }
    result.ok = true;
    return result;
}

}  // namespace schizo::editor
