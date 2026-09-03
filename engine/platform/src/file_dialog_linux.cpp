#include "gws/platform/file_dialog.h"

#include <cstdio>
#include <string>

namespace gws::platform {
namespace {

std::string shell_quote(const char* text) {
    std::string out = "'";
    if (text) {
        for (const char* p = text; *p; ++p) {
            if (*p == '\'') out += "'\\''";
            else out += *p;
        }
    }
    out += "'";
    return out;
}

bool command_exists(const char* command) {
    const std::string check = std::string("command -v ") + command + " >/dev/null 2>&1";
    return std::system(check.c_str()) == 0;
}

std::string run_picker(const std::string& command) {
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return {};

    std::string result;
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }

    const int rc = pclose(pipe);
    if (rc != 0) return {};

    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    return result;
}

} // namespace

std::string browse_folder(const char* title) {
    const std::string quoted_title = shell_quote(title ? title : "Choose folder");

    // GNOME and many other desktops.
    if (command_exists("zenity")) {
        return run_picker("zenity --file-selection --directory --title=" + quoted_title);
    }

    // KDE Plasma fallback.
    if (command_exists("kdialog")) {
        return run_picker("kdialog --getexistingdirectory \"$HOME\" --title " + quoted_title);
    }

    return {};
}

} // namespace gws::platform
