#pragma once

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

namespace schizo::editor {

inline std::string AiProviderFailureMessage(const std::string& diagnostics) {
    // Never display raw stderr: Codex echoes the full prompt, scripts and
    // scene before its error. Only classify the last actual diagnostic line.
    std::string clean;
    bool escape = false;
    for (char ch : diagnostics) {
        if (!escape && ch == '\x1b') { escape = true; continue; }
        if (escape) {
            if (std::isalpha(static_cast<unsigned char>(ch))) escape = false;
            continue;
        }
        clean.push_back(ch);
    }
    std::istringstream lines(clean);
    std::string line, last_error;
    while (std::getline(lines, line)) {
        std::transform(line.begin(), line.end(), line.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos) continue;
        line.erase(0, first);
        if (line.starts_with("error:") || line.starts_with("bwrap:")) last_error = line;
    }
    if (last_error.find("at capacity") != std::string::npos ||
        last_error.find("overloaded") != std::string::npos ||
        last_error.find("server_is_overloaded") != std::string::npos)
        return "The selected model is temporarily at capacity on the provider's servers. "
               "Choose another model above or try again later. This is not a login error. "
               "Your model was not changed and no project changes were applied.";
    if (last_error.find("usage limit") != std::string::npos ||
        last_error.find("quota") != std::string::npos ||
        last_error.find("rate limit") != std::string::npos)
        return "The provider reported a usage or rate limit. Check usage/reset times before retrying. "
               "No project changes were applied.";
    if (last_error.find("unauthorized") != std::string::npos ||
        last_error.find("authentication") != std::string::npos ||
        last_error.find("token expired") != std::string::npos)
        return "The provider rejected authentication. Refresh the account connection and sign in again if needed. "
               "No project changes were applied.";
    if (last_error.starts_with("bwrap:"))
        return "The local provider sandbox could not start. No project changes were applied.";
    if (last_error.find("model") != std::string::npos &&
        (last_error.find("not found") != std::string::npos ||
         last_error.find("not supported") != std::string::npos ||
         last_error.find("does not exist") != std::string::npos))
        return "The selected model is unavailable. Refresh the model list and choose an available model. "
               "No project changes were applied.";
    return "The provider request failed. Check the account/model availability or try again later. "
           "No project changes were applied. Raw diagnostics are hidden to avoid exposing project data.";
}

}  // namespace schizo::editor
